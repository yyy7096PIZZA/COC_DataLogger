#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "main.h"

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

/***** 기어비 및 타이어 설정 *****/
#define DISPLAY_GEAR_RATIO  4.02f
#define DISPLAY_REFRESH_MS   200
#define DISPLAY_PROBE_MS    1000

/***** PCF8574 + HD44780 *****/
#define PCF8574_ADDR  0x27   // A0-A2 모두 GND 기본 주소
#define LCD_BL        0x08   // 백라이트 비트
#define LCD_EN        0x04   // Enable
#define LCD_RS        0x01   // Register Select (0=cmd, 1=data)
#define LCD_ROWS      2
#define LCD_COLS      16

#define DISPLAY_TEXT_COL 1

static const uint8_t ROW_ADDR[LCD_ROWS] = {0x00, 0x40};

static i2c_master_dev_handle_t pcf8574_dev;

/***** shared CAN snapshot (defined here, declared extern in main.h) *****/
volatile display_can_t display_can = {0};

/*******************************************************************************
 * PCF8574 low-level I2C write
 ******************************************************************************/
static esp_err_t pcf_write(uint8_t data) {
  return i2c_master_transmit(pcf8574_dev, &data, 1, I2C_TIMEOUT_MS);
}

static esp_err_t lcd_pulse_en(uint8_t data) {
  esp_err_t ret = pcf_write(data | LCD_EN);
  if (ret != ESP_OK) return ret;

  esp_rom_delay_us(1);
  ret = pcf_write(data & ~LCD_EN);
  if (ret != ESP_OK) return ret;

  esp_rom_delay_us(50);
  return ESP_OK;
}

static esp_err_t lcd_nibble(uint8_t nibble, uint8_t flags) {
  return lcd_pulse_en((uint8_t)((nibble << 4) | LCD_BL | flags));
}

/* one full byte = both nibbles' EN pulses in a single 4-byte I2C transaction
 * (PCF8574 latches its outputs after every received byte). At 100kHz each I2C
 * byte spends ~90us on the wire — longer than the HD44780's 37us execution
 * time — so no explicit delay between pulses is needed. Kept to one character
 * per transaction so the gyroscope sharing I2C0 is never blocked for long. */
static esp_err_t lcd_write8(uint8_t b, uint8_t flags) {
  uint8_t hi     = (uint8_t)((b & 0xF0) | LCD_BL | flags);
  uint8_t lo     = (uint8_t)((b << 4) | LCD_BL | flags);
  uint8_t seq[4] = { hi | LCD_EN, hi, lo | LCD_EN, lo };
  return i2c_master_transmit(pcf8574_dev, seq, sizeof(seq), I2C_TIMEOUT_MS);
}

static esp_err_t lcd_cmd(uint8_t cmd) {
  return lcd_write8(cmd, 0);
}

static esp_err_t lcd_put(uint8_t b) {
  return lcd_write8(b, LCD_RS);
}

static esp_err_t lcd_set_cursor(uint8_t row, uint8_t col) {
  return lcd_cmd(0x80 | (ROW_ADDR[row] + col));
}

/*******************************************************************************
 * LCD initialization (4-bit mode)
 ******************************************************************************/
static esp_err_t lcd_init(void) {
  esp_err_t ret;

  vTaskDelay(pdMS_TO_TICKS(50));

  ret = lcd_nibble(0x03, 0);
  if (ret != ESP_OK) return ret;
  vTaskDelay(pdMS_TO_TICKS(5));

  ret = lcd_nibble(0x03, 0);
  if (ret != ESP_OK) return ret;
  esp_rom_delay_us(150);

  ret = lcd_nibble(0x03, 0);
  if (ret != ESP_OK) return ret;
  esp_rom_delay_us(150);

  ret = lcd_nibble(0x02, 0);    // 4-bit mode
  if (ret != ESP_OK) return ret;

  ret = lcd_cmd(0x28);          // 4-bit, 2-line, 5×8
  if (ret != ESP_OK) return ret;
  ret = lcd_cmd(0x0C);          // display on, cursor/blink off
  if (ret != ESP_OK) return ret;
  ret = lcd_cmd(0x06);          // increment, no display shift
  if (ret != ESP_OK) return ret;
  ret = lcd_cmd(0x01);          // clear display
  if (ret != ESP_OK) return ret;

  vTaskDelay(pdMS_TO_TICKS(2));
  return ESP_OK;
}

