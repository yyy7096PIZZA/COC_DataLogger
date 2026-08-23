#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "main.h"

#include "driver/gpio.h"
#include "driver/uart.h"

QueueHandle_t uart_queue;
static bool gps_uart_ready;

/* NMEA GPRMC message */
typedef struct {
  uint8_t *id;
  uint8_t *utc_time;
  uint8_t *status;
  uint8_t *lat;
  uint8_t *north;
  uint8_t *lon;
  uint8_t *east;
  uint8_t *speed;
  uint8_t *course;
  uint8_t *utc_date;
  uint8_t *others;
} nmea_gprmc_t;

/* NUL-terminate the current field at the next comma and return the next field.
 * Returns NULL when there is no next comma (truncated/corrupted sentence). */
static uint8_t *next_field(uint8_t *s) {
  uint8_t *p = (uint8_t *)strchr((char *)s, ',');
  if (p == NULL) return NULL;
  *p = '\0';
  return p + 1;
}

bool parse_nmea_gprmc(nmea_gprmc_t *gprmc, uint8_t *data) {
  gprmc->id       = data;
  gprmc->utc_time = next_field(gprmc->id);
  if (gprmc->utc_time == NULL) return false;

  gprmc->status = next_field(gprmc->utc_time);
  if (gprmc->status == NULL || *gprmc->status != 'A') return false;

  gprmc->lat = next_field(gprmc->status);
  if (gprmc->lat == NULL) return false;

  gprmc->north = next_field(gprmc->lat);
  if (gprmc->north == NULL) return false;

  gprmc->lon = next_field(gprmc->north);
  if (gprmc->lon == NULL) return false;

  gprmc->east = next_field(gprmc->lon);
  if (gprmc->east == NULL) return false;

  gprmc->speed = next_field(gprmc->east);
  if (gprmc->speed == NULL) return false;

  gprmc->course = next_field(gprmc->speed);
  if (gprmc->course == NULL) return false;

  gprmc->utc_date = next_field(gprmc->course);
  if (gprmc->utc_date == NULL) return false;

  gprmc->others = next_field(gprmc->utc_date);
  if (gprmc->others == NULL) return false;

  return true;
}

static const uint32_t GPS_BAUD_RATES[] = { 115200, 57600, 38400, 19200, 9600 };
#define GPS_BAUD_RATES_N (sizeof(GPS_BAUD_RATES) / sizeof(GPS_BAUD_RATES[0]))
#define GPS_BAUD_PROBE_MS 1000
#define GPS_COMM_WINDOW_MS 5000

static uint32_t gps_last_baud = 9600;

