#include "main.h"

#include "driver/twai.h"

#define CAN_ALERT_ERROR                                                                                       \
  (TWAI_ALERT_RECOVERY_IN_PROGRESS | TWAI_ALERT_ARB_LOST | TWAI_ALERT_ABOVE_ERR_WARN | TWAI_ALERT_BUS_ERROR | \
    TWAI_ALERT_TX_FAILED | TWAI_ALERT_RX_QUEUE_FULL | TWAI_ALERT_ERR_PASS | TWAI_ALERT_BUS_OFF |             \
    TWAI_ALERT_RX_FIFO_OVERRUN)

#define CAN_ALERT_RECOVERED (TWAI_ALERT_ERR_ACTIVE | TWAI_ALERT_BUS_RECOVERED)
#define CAN_ALERT_ENABLED   (CAN_ALERT_ERROR | CAN_ALERT_RECOVERED)
#define CAN_ERROR_WARNING_THRESHOLD 96U

static esp_err_t can_driver_start(void) {
  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_15, GPIO_NUM_16, TWAI_MODE_NORMAL);
  g_config.rx_queue_len          = 1024;
  g_config.alerts_enabled        = CAN_ALERT_ENABLED;
  twai_timing_config_t t_config  = TWAI_TIMING_CONFIG_250KBITS();
  twai_filter_config_t f_config  = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  esp_err_t err = twai_driver_install(&g_config, &t_config, &f_config);
  if (err != ESP_OK) return err;

  err = twai_start();
  if (err != ESP_OK) twai_driver_uninstall();
  return err;
}

static void can_driver_stop(void) {
  twai_stop();
  twai_driver_uninstall();
}

static bool can_driver_status_healthy(void) {
  twai_status_info_t status = { 0 };
  if (twai_get_status_info(&status) != ESP_OK) return false;

  return status.state == TWAI_STATE_RUNNING && status.tx_error_counter < CAN_ERROR_WARNING_THRESHOLD &&
         status.rx_error_counter < CAN_ERROR_WARNING_THRESHOLD;
}

static motor_runtime_t motor;
static bool motor_rx_seen;

static inline uint16_t can_read_le16(const uint8_t *data, size_t offset) {
  return (uint16_t)data[offset] | ((uint16_t)data[offset + 1] << 8);
}

static void motor_publish_rpm(uint16_t rpm_raw, TickType_t now) {
  display_can.ez_rpm_raw   = rpm_raw;
  display_can.ez_rpm_tick  = now;
  display_can.ez_rpm_valid = 1;
}

static void motor_parse_msg1(const uint8_t data[8], TickType_t now) {
  uint16_t rpm_raw = can_read_le16(data, 6);

  motor.bus_voltage      = (float)can_read_le16(data, 0) * 0.1f;
  motor.bus_current      = (float)can_read_le16(data, 2) * 0.1f - 3200.0f;
  motor.phase_current    = (float)can_read_le16(data, 4) * 0.1f - 3200.0f;
  motor.rpm              = motor_decode_rpm(rpm_raw);
  motor.motor_rpm_valid  = true;
  motor.motor_msg1_ready = true;
  motor.motor_valid      = false;
  motor.motor_msg1_tick  = now;
  motor.last_motor_rx    = now;
  motor_rx_seen          = true;

  motor_publish_rpm(rpm_raw, now);
  debug_monitor_publish_motor(&motor);
}

static void motor_parse_msg2(const uint8_t data[8], TickType_t now) {
  motor.controller_temp = (int)data[0] - 40;
  motor.motor_temp      = (int)data[1] - 40;
  motor.accelerator     = data[2];
  motor.gear            = data[3] & 0x07U;
  motor.brake           = ((data[3] >> 3) & 0x01U) != 0;
  motor.op_mode         = (data[3] >> 4) & 0x07U;
  motor.dc_contactor    = ((data[3] >> 7) & 0x01U) != 0;
  motor.err_byte4       = data[4];
  motor.err_byte5       = data[5];
  motor.err_byte6       = data[6];
  motor.life_signal     = (data[7] >> 4) & 0x0FU;
  motor.last_motor_rx   = now;
  motor_rx_seen         = true;

  motor.motor_valid      = motor.motor_msg1_ready &&
                           now - motor.motor_msg1_tick <= pdMS_TO_TICKS(MOTOR_PAIR_WINDOW_MS);
  motor.motor_msg1_ready = false;
  debug_monitor_publish_motor(&motor);
}

