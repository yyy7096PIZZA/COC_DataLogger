#include "main.h"

#include "driver/i2c_master.h"

#define GYRO_I2C_ADDR 0x68
#define GYRO_CALIBRATION_SAMPLES 32

static void gyro_disconnect(i2c_master_dev_handle_t *gyro) {
  if (*gyro == NULL) return;

  i2c_master_bus_rm_device(*gyro);
  *gyro = NULL;
}

/*******************************************************************************
 * Configure the MPU6050 and recalculate its hardware gyro offsets.
 *
 * This is deliberately run after every reconnect: the device may have lost
 * power while the ESP32 stayed up, so its previous register contents cannot be
 * trusted merely because it ACKs again.
 ******************************************************************************/
static esp_err_t gyro_configure_and_calibrate(i2c_master_dev_handle_t gyro) {
  uint8_t reg = 0x75;  // WHO_AM_I
  uint8_t who_am_i;
  esp_err_t ret = i2c_master_transmit_receive(gyro, &reg, 1, &who_am_i, 1, I2C_TIMEOUT_MS);
  if (ret != ESP_OK) return ret;
  if (who_am_i != GYRO_I2C_ADDR) return ESP_ERR_INVALID_RESPONSE;

  // MPU6050 powers up in sleep mode. Wake it explicitly on every connection,
  // then allow its clock and sensor paths to settle before configuration.
  uint8_t wake[2] = { 0x6B, 0x00 };  // PWR_MGMT_1, internal clock / sleep off
  ret = i2c_master_transmit(gyro, wake, sizeof(wake), I2C_TIMEOUT_MS);
  if (ret != ESP_OK) return ret;
  vTaskDelay(pdMS_TO_TICKS(100));

  uint8_t tx[7] = { 0x1B, 1 << 3, 1 << 4 };  // 500dps gyro, 8g accel full scale
  ret = i2c_master_transmit(gyro, tx, 3, I2C_TIMEOUT_MS);
  if (ret != ESP_OK) return ret;

  // Clear all gyro offset registers before taking calibration samples.
  tx[0] = 0x13;  // XG_OFFSET_H register address
  tx[1] = 0;
  tx[2] = 0;
  tx[3] = 0;
  tx[4] = 0;
  tx[5] = 0;
  tx[6] = 0;

  ret = i2c_master_transmit(gyro, tx, 7, I2C_TIMEOUT_MS);
  if (ret != ESP_OK) return ret;

  int32_t sum_x = 0;
  int32_t sum_y = 0;
  int32_t sum_z = 0;

  tx[0] = 0x43;  // GYRO_XOUT_H register address

  for (int i = 0; i < GYRO_CALIBRATION_SAMPLES; i++) {
    uint8_t rx[6] = { 0 };

    ret = i2c_master_transmit_receive(gyro, tx, 1, rx, sizeof(rx), I2C_TIMEOUT_MS);
    if (ret != ESP_OK) return ret;

    sum_x += (int16_t)(((uint16_t)rx[0] << 8) | rx[1]);
    sum_y += (int16_t)(((uint16_t)rx[2] << 8) | rx[3]);
    sum_z += (int16_t)(((uint16_t)rx[4] << 8) | rx[5]);

    // Spread samples over 32ms instead of averaging a single burst.
    vTaskDelay(pdMS_TO_TICKS(1));
  }

  int32_t off_x = -sum_x / GYRO_CALIBRATION_SAMPLES / 2;
  int32_t off_y = -sum_y / GYRO_CALIBRATION_SAMPLES / 2;
  int32_t off_z = -sum_z / GYRO_CALIBRATION_SAMPLES / 2;

  tx[0] = 0x13;
  tx[1] = (uint8_t)(off_x >> 8);
  tx[2] = (uint8_t)(off_x & 0xFF);
  tx[3] = (uint8_t)(off_y >> 8);
  tx[4] = (uint8_t)(off_y & 0xFF);
  tx[5] = (uint8_t)(off_z >> 8);
  tx[6] = (uint8_t)(off_z & 0xFF);

  return i2c_master_transmit(gyro, tx, 7, I2C_TIMEOUT_MS);
}

static esp_err_t gyro_connect(i2c_master_bus_handle_t i2c0,
                              i2c_master_dev_handle_t *gyro,
                              bool *device_missing) {
  *device_missing = false;

  esp_err_t ret = i2c_master_probe(i2c0, GYRO_I2C_ADDR, I2C_TIMEOUT_MS);
  if (ret != ESP_OK) {
    *device_missing = true;
    return ret;
  }

  i2c_device_config_t gyro_cfg = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address  = GYRO_I2C_ADDR,
    .scl_speed_hz    = 400000,
  };

  ret = i2c_master_bus_add_device(i2c0, &gyro_cfg, gyro);
  if (ret != ESP_OK) return ret;

  ret = gyro_configure_and_calibrate(*gyro);
  if (ret != ESP_OK) gyro_disconnect(gyro);

  return ret;
}

