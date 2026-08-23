#include "main.h"

#include "driver/i2c_master.h"

/*******************************************************************************
 * ADS1115 외장 ADC — 파란색 모듈보드 2개 (I2C1 버스: GPIO47=SDA / GPIO42=SCL 공유)
 *
 * 모듈도 동일한 ADS1115 칩이라 레지스터/MUX/변환 시퀀스는 베어 IC와 동일하다.
 * 모듈 헤더 배선 (★ 전원은 반드시 3.3V — 5V 금지: 모듈 온보드 풀업이 SDA/SCL을
 * 5V로 끌어올려 ESP32-S3 입력을 손상시킨다):
 *   VDD→3.3V   GND→GND   SCL→GPIO42   SDA→GPIO47   ALRT→미연결(ALERT/RDY 미사용)
 *   모듈1: ADDR→GND ⇒ I2C 0x48 (adc1)
 *   모듈2: ADDR→VDD ⇒ I2C 0x49 (adc2)
 *
 * 입력 채널(모듈 헤더 A0~A3) → 로그 필드:
 *   모듈1(0x48): A0→ain1  A1→ain2  A2→ain3  A3→ain4
 *   모듈2(0x49): A0→ain5  A1→ain6  A2→ain7  A3→ain8
 ******************************************************************************/

#define ADS1115_CONVERSION_REG_ADDR 0x00
#define ADS1115_CONFIG_REG_ADDR 0x01

#define MUX_AIN0 (0b100 << 4)  // 모듈 헤더 A0
#define MUX_AIN1 (0b101 << 4)  // 모듈 헤더 A1
#define MUX_AIN2 (0b110 << 4)  // 모듈 헤더 A2
#define MUX_AIN3 (0b111 << 4)  // 모듈 헤더 A3
#define ADS1115_OS (1 << 7)

#define ADS1115_CONFIG_H 0x03  // +-4.096V FSR, single-shot mode
#define ADS1115_CONFIG_L 0xE3  // 860SPS (1.16ms per conversion)
#define ADS1115_CONFIG(channel) (ADS1115_CONFIG_H | ADS1115_OS | channel)

#define ADS1115_ADDRESS_48 0x48
#define ADS1115_ADDRESS_49 0x49

#define ADS48_CHANNEL_MASK ((uint8_t)((SENSOR_ENABLE_ADS48) ? (SENSOR_ANALOG_MASK & 0x0FU) : 0U))
#define ADS49_CHANNEL_MASK ((uint8_t)((SENSOR_ENABLE_ADS49) ? ((SENSOR_ANALOG_MASK >> 4) & 0x0FU) : 0U))
#define ADS_RETRY_TICKS pdMS_TO_TICKS(SENSOR_RETRY_MS)

typedef enum {
  ADS_STATE_DISABLED,
  ADS_STATE_ACTIVE,
  ADS_STATE_BACKOFF,
} ads_state_t;

typedef struct {
  uint8_t address;
  uint8_t channel_mask;  // local A0-A3 mask
  const char *name;
  const char *event_miss;
  const char *event_error;
  const char *event_ok;
  i2c_master_dev_handle_t device;
  ads_state_t state;
  uint32_t consecutive_failures;
  TickType_t retry_at;
} ads_module_t;

static i2c_master_bus_handle_t i2c1;

static esp_err_t adc_start(i2c_master_dev_handle_t adc, uint8_t ch) {
  uint8_t tx[3] = { ADS1115_CONFIG_REG_ADDR, ADS1115_CONFIG(ch), ADS1115_CONFIG_L };
  return i2c_master_transmit(adc, tx, sizeof(tx), I2C_TIMEOUT_MS);
}

static esp_err_t adc_read(i2c_master_dev_handle_t adc, int16_t *v) {
  uint8_t tx_conv = ADS1115_CONVERSION_REG_ADDR;
  uint8_t rx[2]   = { 0 };
  esp_err_t ret = i2c_master_transmit_receive(adc, &tx_conv, sizeof(tx_conv), rx, sizeof(rx), I2C_TIMEOUT_MS);
  if (ret == ESP_OK) *v = ((int16_t)rx[0] << 8) | rx[1];
  return ret;
}

static bool retry_time_reached(TickType_t now, TickType_t retry_at) {
  return (int32_t)(now - retry_at) >= 0;
}

static esp_err_t ads_attach(ads_module_t *module) {
  i2c_device_config_t config = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address  = module->address,
    .scl_speed_hz    = 400000,
  };

  return i2c_master_bus_add_device(i2c1, &config, &module->device);
}

static void ads_detach(ads_module_t *module) {
  if (module->device == NULL) return;
  if (i2c_master_bus_rm_device(module->device) == ESP_OK) module->device = NULL;
}