static void motor_invalidate(void) {
  motor.motor_msg1_ready = false;
  motor.motor_valid      = false;
  motor.motor_rpm_valid  = false;
  display_can.ez_rpm_valid = 0;
  debug_monitor_publish_motor(&motor);
}

static void motor_check_timeout(TickType_t now) {
  if (motor.motor_rpm_valid &&
      now - motor.motor_msg1_tick >= pdMS_TO_TICKS(MOTOR_RX_TIMEOUT_MS)) {
    motor.motor_rpm_valid    = false;
    motor.motor_valid        = false;
    display_can.ez_rpm_valid = 0;
    debug_monitor_publish_motor(&motor);
  }

  if (motor_rx_seen && now - motor.last_motor_rx >= pdMS_TO_TICKS(MOTOR_RX_TIMEOUT_MS)) {
    motor_rx_seen = false;
    motor_invalidate();
    ESP_LOGW("CAN", "EZkontrol motor data timeout (%d ms); CAN driver remains online",
             MOTOR_RX_TIMEOUT_MS);
  }
}

static const uint8_t bms_data_ids[NUM_DALY_DATA_IDS] = {
  0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x98,
};

static bms_runtime_t bms;

typedef enum {
  BMS_POLL_SEND,
  BMS_POLL_WAIT_RESPONSE,
} bms_poll_phase_t;

typedef struct {
  bms_poll_phase_t phase;
  uint8_t data_index;
  uint8_t expected_frames;
  uint16_t seen_frames;
  bool got_response;
  TickType_t next_action_tick;
  TickType_t response_deadline;
  TickType_t last_frame_tick;
  unsigned tx_failures;
  bool tx_failure_reported;
} bms_poll_state_t;

static bms_poll_state_t bms_poll;

static inline uint16_t can_read_be16(const uint8_t *data, size_t offset) {
  return ((uint16_t)data[offset] << 8) | (uint16_t)data[offset + 1];
}

static inline bool can_tick_reached(TickType_t now, TickType_t deadline) {
  return (int32_t)(now - deadline) >= 0;
}

static uint8_t bms_normalize_frame_number(uint8_t raw) {
  if (raw == 0xFFU) return 0xFFU;
  if (raw == 0U) return 0U;
  return raw - 1U;
}

static uint16_t bms_expected_frame_mask(uint8_t expected_frames) {
  if (expected_frames >= 16U) return 0xFFFFU;
  return (uint16_t)(((uint16_t)1U << expected_frames) - 1U);
}

static void bms_parse_90(const uint8_t data[8], TickType_t now) {
  uint16_t raw_current = can_read_be16(data, 4);
  uint16_t raw_soc     = can_read_be16(data, 6);

  bms.pack_voltage   = (float)can_read_be16(data, 0) * 0.1f;
  bms.gather_voltage = (float)can_read_be16(data, 2) * 0.1f;
  bms.current        = ((int32_t)raw_current - 30000) * 0.1f;
  bms.soc            = (float)raw_soc * 0.1f;

  display_can.bms_soc_raw   = raw_soc;
  display_can.bms_soc_tick  = now;
  display_can.bms_soc_valid = 1;
}

static void bms_parse_91(const uint8_t data[8]) {
  bms.max_cell_voltage = can_read_be16(data, 0);
  bms.max_cell_no      = data[2];
  bms.min_cell_voltage = can_read_be16(data, 3);
  bms.min_cell_no      = data[5];
}

static void bms_parse_92(const uint8_t data[8]) {
  bms.max_temp           = (int8_t)((int16_t)data[0] - 40);
  bms.max_temp_sensor_no = data[1];
  bms.min_temp           = (int8_t)((int16_t)data[2] - 40);
  bms.min_temp_sensor_no = data[3];
}

