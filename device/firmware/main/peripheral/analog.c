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

#define VOLT(v) ((int)(((v) * 2048) >> 15))

static i2c_master_bus_handle_t i2c1;

esp_err_t adc_start(i2c_master_dev_handle_t adc, uint8_t ch) {
  uint8_t tx[3] = { ADS1115_CONFIG_REG_ADDR, ADS1115_CONFIG(ch), ADS1115_CONFIG_L };
  return i2c_master_transmit(adc, tx, sizeof(tx), I2C_TIMEOUT_MS);
}

esp_err_t adc_read(i2c_master_dev_handle_t adc, int16_t *v) {
  uint8_t tx_conv = ADS1115_CONVERSION_REG_ADDR;
  uint8_t rx[2]   = { 0 };
  esp_err_t ret = i2c_master_transmit_receive(adc, &tx_conv, sizeof(tx_conv), rx, sizeof(rx), I2C_TIMEOUT_MS);
  *v = ((int16_t)rx[0] << 8) | rx[1];
  return ret;
}

/*******************************************************************************
 * Analog channel and temperature sensor monitor task
 ******************************************************************************/
void task_analog(void *pvParameters) {
  // initialize i2c bus and adc
  i2c_master_bus_config_t i2c_config = {
    .clk_source                   = I2C_CLK_SRC_DEFAULT,
    .i2c_port                     = I2C_NUM_1,
    .scl_io_num                   = GPIO_NUM_42,
    .sda_io_num                   = GPIO_NUM_47,
    .glitch_ignore_cnt            = 7,
    .flags.enable_internal_pullup = false,
  };

  if (i2c_new_master_bus(&i2c_config, &i2c1) != ESP_OK) {
    ERROR_SYSLOG(&init, ANALOG, "I2C init failure", "ANL_INIT_FAIL");
    vTaskDelete(NULL);
  }

  i2c_master_dev_handle_t adc1;
  i2c_device_config_t adc1_cfg = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address  = 0x48,  // 모듈1: ADDR→GND
    .scl_speed_hz    = 400000,
  };

  i2c_master_dev_handle_t adc2;
  i2c_device_config_t adc2_cfg = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address  = 0x49,  // 모듈2: ADDR→VDD
    .scl_speed_hz    = 400000,
  };

  esp_err_t ret = i2c_master_bus_add_device(i2c1, &adc1_cfg, &adc1);
  ret |= i2c_master_bus_add_device(i2c1, &adc2_cfg, &adc2);

  if (ret != ESP_OK) {
    i2c_del_master_bus(i2c1);
    ERROR_SYSLOG(&init, ANALOG, "device init failure", "ANL_DEV_FAIL");
    vTaskDelete(NULL);
  }

  if (IS_OK(&init, ANALOG)) {
    CLEAR_ALL(&logbuf.run, ANALOG);
    SYSLOG("ANL_RDY");
  } else {
    COPY_STATE(&logbuf.run, &init, ANALOG);
  }

  TickType_t xLastWakeTime = xTaskGetTickCount();

  log_t analog;

  while (true) {
    esp_err_t err = ESP_OK;

    // 4 paired cycles: adc1 + adc2 simultaneous conversion
    uint8_t  ch[]   = { MUX_AIN0, MUX_AIN1, MUX_AIN2, MUX_AIN3 };
    int16_t *dst1[] = { &analog.payload.analog.ain1, &analog.payload.analog.ain2,
                        &analog.payload.analog.ain3, &analog.payload.analog.ain4 };
    int16_t *dst2[] = { &analog.payload.analog.ain5, &analog.payload.analog.ain6,
                        &analog.payload.analog.ain7, &analog.payload.analog.ain8 };

    for (int i = 0; i < 4; i++) {
      int cnt = 0;
      esp_err_t ret;
      do {
        if (cnt) i2c_master_bus_reset(i2c1);
        ret  = adc_start(adc1, ch[i]);
        ret |= adc_start(adc2, ch[i]);
        // 860SPS 변환에 1.163ms 필요 — busy-wait 대신 sleep으로 CPU를 양보하고,
        // 스케줄러가 일찍 깨운 경우에만 부족분을 짧게 spin해서 채운다
        int64_t t0 = esp_timer_get_time();
        vTaskDelay(pdMS_TO_TICKS(2));
        int64_t remain = 1400 - (esp_timer_get_time() - t0);
        if (remain > 0) esp_rom_delay_us((uint32_t)remain);
        ret |= adc_read(adc1, dst1[i]);
        ret |= adc_read(adc2, dst2[i]);
        cnt++;
      } while (ret != ESP_OK && cnt < 2);
      err |= ret;
    }

    if (err == ESP_OK) {
      LOG(LOG_TYPE_ANALOG, &analog);

      if (IS_ERROR(&logbuf.run, ANALOG)) {
        CLEAR_ERROR(&logbuf.run, ANALOG);
      }
    } else {
      ERROR_SYSLOG(&logbuf.run, ANALOG, "ADC read failure", "ADC_READ_FAIL");
    }

    xTaskDelayUntil(&xLastWakeTime, TASK_INTERVAL_ANALOG);
  }
}