// Probe first so an absent optional module never enters the high-rate read path.
// A recovered module gets a fresh device handle before sampling resumes.
static esp_err_t ads_probe_and_attach(ads_module_t *module) {
  if (module->device != NULL) {
    if (i2c_master_bus_rm_device(module->device) != ESP_OK) return ESP_FAIL;
    module->device = NULL;
  }

  esp_err_t ret = i2c_master_probe(i2c1, module->address, I2C_TIMEOUT_MS);
  if (ret != ESP_OK) return ret;
  return ads_attach(module);
}

static void ads_enter_backoff(ads_module_t *module, const char *event, const char *reason) {
  ads_detach(module);
  module->state                = ADS_STATE_BACKOFF;
  module->consecutive_failures = 0;
  module->retry_at             = xTaskGetTickCount() + ADS_RETRY_TICKS;

  SYSLOG(event);
  SET_ERROR(&logbuf.run, ANALOG);
  ESP_LOGW(components[ANALOG], "%s %s", module->name, reason);
}

static bool all_enabled_modules_active(const ads_module_t modules[2]) {
  for (int i = 0; i < 2; i++) {
    if (modules[i].state != ADS_STATE_DISABLED && modules[i].state != ADS_STATE_ACTIVE) return false;
  }
  return true;
}

static void ads_try_recover(ads_module_t *module, ads_module_t modules[2], TickType_t now) {
  if (module->state != ADS_STATE_BACKOFF || !retry_time_reached(now, module->retry_at)) return;

  if (ads_probe_and_attach(module) != ESP_OK) {
    // Missing hardware is expected for an optional sensor. Keep retrying without
    // repeating either the console warning or the SYSTEM event.
    module->retry_at = now + ADS_RETRY_TICKS;
    return;
  }

  module->state                = ADS_STATE_ACTIVE;
  module->consecutive_failures = 0;
  SYSLOG(module->event_ok);
  ESP_LOGI(components[ANALOG], "%s recovered", module->name);

  if (all_enabled_modules_active(modules) && IS_ERROR(&logbuf.run, ANALOG)) {
    CLEAR_ERROR(&logbuf.run, ANALOG);
  }
}

static void ads_note_cycle_result(ads_module_t *module, bool success) {
  if (module->state != ADS_STATE_ACTIVE) return;

  if (success) {
    module->consecutive_failures = 0;
    return;
  }

  if (++module->consecutive_failures >= SENSOR_FAILURE_THRESHOLD) {
    ads_enter_backoff(module, module->event_error, "read failure; entering retry backoff");
  }
}

static void ads_wait_for_conversion(void) {
  // 860SPS conversion requires 1.163ms. Yield first, then spin only for a
  // scheduler wake-up that was earlier than the conservative 1.4ms margin.
  int64_t t0 = esp_timer_get_time();
  vTaskDelay(pdMS_TO_TICKS(2));
  int64_t remain = 1400 - (esp_timer_get_time() - t0);
  if (remain > 0) esp_rom_delay_us((uint32_t)remain);
}

/*******************************************************************************
 * Analog channel and temperature sensor monitor task
 ******************************************************************************/