static void bms_parse_93(const uint8_t data[8]) {
  bms.charge_state    = data[0];
  bms.charge_mos      = data[1];
  bms.discharge_mos   = data[2];
  bms.bms_life_cycles = data[3];
  bms.remain_capacity = ((uint32_t)data[4] << 24) | ((uint32_t)data[5] << 16) |
                        ((uint32_t)data[6] << 8) | (uint32_t)data[7];
}

static void bms_parse_94(const uint8_t data[8]) {
  bms.cell_string_count = data[0];
  bms.temp_sensor_count = data[1];
  bms.charger_connected = data[2];
  bms.load_connected    = data[3];
  bms.di_do_flags       = data[4];
}

static void bms_parse_95(const uint8_t data[8]) {
  uint8_t frame_number = bms_normalize_frame_number(data[0]);
  if (frame_number == 0xFFU) return;

  for (uint8_t i = 0; i < 3U; i++) {
    uint8_t cell_index = frame_number * 3U + i;
    if (cell_index >= DALY_MAX_CELLS) continue;

    bms.cell_voltage[cell_index] = can_read_be16(data, 1U + i * 2U);
    if (cell_index + 1U > bms.cell_voltage_count) {
      bms.cell_voltage_count = cell_index + 1U;
    }
  }
}

static void bms_parse_96(const uint8_t data[8]) {
  uint8_t frame_number = bms_normalize_frame_number(data[0]);
  if (frame_number == 0xFFU) return;

  for (uint8_t i = 0; i < 7U; i++) {
    if (data[1U + i] == 0xFFU) continue;

    uint8_t sensor_index = frame_number * 7U + i;
    if (sensor_index >= DALY_MAX_TEMPS) continue;

    bms.cell_temp[sensor_index] = (int8_t)((int16_t)data[1U + i] - 40);
    if (sensor_index + 1U > bms.cell_temp_count) {
      bms.cell_temp_count = sensor_index + 1U;
    }
  }
}

static void bms_parse_98(const uint8_t data[8]) {
  bms.any_fault = false;
  for (uint8_t i = 0; i < 8U; i++) {
    bms.fault[i] = data[i];
    if (i < 7U && data[i] != 0U) bms.any_fault = true;
  }
}

static void bms_parse_frame(uint8_t data_id, const uint8_t data[8], TickType_t now) {
  switch (data_id) {
    case 0x90: bms_parse_90(data, now); break;
    case 0x91: bms_parse_91(data); break;
    case 0x92: bms_parse_92(data); break;
    case 0x93: bms_parse_93(data); break;
    case 0x94: bms_parse_94(data); break;
    case 0x95: bms_parse_95(data); break;
    case 0x96: bms_parse_96(data); break;
    case 0x98: bms_parse_98(data); break;
    default: return;
  }
  debug_monitor_publish_bms(&bms);
}

static bool bms_extract_data_id(const twai_message_t *msg, uint8_t *data_id) {
  if (!msg->extd || msg->rtr || msg->data_length_code < 8U) return false;

  uint8_t candidate = (uint8_t)((msg->identifier >> 16) & 0xFFU);
  if (msg->identifier != CAN_DALY_RX_ID(candidate)) return false;

  for (size_t i = 0; i < NUM_DALY_DATA_IDS; i++) {
    if (candidate == bms_data_ids[i]) {
      *data_id = candidate;
      return true;
    }
  }
  return false;
}

static void bms_finish_request(TickType_t now) {
  uint8_t index   = bms_poll.data_index;
  uint8_t data_id = bms_data_ids[index];

  if (data_id == 0x95U || data_id == 0x96U) {
    bms.data_valid[index] = bms_poll.expected_frames > 0U &&
                            (bms_poll.seen_frames & bms_expected_frame_mask(bms_poll.expected_frames)) ==
                              bms_expected_frame_mask(bms_poll.expected_frames);
  } else {
    bms.data_valid[index] = bms_poll.got_response;
  }

  if (!bms.data_valid[index]) {
    ESP_LOGW("CAN", "Daly response missing/incomplete: data_id=0x%02X", (unsigned)data_id);
  }

  if (index + 1U < NUM_DALY_DATA_IDS) {
    bms_poll.data_index       = index + 1U;
    bms_poll.next_action_tick = now + pdMS_TO_TICKS(BMS_REQ_INTERVAL_MS);
  } else {
    bms.cycle_count++;
    bms.last_cycle_tick       = now;
    bms_poll.data_index       = 0;
    bms_poll.next_action_tick = now + pdMS_TO_TICKS(BMS_CYCLE_INTERVAL_MS);
  }

  bms_poll.phase = BMS_POLL_SEND;
  debug_monitor_publish_bms(&bms);
}

