#include "main.h"

#include "driver/twai.h"

#define CAN_ALERT_ERROR                                                                                        \
  (TWAI_ALERT_ERR_ACTIVE | TWAI_ALERT_RECOVERY_IN_PROGRESS | TWAI_ALERT_ARB_LOST | TWAI_ALERT_ABOVE_ERR_WARN | \
    TWAI_ALERT_BUS_ERROR | TWAI_ALERT_TX_FAILED | TWAI_ALERT_RX_QUEUE_FULL | TWAI_ALERT_ERR_PASS |             \
    TWAI_ALERT_BUS_OFF | TWAI_ALERT_RX_FIFO_OVERRUN)

#define CAN_ALERT_ENABLED (CAN_ALERT_ERROR)

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

/*******************************************************************************
 * CAN traffic monitor task
 ******************************************************************************/
void task_can(void *pvParameters) {
  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_15, GPIO_NUM_16, TWAI_MODE_NORMAL);
  g_config.rx_queue_len          = 1024;
  g_config.alerts_enabled        = CAN_ALERT_ENABLED;
  twai_timing_config_t t_config  = select_baud(storage.can.bps);
  twai_filter_config_t f_config  = {
     .single_filter   = true,
     .acceptance_code = storage.can.filter,
     .acceptance_mask = storage.can.mask,
  };

  if (twai_driver_install(&g_config, &t_config, &f_config) != ESP_OK || twai_start() != ESP_OK) {
    ERROR_SYSLOG(&init, CAN, "driver init failure", "CAN_INIT_FAIL");
  }

  if (IS_OK(&init, CAN)) {
    CLEAR_ALL(&logbuf.run, CAN);
    SYSLOG("CAN_RDY");
  } else {
    COPY_STATE(&logbuf.run, &init, CAN);
  }

  uint32_t prev_alerts       = 0;
  TickType_t last_alert_tick = xTaskGetTickCount();

  while (true) {
    // block inside the driver until a frame arrives; the timeout only bounds
    // how long the periodic alert check below can be deferred on an idle bus
    twai_message_t msg;

    if (twai_receive(&msg, pdMS_TO_TICKS(100)) == ESP_OK) {
      log_t log;
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
        display_can.ez_rpm_raw = (uint16_t)msg.data[6] | ((uint16_t)msg.data[7] << 8);
        display_can.valid      = 1;
        display_can.last_tick  = xTaskGetTickCount();
      } else if (msg.identifier == CAN_DALY_ID90 && msg.data_length_code >= 8) {
        display_can.bms_soc_raw = ((uint16_t)msg.data[6] << 8) | (uint16_t)msg.data[7];
        display_can.valid       = 1;
        display_can.last_tick   = xTaskGetTickCount();
      }
    }

    // check CAN alerts at most every 100 ms
    TickType_t now = xTaskGetTickCount();

    if (now - last_alert_tick < pdMS_TO_TICKS(100)) {
      continue;
    }

    last_alert_tick = now;

    uint32_t alerts = 0;
    twai_read_alerts(&alerts, 0);

    if (alerts) {
      if (alerts != prev_alerts) {
        char buf[sizeof(system_event_t)];
        snprintf(buf, sizeof(buf), "CANALT:%lX", alerts);
        SYSLOG(buf);

        if (alerts & CAN_ALERT_ERROR) {
          SET_ERROR(&logbuf.run, CAN);
        } else {
          CLEAR_ERROR(&logbuf.run, CAN);
        }
      }

      prev_alerts = alerts;
    } else if (prev_alerts) {
      CLEAR_ERROR(&logbuf.run, CAN);
      prev_alerts = 0;
    }
  }
}