/*******************************************************************************
 * framebuffer diff rendering
 * lcd_want = 이번 사이클에 그리고 싶은 화면, lcd_frame = 실제 LCD에 있는 내용.
 * 달라진 글자만 I2C로 전송한다 — 매번 화면 전체를 전송하지 않고 보통 몇 글자로 끝나
 * 같은 I2C0 버스를 쓰는 자이로(100Hz)의 대기 시간이 줄어든다.
 ******************************************************************************/
static uint8_t lcd_want[LCD_ROWS][LCD_COLS];
static uint8_t lcd_frame[LCD_ROWS][LCD_COLS];

static esp_err_t lcd_flush(void) {
  for (int r = 0; r < LCD_ROWS; r++) {
    int c = 0;
    while (c < LCD_COLS) {
      if (lcd_want[r][c] == lcd_frame[r][c]) {
        c++;
        continue;
      }
      // 바뀐 글자 구간: 커서를 한 번만 옮기고 연속으로 쓴다
      esp_err_t ret = lcd_set_cursor(r, c);
      if (ret != ESP_OK) return ret;

      while (c < LCD_COLS && lcd_want[r][c] != lcd_frame[r][c]) {
        ret = lcd_put(lcd_want[r][c]);
        if (ret != ESP_OK) return ret;

        // Only cache bytes that the expander accepted. A failed byte remains
        // dirty and is sent again on the next successful refresh.
        lcd_frame[r][c] = lcd_want[r][c];
        c++;
      }
    }
  }

  return ESP_OK;
}

static void lcd_write_text(uint8_t row, uint8_t col, const char *text) {
  if (row >= LCD_ROWS || col >= LCD_COLS) return;

  size_t len     = strlen(text);
  size_t max_len = LCD_COLS - col;
  if (len > max_len) len = max_len;
  memcpy(&lcd_want[row][col], text, len);
}

static void display_disconnect(void) {
  if (pcf8574_dev == NULL) return;

  i2c_master_bus_rm_device(pcf8574_dev);
  pcf8574_dev = NULL;

  // A failed transaction can leave either the expander or HD44780 part-way
  // through a write. Never trust the framebuffer cache across a reconnect.
  memset(lcd_frame, 0xFF, sizeof(lcd_frame));
}

static esp_err_t display_connect(i2c_master_bus_handle_t i2c0, bool *device_missing) {
  *device_missing = false;

  esp_err_t ret = i2c_master_probe(i2c0, PCF8574_ADDR, I2C_TIMEOUT_MS);
  if (ret != ESP_OK) {
    *device_missing = true;
    return ret;
  }

  i2c_device_config_t dev_cfg = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address  = PCF8574_ADDR,
    .scl_speed_hz    = 100000,
  };

  ret = i2c_master_bus_add_device(i2c0, &dev_cfg, &pcf8574_dev);
  if (ret != ESP_OK) return ret;

  ret = lcd_init();
  if (ret != ESP_OK) {
    display_disconnect();
    return ret;
  }

  // lcd_init() cleared DDRAM, so a blank cache is truthful. Rebuild static
  // content through the normal diff renderer to keep cache and LCD in sync.
  memset(lcd_frame, ' ', sizeof(lcd_frame));
  memset(lcd_want, ' ', sizeof(lcd_want));

  ret = lcd_flush();
  if (ret != ESP_OK) display_disconnect();
  return ret;
}

/*******************************************************************************
 * display refresh task — 200 ms
 *
 * Row 0: SOC text
 * Row 1: vehicle speed text
 ******************************************************************************/