static void bms_prepare_request(uint8_t data_id) {
  bms_poll.expected_frames = 0;
  bms_poll.seen_frames     = 0;
  bms_poll.got_response    = false;

  if (bms_poll.data_index == 0U) {
    memset(bms.data_valid, 0, sizeof(bms.data_valid));
  }

  if (data_id == 0x95U) {
    memset(bms.cell_voltage, 0, sizeof(bms.cell_voltage));
    bms.cell_voltage_count = 0;
    if (bms.data_valid[DALY_IDX_94] && bms.cell_string_count > 0U &&
        bms.cell_string_count <= DALY_MAX_CELLS) {
      bms_poll.expected_frames = (bms.cell_string_count + 2U) / 3U;
    }
  } else if (data_id == 0x96U) {
    memset(bms.cell_temp, 0, sizeof(bms.cell_temp));
    bms.cell_temp_count = 0;
    if (bms.data_valid[DALY_IDX_94] && bms.temp_sensor_count > 0U &&
        bms.temp_sensor_count <= DALY_MAX_TEMPS) {
      bms_poll.expected_frames = (bms.temp_sensor_count + 6U) / 7U;
    }
  }
}

static esp_err_t bms_transmit_request(uint8_t data_id) {
  twai_message_t request = {
    .identifier       = CAN_DALY_TX_ID(data_id),
    .extd             = 1,
    .rtr              = 0,
    .data_length_code = 8,
    .data             = { 0 },
  };
  return twai_transmit(&request, 0);
}

static void bms_poll_step(TickType_t now) {
  if (bms_poll.phase == BMS_POLL_SEND) {
    if (!can_tick_reached(now, bms_poll.next_action_tick)) return;

    uint8_t data_id = bms_data_ids[bms_poll.data_index];
    bms_prepare_request(data_id);
    esp_err_t err = bms_transmit_request(data_id);
    if (err != ESP_OK) {
      bms_poll.tx_failures++;
      if (!bms_poll.tx_failure_reported && bms_poll.tx_failures >= SENSOR_FAILURE_THRESHOLD) {
        ESP_LOGW("CAN", "Daly request transmit failed: data_id=0x%02X error=%s (0x%X)",
                 (unsigned)data_id, esp_err_to_name(err), (unsigned)err);
        bms_poll.tx_failure_reported = true;
      }
      bms_finish_request(now);
      return;
    }

    if (bms_poll.tx_failure_reported) {
      ESP_LOGI("CAN", "Daly request transmission recovered");
    }
    bms_poll.tx_failures         = 0;
    bms_poll.tx_failure_reported = false;
    bms_poll.phase               = BMS_POLL_WAIT_RESPONSE;
    bms_poll.response_deadline   = now + pdMS_TO_TICKS(
      (data_id == 0x95U || data_id == 0x96U) ? BMS_MULTIFRAME_TIMEOUT_MS : BMS_RESPONSE_TIMEOUT_MS);
    bms_poll.last_frame_tick = now;
    return;
  }

  uint8_t data_id = bms_data_ids[bms_poll.data_index];
  bool unknown_multiframe_done = (data_id == 0x95U || data_id == 0x96U) &&
                                 bms_poll.expected_frames == 0U && bms_poll.got_response &&
                                 can_tick_reached(now, bms_poll.last_frame_tick +
                                                        pdMS_TO_TICKS(BMS_INTERFRAME_GAP_MS));
  if (unknown_multiframe_done || can_tick_reached(now, bms_poll.response_deadline)) {
    bms_finish_request(now);
  }
}