/*******************************************************************************
 * Gyroscope monitor task
 ******************************************************************************/
void task_gyroscope(void *pvParameters) {
  (void)pvParameters;

  i2c_master_bus_handle_t i2c0;

  // I2C0 is created by the main thread whenever this task is enabled.
  if (i2c_master_get_bus_handle(I2C_NUM_0, &i2c0) != ESP_OK) {
    CLEAR_FATAL(&logbuf.run, GYRO);
    SYSLOG("SNS:GYR:ERR");
    ERROR_LOG(&logbuf.run, GYRO, "I2C0 bus not found");
    vTaskDelete(NULL);
    return;
  }

  i2c_master_dev_handle_t gyro = NULL;
  bool online                   = false;
  bool outage_reported          = false;
  bool ever_online              = false;
  uint32_t consecutive_failures = 0;
  TickType_t xLastWakeTime      = xTaskGetTickCount();

  // Remove the placeholder error/fatal bits installed before peripheral tasks
  // start. A genuinely missing enabled gyro is set back to ERROR below.
  CLEAR_ALL(&logbuf.run, GYRO);

  while (true) {
    if (!online) {
      bool device_missing;
      esp_err_t ret = gyro_connect(i2c0, &gyro, &device_missing);
      if (ret != ESP_OK) {
        if (!outage_reported) {
          bool report_error = ever_online || !device_missing;
          SET_ERROR(&logbuf.run, GYRO);
          SYSLOG(report_error ? "SNS:GYR:ERR" : "SNS:GYR:MISS");
          if (ever_online) {
            ESP_LOGW("GYRO", "device unavailable; retrying every %d ms", (int)SENSOR_RETRY_MS);
          } else if (!device_missing) {
            ESP_LOGW("GYRO", "device initialization failed; retrying every %d ms", (int)SENSOR_RETRY_MS);
          } else {
            ESP_LOGW("GYRO", "device not found; retrying every %d ms", (int)SENSOR_RETRY_MS);
          }
          outage_reported = true;
        }

        vTaskDelay(pdMS_TO_TICKS(SENSOR_RETRY_MS));
        continue;
      }

      online               = true;
      consecutive_failures = 0;
      CLEAR_ALL(&logbuf.run, GYRO);

      if (outage_reported) {
        SYSLOG("SNS:GYR:OK");
        ESP_LOGI("GYRO", "device recovered");
        outage_reported = false;
      } else {
        SYSLOG("SNS:GYR:OK");
        SYSLOG("GYR_RDY");
      }

      ever_online   = true;
      xLastWakeTime = xTaskGetTickCount();
    }

    uint8_t tx     = 0x3B;  // ACCEL_XOUT_H register address
    uint8_t rx[14] = { 0 };  // through GYRO_ZOUT_L
    esp_err_t ret  = i2c_master_transmit_receive(gyro, &tx, 1, rx, sizeof(rx), I2C_TIMEOUT_MS);

    if (ret == ESP_OK) {
      consecutive_failures = 0;

      log_t gyro_log = { 0 };
      gyro_log.payload.gyroscope.accel_x     = (int16_t)(((uint16_t)rx[0] << 8) | rx[1]);
      gyro_log.payload.gyroscope.accel_y     = (int16_t)(((uint16_t)rx[2] << 8) | rx[3]);
      gyro_log.payload.gyroscope.accel_z     = (int16_t)(((uint16_t)rx[4] << 8) | rx[5]);
      gyro_log.payload.gyroscope.temperature = (int16_t)(((uint16_t)rx[6] << 8) | rx[7]);
      gyro_log.payload.gyroscope.gyro_x      = (int16_t)(((uint16_t)rx[8] << 8) | rx[9]);
      gyro_log.payload.gyroscope.gyro_y      = (int16_t)(((uint16_t)rx[10] << 8) | rx[11]);
      gyro_log.payload.gyroscope.gyro_z      = (int16_t)(((uint16_t)rx[12] << 8) | rx[13]);
      debug_monitor_publish_gyroscope(&gyro_log.payload.gyroscope);
      LOG(LOG_TYPE_GYROSCOPE, &gyro_log);
    } else {
      consecutive_failures++;

      if (consecutive_failures >= SENSOR_FAILURE_THRESHOLD) {
        SET_ERROR(&logbuf.run, GYRO);
        SYSLOG("SNS:GYR:ERR");
        ESP_LOGW("GYRO", "read failed %d consecutive times; backing off for %d ms",
                 (int)SENSOR_FAILURE_THRESHOLD, (int)SENSOR_RETRY_MS);

        gyro_disconnect(&gyro);
        online          = false;
        outage_reported = true;

        vTaskDelay(pdMS_TO_TICKS(SENSOR_RETRY_MS));
        continue;
      }
    }

    // Preserve the original 100Hz sample cadence while the device is active.
    xTaskDelayUntil(&xLastWakeTime, TASK_INTERVAL_GYRO);
  }
}
