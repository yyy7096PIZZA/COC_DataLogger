#include "main.h"

#include "driver/twai.h"

#define CAN_ALERT_ERROR                                                                                       \
  (TWAI_ALERT_RECOVERY_IN_PROGRESS | TWAI_ALERT_ARB_LOST | TWAI_ALERT_ABOVE_ERR_WARN | TWAI_ALERT_BUS_ERROR | \
    TWAI_ALERT_TX_FAILED | TWAI_ALERT_RX_QUEUE_FULL | TWAI_ALERT_ERR_PASS | TWAI_ALERT_BUS_OFF |             \
    TWAI_ALERT_RX_FIFO_OVERRUN)

#define CAN_ALERT_RECOVERED (TWAI_ALERT_ERR_ACTIVE | TWAI_ALERT_BUS_RECOVERED)
#define CAN_ALERT_ENABLED   (CAN_ALERT_ERROR | CAN_ALERT_RECOVERED)
#define CAN_ERROR_WARNING_THRESHOLD 96U

static inline twai_timing_config_t select_baud(uint8_t can_bps) {
  switch (can_bps) {
    case CAN_BPS_1K:
      return (twai_timing_config_t)TWAI_TIMING_CONFIG_1KBITS();
    case CAN_BPS_5K:
      return (twai_timing_config_t)TWAI_TIMING_CONFIG_5KBITS();
    case CAN_BPS_10K:
      return (twai_timing_config_t)TWAI_TIMING_CONFIG_10KBITS();
    case CAN_BPS_12_5K:
      return (twai_timing_config_t)TWAI_TIMING_CONFIG_12_5KBITS();
    case CAN_BPS_16K:
      return (twai_timing_config_t)TWAI_TIMING_CONFIG_16KBITS();
    case CAN_BPS_20K:
      return (twai_timing_config_t)TWAI_TIMING_CONFIG_20KBITS();
    case CAN_BPS_25K:
      return (twai_timing_config_t)TWAI_TIMING_CONFIG_25KBITS();
    case CAN_BPS_50K:
      return (twai_timing_config_t)TWAI_TIMING_CONFIG_50KBITS();
    case CAN_BPS_100K:
      return (twai_timing_config_t)TWAI_TIMING_CONFIG_100KBITS();
    case CAN_BPS_125K:
      return (twai_timing_config_t)TWAI_TIMING_CONFIG_125KBITS();
    case CAN_BPS_250K:
      return (twai_timing_config_t)TWAI_TIMING_CONFIG_250KBITS();
    case CAN_BPS_500K:
      return (twai_timing_config_t)TWAI_TIMING_CONFIG_500KBITS();
    case CAN_BPS_800K:
      return (twai_timing_config_t)TWAI_TIMING_CONFIG_800KBITS();
    case CAN_BPS_1M:
      return (twai_timing_config_t)TWAI_TIMING_CONFIG_1MBITS();
    default:
      return (twai_timing_config_t)TWAI_TIMING_CONFIG_500KBITS();
  }
}

static esp_err_t can_driver_start(void) {
  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_15, GPIO_NUM_16, TWAI_MODE_NORMAL);
  g_config.rx_queue_len          = 1024;
  g_config.alerts_enabled        = CAN_ALERT_ENABLED;
  twai_timing_config_t t_config  = select_baud(storage.can.bps);
  twai_filter_config_t f_config  = {
     .single_filter   = true,
     .acceptance_code = storage.can.filter,
     .acceptance_mask = storage.can.mask,
  };

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
      CLEAR_ALL(&logbuf.run, CAN);
      INFO(CAN, "TWAI driver online");
      SYSLOG("SNS:CAN:OK");
      fault_reported = false;
    }

    // block inside the driver until a frame arrives; the timeout only bounds
    // how long the periodic alert check below can be deferred on an idle bus
    twai_message_t msg = { 0 };
    esp_err_t rx_err   = twai_receive(&msg, pdMS_TO_TICKS(100));

    if (rx_err == ESP_OK) {
      rx_failures = 0;
      log_t log         = { 0 };
      log.payload.can.id           = msg.identifier;
      log.payload.can.extended     = msg.extd;
      log.payload.can.remote       = msg.rtr;
      log.payload.can.len          = msg.data_length_code;
      log.payload.can._reserved[0] = 0;

      // 비표준 노드는 DLC > 8을 보낼 수 있으므로 복사는 8바이트로 제한하고,
      // 짧은 프레임의 나머지 바이트는 0으로 채워 스택 쓰레기가 기록되지 않게 한다
      memset(log.payload.can.data, 0, sizeof(log.payload.can.data));
      memcpy(log.payload.can.data, msg.data,
        msg.data_length_code > sizeof(log.payload.can.data) ? sizeof(log.payload.can.data) : msg.data_length_code);
      LOG(LOG_TYPE_CAN, &log);

      // update display snapshot
      if (msg.identifier == CAN_EZ_ID1 && msg.data_length_code >= 8) {
        uint16_t motor_rpm_raw  = (uint16_t)msg.data[6] | ((uint16_t)msg.data[7] << 8);
        display_can.ez_rpm_raw = motor_rpm_raw;
        display_can.valid      = 1;
        display_can.last_tick  = xTaskGetTickCount();
        debug_monitor_publish_can_rpm(motor_rpm_raw);
      } else if (msg.identifier == CAN_DALY_ID90 && msg.data_length_code >= 8) {
        uint16_t bms_soc_raw     = ((uint16_t)msg.data[6] << 8) | (uint16_t)msg.data[7];
        display_can.bms_soc_raw = bms_soc_raw;
        display_can.valid       = 1;
        display_can.last_tick   = xTaskGetTickCount();
        debug_monitor_publish_can_soc(bms_soc_raw);
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
      can_driver_stop();
      driver_online = false;
      vTaskDelay(pdMS_TO_TICKS(SENSOR_RETRY_MS));
      continue;
    }

    // check CAN alerts at most every 100 ms
    TickType_t now = xTaskGetTickCount();

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