static void bms_handle_frame(uint8_t data_id, const uint8_t data[8], TickType_t now) {
  if (bms_poll.phase != BMS_POLL_WAIT_RESPONSE ||
      data_id != bms_data_ids[bms_poll.data_index]) {
    return;
  }

  bms_parse_frame(data_id, data, now);
  bms_poll.got_response   = true;
  bms_poll.last_frame_tick = now;

  if (data_id != 0x95U && data_id != 0x96U) {
    bms_finish_request(now);
    return;
  }

  uint8_t frame_number = bms_normalize_frame_number(data[0]);
  if (frame_number != 0xFFU && frame_number < 16U) {
    bms_poll.seen_frames |= (uint16_t)1U << frame_number;
  }

  if (bms_poll.expected_frames > 0U) {
    uint16_t full_mask = bms_expected_frame_mask(bms_poll.expected_frames);
    if ((bms_poll.seen_frames & full_mask) == full_mask) bms_finish_request(now);
  }
}

static void bms_poll_reset(TickType_t now) {
  memset(bms.data_valid, 0, sizeof(bms.data_valid));
  bms_poll = (bms_poll_state_t) {
    .phase            = BMS_POLL_SEND,
    .data_index       = 0,
    .next_action_tick = now,
  };
  display_can.bms_soc_valid = 0;
  debug_monitor_publish_bms(&bms);
}

/*******************************************************************************
 * CAN traffic monitor task
 ******************************************************************************/