static esp_err_t gps_uart_init(void) {
  uart_config_t uart_config = {
    .baud_rate = 9600,
    .data_bits = UART_DATA_8_BITS,
    .parity    = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
  };

  if (uart_driver_install(UART_NUM_1, 2048, 256, 16, &uart_queue, 0) != ESP_OK) {
    uart_queue = NULL;
    return ESP_FAIL;
  }
  gps_uart_ready = true;

  if (uart_param_config(UART_NUM_1, &uart_config) != ESP_OK ||
      uart_set_pin(UART_NUM_1, GPIO_NUM_17, GPIO_NUM_18, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK ||
      uart_enable_pattern_det_baud_intr(UART_NUM_1, '\n', 1, 10, 0, 0) != ESP_OK) {
    uart_driver_delete(UART_NUM_1);
    gps_uart_ready = false;
    uart_queue     = NULL;
    return ESP_FAIL;
  }

  return ESP_OK;
}

static void gps_uart_deinit(void) {
  if (gps_uart_ready) uart_driver_delete(UART_NUM_1);
  gps_uart_ready = false;
  uart_queue     = NULL;
}

typedef enum {
  GPS_PROBE_OK,
  GPS_PROBE_MISSING,
  GPS_PROBE_ERROR,
} gps_probe_result_t;

static bool gps_write_command(const uint8_t *command, size_t size) {
  return uart_write_bytes(UART_NUM_1, command, size) == (int)size;
}

static gps_probe_result_t probe_and_configure_ublox(void) {
  bool found      = false;
  uint32_t bitrate = gps_last_baud;

  // Try the last working rate first so a reattached device normally recovers
  // in one NMEA period. The remaining rates form one bounded scan cycle.
  for (size_t attempt = 0; attempt < GPS_BAUD_RATES_N; attempt++) {
    uint32_t candidate = gps_last_baud;
    if (attempt != 0) {
      size_t remaining = attempt - 1;
      for (size_t i = 0; i < GPS_BAUD_RATES_N; i++) {
        if (GPS_BAUD_RATES[i] == gps_last_baud) continue;
        if (remaining-- == 0) {
          candidate = GPS_BAUD_RATES[i];
          break;
        }
      }
    }

    if (uart_set_baudrate(UART_NUM_1, candidate) != ESP_OK || uart_flush_input(UART_NUM_1) != ESP_OK ||
        uart_pattern_queue_reset(UART_NUM_1, 16) != ESP_OK) {
      return GPS_PROBE_ERROR;
    }

    uint8_t peek[64];
    int len = uart_read_bytes(UART_NUM_1, peek, sizeof(peek), pdMS_TO_TICKS(GPS_BAUD_PROBE_MS));

    for (int j = 0; j < len; j++) {
      if (peek[j] == '$') {
        bitrate = candidate;
        found   = true;
        break;
      }
    }

    if (found) break;
  }

  if (!found) return GPS_PROBE_MISSING;

  gps_last_baud = bitrate;
  if (uart_set_baudrate(UART_NUM_1, bitrate) != ESP_OK || uart_flush_input(UART_NUM_1) != ESP_OK ||
      uart_pattern_queue_reset(UART_NUM_1, 16) != ESP_OK) {
    return GPS_PROBE_ERROR;
  }

  const uint8_t GPS_DISABLE_NMEA_GxGGA[] = { 0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x24 };
  const uint8_t GPS_DISABLE_NMEA_GxGLL[] = { 0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x01, 0x2B };
  const uint8_t GPS_DISABLE_NMEA_GxGSA[] = { 0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x02, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x02, 0x32 };
  const uint8_t GPS_DISABLE_NMEA_GxGSV[] = { 0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x03, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x03, 0x39 };
  const uint8_t GPS_DISABLE_NMEA_GxVTG[] = { 0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x05, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x05, 0x47 };
  const uint8_t GPS_DISABLE_NMEA_GxZDA[] = { 0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x08, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x07, 0x5B };
  const uint8_t GPS_DISABLE_NMEA_GxTXT[] = { 0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x41, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x40, 0xEA };

  const uint8_t GPS_PMS_FULL[]  = { 0xB5, 0x62, 0x06, 0x86, 0x00, 0x00, 0x8C, 0xAA };
  const uint8_t GPS_RATE_10HZ[] = { 0xB5, 0x62, 0x06, 0x08, 0x06, 0x00, 0x64, 0x00, 0x01, 0x00, 0x01, 0x00, 0x7A,
    0x12 };

  if (!gps_write_command(GPS_DISABLE_NMEA_GxGGA, sizeof(GPS_DISABLE_NMEA_GxGGA)) ||
      !gps_write_command(GPS_DISABLE_NMEA_GxGLL, sizeof(GPS_DISABLE_NMEA_GxGLL)) ||
      !gps_write_command(GPS_DISABLE_NMEA_GxGSA, sizeof(GPS_DISABLE_NMEA_GxGSA)) ||
      !gps_write_command(GPS_DISABLE_NMEA_GxGSV, sizeof(GPS_DISABLE_NMEA_GxGSV)) ||
      !gps_write_command(GPS_DISABLE_NMEA_GxVTG, sizeof(GPS_DISABLE_NMEA_GxVTG)) ||
      !gps_write_command(GPS_DISABLE_NMEA_GxZDA, sizeof(GPS_DISABLE_NMEA_GxZDA)) ||
      !gps_write_command(GPS_DISABLE_NMEA_GxTXT, sizeof(GPS_DISABLE_NMEA_GxTXT)) ||
      !gps_write_command(GPS_PMS_FULL, sizeof(GPS_PMS_FULL)) ||
      !gps_write_command(GPS_RATE_10HZ, sizeof(GPS_RATE_10HZ))) {
    return GPS_PROBE_ERROR;
  }

  if (uart_flush_input(UART_NUM_1) != ESP_OK || uart_pattern_queue_reset(UART_NUM_1, 16) != ESP_OK) {
    return GPS_PROBE_ERROR;
  }
  return GPS_PROBE_OK;
}

static bool gps_receive_nmea_sentence(uint8_t *data, size_t data_size) {
  uart_event_t event = { 0 };
  if (xQueueReceive(uart_queue, &event, pdMS_TO_TICKS(500)) != pdTRUE) return false;

  if (event.type == UART_FIFO_OVF || event.type == UART_BUFFER_FULL) {
    uart_flush_input(UART_NUM_1);
    uart_pattern_queue_reset(UART_NUM_1, 16);
    return false;
  }

  if (event.type != UART_PATTERN_DET) {
    // A stream with no line endings can otherwise keep this task runnable
    // continuously until the communication watchdog expires.
    vTaskDelay(pdMS_TO_TICKS(1));
    return false;
  }

  int pos = uart_pattern_pop_pos(UART_NUM_1);
  int len = -1;

  // pos + 1 bytes are read below and one more byte is needed for the NUL.
  if (pos < 0 || pos + 2 > (int)data_size ||
      (len = uart_read_bytes(UART_NUM_1, data, pos + 1, pdMS_TO_TICKS(0))) < 6) {
    uart_flush_input(UART_NUM_1);
    uart_pattern_queue_reset(UART_NUM_1, 16);
    return false;
  }

  data[len] = '\0';
  uart_pattern_queue_reset(UART_NUM_1, 16);
  return data[0] == '$';
}

/*******************************************************************************
 * Integer-only fixed-point NMEA field parser
 * Parses decimal string to integer scaled by 10^target_frac_digits.
 ******************************************************************************/
static uint32_t parse_nmea_fixed(const char *s, int target_frac_digits) {
  uint32_t integer = 0, frac = 0;
  int frac_digits = 0;

  while (*s >= '0' && *s <= '9') { integer = integer * 10 + (*s++ - '0'); }
  if (*s == '.') {
    s++;
    while (*s >= '0' && *s <= '9' && frac_digits < target_frac_digits + 1) {
      frac = frac * 10 + (*s++ - '0');
      frac_digits++;
    }
  }

  while (frac_digits < target_frac_digits) { frac *= 10; frac_digits++; }
  while (frac_digits > target_frac_digits) { frac = (frac + 5) / 10; frac_digits--; }

  uint32_t scale = 1;
  for (int i = 0; i < target_frac_digits; i++) scale *= 10;
  return integer * scale + frac;
}

/*******************************************************************************
 * GPS NMEA GPRMC message monitor task
 ******************************************************************************/
void task_gps(void *pvParameters) {
  (void)pvParameters;

  if (storage.gps.dev != GPS_DEV_UBLOX) {
    CLEAR_FATAL(&logbuf.run, GPS);
    SET_ERROR(&logbuf.run, GPS);
    ESP_LOGW("GPS", "unknown GPS device type: %u", (unsigned)storage.gps.dev);
    SYSLOG("SNS:GPS:ERR");
    vTaskDelete(NULL);
    return;
  }

  log_t gps                 = { 0 };
  uint8_t data[256]         = { 0 };
  nmea_gprmc_t gprmc        = { 0 };
  bool clock_set            = false;  // system wall clock set once from the first valid GPS UTC fix
  bool online               = false;
  bool ever_online          = false;
  bool fault_reported       = false;
  unsigned consecutive_fail = 0;
  TickType_t last_sentence_tick = 0;

  // Probing is a normal enabled-but-not-yet-classified state, not an error.
  CLEAR_ALL(&logbuf.run, GPS);
  debug_monitor_publish_gps_online(false);

  while (true) {
    if (!online) {
      TickType_t probe_cycle_start = xTaskGetTickCount();
      const bool uart_ok = gps_uart_init() == ESP_OK;
      const gps_probe_result_t probe_result = uart_ok ? probe_and_configure_ublox() : GPS_PROBE_ERROR;
      const bool found = probe_result == GPS_PROBE_OK;

      if (!found) {
        gps_uart_deinit();
        debug_monitor_publish_gps_online(false);
        debug_monitor_publish_gps_fix(false);
        CLEAR_FATAL(&logbuf.run, GPS);
        SET_ERROR(&logbuf.run, GPS);

        if (!fault_reported) {
          if (ever_online || !uart_ok || probe_result == GPS_PROBE_ERROR) {
            ESP_LOGW("GPS", "GPS communication error; retrying every %d ms", (int)SENSOR_RETRY_MS);
            SYSLOG("SNS:GPS:ERR");
          } else {
            ESP_LOGW("GPS", "GPS not detected; retrying every %d ms", (int)SENSOR_RETRY_MS);
            SYSLOG("SNS:GPS:MISS");
          }
          fault_reported = true;
        }

        // Start each complete baud scan at most SENSOR_RETRY_MS apart. The
        // scan itself waits for NMEA, so only pad whatever time remains.
        TickType_t retry_ticks = pdMS_TO_TICKS(SENSOR_RETRY_MS);
        TickType_t elapsed     = xTaskGetTickCount() - probe_cycle_start;
        if (elapsed < retry_ticks) vTaskDelay(retry_ticks - elapsed);
        continue;
      }

      online               = true;
      ever_online          = true;
      consecutive_fail     = 0;
      CLEAR_ALL(&logbuf.run, GPS);
      debug_monitor_publish_gps_online(true);
      INFO(GPS, "GPS online");
      SYSLOG("SNS:GPS:OK");
      fault_reported    = false;
      last_sentence_tick = xTaskGetTickCount();
    }

    bool valid_sentence = gps_receive_nmea_sentence(data, sizeof(data));
    if (valid_sentence) {
      consecutive_fail = 0;
      last_sentence_tick = xTaskGetTickCount();

      if (strncmp((char *)data, "$GNRMC", 6) == 0 || strncmp((char *)data, "$GPRMC", 6) == 0) {
        gprmc = (nmea_gprmc_t){ 0 };
        if (parse_nmea_gprmc(&gprmc, data)) {
          gps.payload.gps.latitude  = parse_nmea_fixed((char *)gprmc.lat, 5);
          gps.payload.gps.longitude = parse_nmea_fixed((char *)gprmc.lon, 5);
          gps.payload.gps.lat_dir   = *gprmc.north;
          gps.payload.gps.lon_dir   = *gprmc.east;
          uint32_t speed_x100       = parse_nmea_fixed((char *)gprmc.speed, 2);
          gps.payload.gps.speed     = (uint16_t)((speed_x100 * 1852 + 500) / 1000);
          gps.payload.gps.course    = (uint16_t)parse_nmea_fixed((char *)gprmc.course, 2);
          debug_monitor_publish_gps(&gps.payload.gps, true);
          LOG(LOG_TYPE_GPS, &gps);

          // set the system wall clock once from the first valid GPS UTC fix.
          // utc_time = "hhmmss.ss", utc_date = "ddmmyy" (both are numeric with a
          // fix present, since status == 'A').
          if (!clock_set && strlen((char *)gprmc.utc_time) >= 6 && strlen((char *)gprmc.utc_date) >= 6) {
            const uint8_t *t = gprmc.utc_time;
            const uint8_t *d = gprmc.utc_date;
#define D2(p, i) (((p)[i] - '0') * 10 + ((p)[(i) + 1] - '0'))
            struct tm tm = {
              .tm_hour = D2(t, 0),
              .tm_min  = D2(t, 2),
              .tm_sec  = D2(t, 4),
              .tm_mday = D2(d, 0),
              .tm_mon  = D2(d, 2) - 1,    // tm_mon is 0-based
              .tm_year = D2(d, 4) + 100,  // ddmmYY → 20YY; tm_year is years since 1900
            };
#undef D2

            // ensure mktime interprets tm as UTC so the result is a UTC epoch
            setenv("TZ", "UTC", 1);
            tzset();

            time_t seconds = mktime(&tm);

            if (seconds != (time_t)-1) {
              struct timeval tv = { .tv_sec = seconds, .tv_usec = 0 };
              settimeofday(&tv, NULL);
              clock_set = true;

              // STEP 7: signal task_sdcard to correct the BOOT record's boot_time in place.
              // boot happened uptime seconds ago, so the boot epoch = now - uptime. Publishing
              // a non-zero epoch here is the one-shot trigger; task_sdcard rewrites record 0.
              boot_time_fixup_epoch = (uint64_t)tv.tv_sec - (uint64_t)(esp_timer_get_time() / 1000000);

              INFO(GPS, "system clock set from GPS: %s", ctime(&tv.tv_sec));
            }
          }
        } else if (gprmc.status != NULL && *gprmc.status != 'A') {
          debug_monitor_publish_gps_fix(false);
        }
      }
    }

    TickType_t now = xTaskGetTickCount();
    if (!valid_sentence && now - last_sentence_tick >= pdMS_TO_TICKS(GPS_COMM_WINDOW_MS)) {
      last_sentence_tick = now;
      debug_monitor_publish_gps_fix(false);
      if (++consecutive_fail >= SENSOR_FAILURE_THRESHOLD) {
        CLEAR_FATAL(&logbuf.run, GPS);
        SET_ERROR(&logbuf.run, GPS);
        if (!fault_reported) {
          ESP_LOGW("GPS", "GPS stopped producing valid NMEA; entering retry mode");
          SYSLOG("SNS:GPS:ERR");
          fault_reported = true;
        }
        online = false;
        debug_monitor_publish_gps_online(false);
        gps_uart_deinit();
      }
    }
  }
}