void task_analog(void *pvParameters) {
  (void)pvParameters;

  ads_module_t modules[2] = {
    {
      .address       = ADS1115_ADDRESS_48,
      .channel_mask  = ADS48_CHANNEL_MASK,
      .name          = "ADS48",
      .event_miss    = "SNS:ADS48:MISS",
      .event_error   = "SNS:ADS48:ERR",
      .event_ok      = "SNS:ADS48:OK",
      .state         = ADS_STATE_DISABLED,
    },
    {
      .address       = ADS1115_ADDRESS_49,
      .channel_mask  = ADS49_CHANNEL_MASK,
      .name          = "ADS49",
      .event_miss    = "SNS:ADS49:MISS",
      .event_error   = "SNS:ADS49:ERR",
      .event_ok      = "SNS:ADS49:OK",
      .state         = ADS_STATE_DISABLED,
    },
  };

  // main.c normally omits this task when no analog channel is enabled. Keep a
  // local guard as well so an accidental task creation does not touch I2C1.
  if (ADS48_CHANNEL_MASK == 0 && ADS49_CHANNEL_MASK == 0) {
    CLEAR_ALL(&logbuf.run, ANALOG);
    vTaskDelete(NULL);
    return;
  }

  // initialize i2c bus and adc
  i2c_master_bus_config_t i2c_config = {
    .clk_source                   = I2C_CLK_SRC_DEFAULT,
    .i2c_port                     = I2C_NUM_1,
    .scl_io_num                   = GPIO_NUM_42,
    .sda_io_num                   = GPIO_NUM_47,
    .glitch_ignore_cnt            = 7,
    // Keep the idle bus at a defined level even when optional modules (and
    // their onboard pull-ups) are unplugged. Installed modules still provide
    // the stronger pull-ups needed for reliable 400kHz transfers.
    .flags.enable_internal_pullup = true,
  };

  if (i2c_new_master_bus(&i2c_config, &i2c1) != ESP_OK) {
    for (int i = 0; i < 2; i++) {
      if (modules[i].channel_mask != 0) SYSLOG(modules[i].event_error);
    }
    ERROR_SYSLOG(&init, ANALOG, "I2C init failure", "ANL_INIT_FAIL");
    COPY_STATE(&logbuf.run, &init, ANALOG);
    vTaskDelete(NULL);
    return;
  }

  if (IS_OK(&init, ANALOG)) {
    CLEAR_ALL(&logbuf.run, ANALOG);
  } else {
    COPY_STATE(&logbuf.run, &init, ANALOG);
  }

  // Probe and attach only modules that own at least one enabled channel. A
  // missing ADS48 does not prevent ADS49 (or vice versa) from being sampled.
  for (int i = 0; i < 2; i++) {
    ads_module_t *module = &modules[i];
    if (module->channel_mask == 0) continue;

    esp_err_t probe = i2c_master_probe(i2c1, module->address, I2C_TIMEOUT_MS);
    if (probe != ESP_OK) {
      ads_enter_backoff(module, module->event_miss, "not detected; entering retry backoff");
      continue;
    }

    if (ads_attach(module) != ESP_OK) {
      ads_enter_backoff(module, module->event_error, "device init failure; entering retry backoff");
      continue;
    }

    module->state = ADS_STATE_ACTIVE;
    SYSLOG(module->event_ok);
    ESP_LOGI(components[ANALOG], "%s ready", module->name);
  }

  if (all_enabled_modules_active(modules)) SYSLOG("ANL_RDY");

  TickType_t xLastWakeTime = xTaskGetTickCount();
  static const uint8_t mux[4] = { MUX_AIN0, MUX_AIN1, MUX_AIN2, MUX_AIN3 };

  while (true) {
    TickType_t now = xTaskGetTickCount();
    ads_try_recover(&modules[0], modules, now);
    ads_try_recover(&modules[1], modules, now);

    log_t analog = { 0 };
    int16_t *destinations[2][4] = {
      {
        &analog.payload.analog.ain1,
        &analog.payload.analog.ain2,
        &analog.payload.analog.ain3,
        &analog.payload.analog.ain4,
      },
      {
        &analog.payload.analog.ain5,
        &analog.payload.analog.ain6,
        &analog.payload.analog.ain7,
        &analog.payload.analog.ain8,
      },
    };
    bool module_cycle_ok[2] = { true, true };
    bool any_channel_success = false;

    // Start the matching A0-A3 conversion on both healthy modules before the
    // shared wait. Failed modules/channels are retried independently, so one
    // ADS never causes the other ADS's sample to be discarded.
    for (int channel = 0; channel < 4; channel++) {
      bool needed[2] = {
        modules[0].state == ADS_STATE_ACTIVE && (modules[0].channel_mask & (1U << channel)),
        modules[1].state == ADS_STATE_ACTIVE && (modules[1].channel_mask & (1U << channel)),
      };
      bool complete[2] = { !needed[0], !needed[1] };

      for (int attempt = 0; attempt < 2 && (!complete[0] || !complete[1]); attempt++) {
        if (attempt != 0) (void)i2c_master_bus_reset(i2c1);

        bool started[2] = { false, false };
        for (int module_index = 0; module_index < 2; module_index++) {
          if (!complete[module_index] &&
              adc_start(modules[module_index].device, mux[channel]) == ESP_OK) {
            started[module_index] = true;
          }
        }

        if (started[0] || started[1]) ads_wait_for_conversion();

        for (int module_index = 0; module_index < 2; module_index++) {
          if (started[module_index] &&
              adc_read(modules[module_index].device, destinations[module_index][channel]) == ESP_OK) {
            complete[module_index] = true;
            any_channel_success    = true;
          }
        }
      }

      for (int module_index = 0; module_index < 2; module_index++) {
        if (needed[module_index] && !complete[module_index]) module_cycle_ok[module_index] = false;
      }
    }

    for (int i = 0; i < 2; i++) {
      if (modules[i].state == ADS_STATE_ACTIVE) ads_note_cycle_result(&modules[i], module_cycle_ok[i]);
    }

    if (any_channel_success) {
      debug_monitor_publish_analog(&analog.payload.analog);
      LOG(LOG_TYPE_ANALOG, &analog);
    }

    xTaskDelayUntil(&xLastWakeTime, TASK_INTERVAL_ANALOG);
  }
}