void task_can(void *pvParameters) {
  (void)pvParameters;

  bool driver_online         = false;
  bool fault_reported        = false;
  unsigned rx_failures       = 0;
  unsigned alert_failures    = 0;
  TickType_t last_alert_tick = xTaskGetTickCount();

  while (true) {
    if (!driver_online) {
      esp_err_t err = can_driver_start();
      if (err != ESP_OK) {
        CLEAR_FATAL(&logbuf.run, CAN);
        SET_ERROR(&logbuf.run, CAN);
        if (!fault_reported) {
          ESP_LOGW("CAN", "TWAI driver start failed (%s); retrying every %d ms", esp_err_to_name(err),
            (int)SENSOR_RETRY_MS);
          SYSLOG("SNS:CAN:ERR");
          fault_reported = true;
        }
        vTaskDelay(pdMS_TO_TICKS(SENSOR_RETRY_MS));
        continue;
      }

      driver_online     = true;
      rx_failures       = 0;
      alert_failures    = 0;
      last_alert_tick   = xTaskGetTickCount();
      bms_poll_reset(last_alert_tick);
      CLEAR_ALL(&logbuf.run, CAN);
      INFO(CAN, "TWAI driver online");
      SYSLOG("SNS:CAN:OK");
      fault_reported = false;
    }

    bms_poll_step(xTaskGetTickCount());

    // Keep the receive wait short enough for the 120 ms Daly polling state
    // machine while still blocking efficiently when the bus is idle.
    twai_message_t msg = { 0 };
    esp_err_t rx_err   = twai_receive(&msg, pdMS_TO_TICKS(20));

    if (rx_err == ESP_OK) {
      rx_failures = 0;

      // Print the untouched TWAI payload before it is copied into the SD log.
      // Zero-fill short frames so all eight displayed bytes are safe to read.
      if (msg.extd && !msg.rtr && msg.identifier == CAN_EZ_ID2) {
        uint8_t raw_data[8] = { 0 };
        uint8_t raw_len     = msg.data_length_code;
        if (raw_len > sizeof(raw_data)) raw_len = sizeof(raw_data);
        memcpy(raw_data, msg.data, raw_len);

        ESP_LOGI("CAN_RAW",
          "ID=%08lX DLC=%u DATA=%02X %02X %02X %02X %02X %02X %02X %02X",
          (unsigned long)msg.identifier, (unsigned)msg.data_length_code,
          (unsigned)raw_data[0], (unsigned)raw_data[1],
          (unsigned)raw_data[2], (unsigned)raw_data[3],
          (unsigned)raw_data[4], (unsigned)raw_data[5],
          (unsigned)raw_data[6], (unsigned)raw_data[7]);
      }

      log_t log         = { 0 };
      uint8_t len       = msg.data_length_code;
      if (len > sizeof(log.payload.can.data)) len = sizeof(log.payload.can.data);

      log.payload.can.id           = msg.identifier;
      log.payload.can.extended     = msg.extd;
      log.payload.can.remote       = msg.rtr;
      log.payload.can.len          = len;

      // Keep the stored DLC and payload length consistent. Short frames retain
      // zeroes in the unused bytes without changing the 24-byte log format.
      memset(log.payload.can.data, 0, sizeof(log.payload.can.data));
      memcpy(log.payload.can.data, msg.data, len);
      LOG(LOG_TYPE_CAN, &log);

      TickType_t now = xTaskGetTickCount();

      // EZkontrol METER frames are 29-bit extended data frames. Pair Msg1 and
      // Msg2 only when both belong to the same 100 ms broadcast cycle.
      if (msg.extd && !msg.rtr && msg.identifier == CAN_EZ_ID1 && msg.data_length_code >= 8) {
        motor_parse_msg1(msg.data, now);
      } else if (msg.extd && !msg.rtr && msg.identifier == CAN_EZ_ID2 && msg.data_length_code >= 8) {
        motor_parse_msg2(msg.data, now);
      } else {
        uint8_t data_id;
        if (bms_extract_data_id(&msg, &data_id)) {
          bms_handle_frame(data_id, msg.data, now);
        }
      }
    } else if (rx_err == ESP_ERR_TIMEOUT) {
      // An idle CAN bus is healthy and breaks a sequence of driver API errors.
      rx_failures = 0;
    } else if (++rx_failures >= SENSOR_FAILURE_THRESHOLD) {
      CLEAR_FATAL(&logbuf.run, CAN);
      SET_ERROR(&logbuf.run, CAN);
      if (!fault_reported) {
        ESP_LOGW("CAN", "TWAI receive failed (%s); restarting driver", esp_err_to_name(rx_err));
        SYSLOG("SNS:CAN:ERR");
        fault_reported = true;
      }
      motor_rx_seen = false;
      motor_invalidate();
      can_driver_stop();
      driver_online = false;
      vTaskDelay(pdMS_TO_TICKS(SENSOR_RETRY_MS));
      continue;
    }

    // Motor data validity is independent from the TWAI driver health state.
    TickType_t now = xTaskGetTickCount();
    motor_check_timeout(now);

    // check CAN alerts at most every 100 ms

    if (now - last_alert_tick < pdMS_TO_TICKS(100)) {
      continue;
    }

    last_alert_tick = now;

    uint32_t alerts = 0;
    esp_err_t alert_err = twai_read_alerts(&alerts, 0);

    if (alert_err != ESP_OK && alert_err != ESP_ERR_TIMEOUT) {
      if (++alert_failures < SENSOR_FAILURE_THRESHOLD) continue;

      CLEAR_FATAL(&logbuf.run, CAN);
      SET_ERROR(&logbuf.run, CAN);
      if (!fault_reported) {
        ESP_LOGW("CAN", "TWAI alert read failed (%s); restarting driver", esp_err_to_name(alert_err));
        SYSLOG("SNS:CAN:ERR");
        fault_reported = true;
      }
      motor_rx_seen = false;
      motor_invalidate();
      can_driver_stop();
      driver_online = false;
      vTaskDelay(pdMS_TO_TICKS(SENSOR_RETRY_MS));
      continue;
    }
    alert_failures = 0;

    if (alerts & CAN_ALERT_ERROR) {
      SET_ERROR(&logbuf.run, CAN);
      if (!fault_reported) {
        ESP_LOGW("CAN", "TWAI warning state: 0x%08lX", alerts);
        SYSLOG("SNS:CAN:ERR");
        fault_reported = true;
      }

      if (alerts & TWAI_ALERT_BUS_OFF) {
        motor_rx_seen = false;
        motor_invalidate();
        can_driver_stop();
        driver_online = false;
        vTaskDelay(pdMS_TO_TICKS(SENSOR_RETRY_MS));
        continue;
      }
    } else if (fault_reported && ((alerts & CAN_ALERT_RECOVERED) || can_driver_status_healthy())) {
      CLEAR_ALL(&logbuf.run, CAN);
      INFO(CAN, "TWAI warning state cleared");
      SYSLOG("SNS:CAN:OK");
      fault_reported = false;
    }
  }
}