void task_display(void *pvParameters) {
  (void)pvParameters;

  i2c_master_bus_handle_t i2c0;

  // I2C0 is created by the main thread whenever this task is enabled.
  if (i2c_master_get_bus_handle(I2C_NUM_0, &i2c0) != ESP_OK) {
    CLEAR_FATAL(&logbuf.run, DISPLAY);
    SET_ERROR(&logbuf.run, DISPLAY);
    SYSLOG("SNS:LCD:ERR");
    ESP_LOGW("DISPLAY", "I2C0 bus not found");
    vTaskDelete(NULL);
    return;
  }

  bool online                   = false;
  bool outage_reported          = false;
  bool ever_online              = false;
  uint32_t consecutive_failures = 0;
  TickType_t tick               = xTaskGetTickCount();
  bool rpm_received             = false;
  bool soc_received             = false;
  uint16_t cached_rpm_raw       = 0;
  uint16_t cached_soc_raw       = 0;
  TickType_t last_probe_tick    = tick;

  // Clear the startup placeholder. A missing or unusable enabled display is
  // immediately set back to ERROR by the first connection attempt below.
  CLEAR_ALL(&logbuf.run, DISPLAY);

  while (true) {
    if (!online) {
      bool device_missing;
      esp_err_t ret = display_connect(i2c0, &device_missing);
      if (ret != ESP_OK) {
        if (!outage_reported) {
          bool report_error = ever_online || !device_missing;
          SET_ERROR(&logbuf.run, DISPLAY);
          SYSLOG(report_error ? "SNS:LCD:ERR" : "SNS:LCD:MISS");
          if (ever_online) {
            ESP_LOGW("DISPLAY", "device unavailable; retrying every %d ms", (int)SENSOR_RETRY_MS);
          } else if (!device_missing) {
            ESP_LOGW("DISPLAY", "device initialization failed; retrying every %d ms", (int)SENSOR_RETRY_MS);
          } else {
            ESP_LOGW("DISPLAY", "device not found; retrying every %d ms", (int)SENSOR_RETRY_MS);
          }
          outage_reported = true;
        }

        vTaskDelay(pdMS_TO_TICKS(SENSOR_RETRY_MS));
        continue;
      }

      online               = true;
      consecutive_failures = 0;
      CLEAR_ALL(&logbuf.run, DISPLAY);

      if (outage_reported) {
        SYSLOG("SNS:LCD:OK");
        ESP_LOGI("DISPLAY", "device recovered");
        outage_reported = false;
      } else {
        SYSLOG("SNS:LCD:OK");
        ESP_LOGI("DISPLAY", "device ready");
      }

      ever_online     = true;
      tick            = xTaskGetTickCount();
      last_probe_tick = tick;
    }

    vTaskDelayUntil(&tick, pdMS_TO_TICKS(DISPLAY_REFRESH_MS));

    // 화면은 200 ms마다 갱신하되, 내용이 바뀌지 않을 때의 연결 확인은 1초마다만
    // 수행해 자이로와 공유하는 I2C0 버스의 불필요한 트래픽을 줄인다.
    esp_err_t ret  = ESP_OK;
    TickType_t now = xTaskGetTickCount();
    if ((TickType_t)(now - last_probe_tick) >= pdMS_TO_TICKS(DISPLAY_PROBE_MS)) {
      ret             = i2c_master_probe(i2c0, PCF8574_ADDR, I2C_TIMEOUT_MS);
      last_probe_tick = now;
    }

    // CAN 값이 잠시 끊겨도 "--"로 되돌리지 않고 마지막 정상값을 유지한다.
    // 새 값이 도착했을 때만 캐시를 갱신하므로 서로 다른 CAN 주기의 간격에서도
    // LCD가 정상값과 미수신 표시 사이를 오가지 않는다.
    if (display_can.ez_rpm_valid) {
      cached_rpm_raw = display_can.ez_rpm_raw;
      rpm_received   = true;
    }
    if (display_can.bms_soc_valid) {
      cached_soc_raw = display_can.bms_soc_raw;
      soc_received   = true;
    }

    memset(lcd_want, ' ', sizeof(lcd_want));

    char line[LCD_COLS + 1];
    if (!soc_received) {
      snprintf(line, sizeof(line), "SOC: --.-%%");
    } else {
      float soc = cached_soc_raw * 0.1f;
      snprintf(line, sizeof(line), "SOC: %.1f%%", soc);
    }
    lcd_write_text(0, DISPLAY_TEXT_COL, line);

    if (!rpm_received) {
      snprintf(line, sizeof(line), "VEL: --.-km/h");
    } else {
      float rpm = motor_decode_rpm(cached_rpm_raw);
      rpm = fabsf(rpm);
      float spd_kmh = rpm / DISPLAY_GEAR_RATIO * VEHICLE_TIRE_CIRC_M * 60.0f / 1000.0f;
      snprintf(line, sizeof(line), "VEL: %.1fkm/h", spd_kmh);
    }
    lcd_write_text(1, DISPLAY_TEXT_COL, line);

    if (ret == ESP_OK) ret = lcd_flush();

    if (ret == ESP_OK) {
      consecutive_failures = 0;
      continue;
    }

    consecutive_failures++;
    if (consecutive_failures < SENSOR_FAILURE_THRESHOLD) continue;

    SYSLOG("SNS:LCD:ERR");
    SET_ERROR(&logbuf.run, DISPLAY);
    ESP_LOGW("DISPLAY", "write failed %d consecutive times; backing off for %d ms",
             (int)SENSOR_FAILURE_THRESHOLD, (int)SENSOR_RETRY_MS);

    display_disconnect();
    online          = false;
    outage_reported = true;

    vTaskDelay(pdMS_TO_TICKS(SENSOR_RETRY_MS));
  }
}
