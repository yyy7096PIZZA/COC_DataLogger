#include "main.h"

#if DEBUG_SENSOR_MONITOR

#include <inttypes.h>
#include <stdio.h>

#define DEBUG_MONITOR_TAG "SENSOR"
#define DEBUG_GPS_FIX_TIMEOUT pdMS_TO_TICKS(1500)

enum {
  DEBUG_WHEEL_FL,
  DEBUG_WHEEL_FR,
  DEBUG_WHEEL_RL,
  DEBUG_WHEEL_RR,
  DEBUG_WHEEL_COUNT,
};

typedef struct {
  float wheel_speed_kmh[DEBUG_WHEEL_COUNT];
  uint32_t wheel_pulse_count[DEBUG_WHEEL_COUNT];
  analog_record_t analog;
  gyroscope_record_t gyroscope;
  gps_record_t gps;
  motor_runtime_t motor;
  bms_runtime_t bms;
  TickType_t gps_last_fix_tick;
  bool wheel_valid;
  bool analog_valid;
  bool gyroscope_valid;
  bool gps_position_valid;
  bool gps_fix;
  bool gps_online;
  bool motor_seen;
  bool bms_seen;
  bool sd_mounted;
  bool sd_logging;
} debug_monitor_snapshot_t;

static debug_monitor_snapshot_t debug_snapshot;
static portMUX_TYPE debug_snapshot_lock = portMUX_INITIALIZER_UNLOCKED;

void debug_monitor_publish_wheels(const float speed_kmh[4], const uint32_t pulse_count[4]) {
  portENTER_CRITICAL(&debug_snapshot_lock);
  for (int i = 0; i < DEBUG_WHEEL_COUNT; i++) {
    debug_snapshot.wheel_speed_kmh[i]   = speed_kmh[i];
    debug_snapshot.wheel_pulse_count[i] = pulse_count[i];
  }
  debug_snapshot.wheel_valid = true;
  portEXIT_CRITICAL(&debug_snapshot_lock);
}

void debug_monitor_publish_analog(const analog_record_t *sample) {
  portENTER_CRITICAL(&debug_snapshot_lock);
  debug_snapshot.analog       = *sample;
  debug_snapshot.analog_valid = true;
  portEXIT_CRITICAL(&debug_snapshot_lock);
}

void debug_monitor_publish_gyroscope(const gyroscope_record_t *sample) {
  portENTER_CRITICAL(&debug_snapshot_lock);
  debug_snapshot.gyroscope.accel_x = sample->accel_x;
  debug_snapshot.gyroscope.accel_y = sample->accel_y;
  debug_snapshot.gyroscope.accel_z = sample->accel_z;
  debug_snapshot.gyroscope.gyro_x  = sample->gyro_x;
  debug_snapshot.gyroscope.gyro_y  = sample->gyro_y;
  debug_snapshot.gyroscope.gyro_z  = sample->gyro_z;
  debug_snapshot.gyroscope_valid   = true;
  portEXIT_CRITICAL(&debug_snapshot_lock);
}

void debug_monitor_publish_gps(const gps_record_t *sample, bool fix) {
  TickType_t fix_tick = fix ? xTaskGetTickCount() : 0;

  portENTER_CRITICAL(&debug_snapshot_lock);
  debug_snapshot.gps                = *sample;
  debug_snapshot.gps_position_valid = true;
  debug_snapshot.gps_fix            = fix;
  if (fix) debug_snapshot.gps_last_fix_tick = fix_tick;
  portEXIT_CRITICAL(&debug_snapshot_lock);
}

void debug_monitor_publish_gps_fix(bool fix) {
  TickType_t fix_tick = fix ? xTaskGetTickCount() : 0;

  portENTER_CRITICAL(&debug_snapshot_lock);
  debug_snapshot.gps_fix = fix;
  if (fix) debug_snapshot.gps_last_fix_tick = fix_tick;
  portEXIT_CRITICAL(&debug_snapshot_lock);
}

void debug_monitor_publish_gps_online(bool online) {
  portENTER_CRITICAL(&debug_snapshot_lock);
  debug_snapshot.gps_online = online;
  if (!online) debug_snapshot.gps_fix = false;
  portEXIT_CRITICAL(&debug_snapshot_lock);
}

void debug_monitor_publish_motor(const motor_runtime_t *sample) {
  portENTER_CRITICAL(&debug_snapshot_lock);
  debug_snapshot.motor      = *sample;
  debug_snapshot.motor_seen = true;
  portEXIT_CRITICAL(&debug_snapshot_lock);
}

void debug_monitor_publish_bms(const bms_runtime_t *sample) {
  portENTER_CRITICAL(&debug_snapshot_lock);
  debug_snapshot.bms      = *sample;
  debug_snapshot.bms_seen = true;
  portEXIT_CRITICAL(&debug_snapshot_lock);
}

void debug_monitor_publish_sd_status(bool mounted, bool logging) {
  portENTER_CRITICAL(&debug_snapshot_lock);
  debug_snapshot.sd_mounted = mounted;
  debug_snapshot.sd_logging = logging;
  portEXIT_CRITICAL(&debug_snapshot_lock);
}

#if SENSOR_ENABLE_GPS
static double gps_coordinate_degrees(uint32_t nmea_x100000, uint8_t direction) {
  uint32_t degrees          = nmea_x100000 / 10000000U;
  uint32_t minutes_x100000  = nmea_x100000 % 10000000U;
  double coordinate_degrees = (double)degrees + (double)minutes_x100000 / 6000000.0;

  if (direction == 'S' || direction == 'W') coordinate_degrees = -coordinate_degrees;
  return coordinate_degrees;
}
#endif

static const char *__attribute__((unused)) debug_component_state(state_component_t component, bool sample_valid) {
  if (IS_ERROR(&logbuf.run, component) || IS_FATAL(&logbuf.run, component)) {
    return sample_valid ? "ERROR/PARTIAL" : "ERROR";
  }
  return sample_valid ? "OK" : "WAITING";
}

#if SENSOR_DIGITAL_ACTIVE
static void debug_format_wheel(char *buf, size_t size, unsigned wheel, const debug_monitor_snapshot_t *snapshot) {
  if ((SENSOR_EFFECTIVE_WHEEL_MASK & (1U << wheel)) == 0) {
    snprintf(buf, size, "OFF");
  } else {
    snprintf(buf, size, "%.2fkm/h/%" PRIu32, (double)snapshot->wheel_speed_kmh[wheel],
      snapshot->wheel_pulse_count[wheel]);
  }
}
#endif

#if SENSOR_ANALOG_ACTIVE
static void debug_format_analog(char *buf, size_t size, unsigned channel, int16_t value) {
  if ((SENSOR_EFFECTIVE_ANALOG_MASK & (1U << channel)) == 0) {
    snprintf(buf, size, "OFF");
  } else {
    snprintf(buf, size, "%d", (int)value);
  }
}
#endif

#if SENSOR_ENABLE_CAN
static void debug_print_bms_cells(const bms_runtime_t *bms) {
  uint8_t count = bms->cell_voltage_count;
  if (bms->cell_string_count > 0U && bms->cell_string_count < count) count = bms->cell_string_count;

  for (uint8_t start = 0; start < count; start += 12U) {
    char line[256];
    int used = snprintf(line, sizeof(line), "BMS CELL | ");
    uint8_t end = start + 12U;
    if (end > count) end = count;
    for (uint8_t i = start; i < end && used > 0 && (size_t)used < sizeof(line); i++) {
      used += snprintf(line + used, sizeof(line) - (size_t)used,
                       "C%u=%umV%s", (unsigned)(i + 1U), (unsigned)bms->cell_voltage[i],
                       i + 1U < end ? " " : "");
    }
    ESP_LOGI(DEBUG_MONITOR_TAG, "%s", line);
  }
  if (count == 0U) ESP_LOGI(DEBUG_MONITOR_TAG, "BMS CELL | N/A");
}

static void debug_print_bms_temps(const bms_runtime_t *bms) {
  uint8_t count = bms->cell_temp_count;
  if (bms->temp_sensor_count > 0U && bms->temp_sensor_count < count) count = bms->temp_sensor_count;

  for (uint8_t start = 0; start < count; start += 12U) {
    char line[256];
    int used = snprintf(line, sizeof(line), "BMS TEMP | ");
    uint8_t end = start + 12U;
    if (end > count) end = count;
    for (uint8_t i = start; i < end && used > 0 && (size_t)used < sizeof(line); i++) {
      used += snprintf(line + used, sizeof(line) - (size_t)used,
                       "T%u=%dC%s", (unsigned)(i + 1U), (int)bms->cell_temp[i],
                       i + 1U < end ? " " : "");
    }
    ESP_LOGI(DEBUG_MONITOR_TAG, "%s", line);
  }
  if (count == 0U) ESP_LOGI(DEBUG_MONITOR_TAG, "BMS TEMP | N/A");
}
#endif

static void debug_monitor_print(const debug_monitor_snapshot_t *snapshot) {
#if SENSOR_DIGITAL_ACTIVE
  char wheel[DEBUG_WHEEL_COUNT][32];
  for (unsigned i = 0; i < DEBUG_WHEEL_COUNT; i++) {
    debug_format_wheel(wheel[i], sizeof(wheel[i]), i, snapshot);
  }
  ESP_LOGI(DEBUG_MONITOR_TAG, "WHEEL | state=%s | FL=%s FR=%s RL=%s RR=%s",
    debug_component_state(DIGITAL, snapshot->wheel_valid), wheel[DEBUG_WHEEL_FL], wheel[DEBUG_WHEEL_FR],
    wheel[DEBUG_WHEEL_RL], wheel[DEBUG_WHEEL_RR]);
#else
  ESP_LOGI(DEBUG_MONITOR_TAG, "WHEEL | DISABLED");
#endif

#if SENSOR_ANALOG_ACTIVE
  char ain[8][16];
  const int16_t analog_value[8] = {
    snapshot->analog.ain1,
    snapshot->analog.ain2,
    snapshot->analog.ain3,
    snapshot->analog.ain4,
    snapshot->analog.ain5,
    snapshot->analog.ain6,
    snapshot->analog.ain7,
    snapshot->analog.ain8,
  };
  for (unsigned i = 0; i < 8; i++) debug_format_analog(ain[i], sizeof(ain[i]), i, analog_value[i]);
  ESP_LOGI(DEBUG_MONITOR_TAG,
    "ADS1115 | state=%s | AIN1=%s AIN2=%s AIN3=%s AIN4=%s AIN5=%s AIN6=%s AIN7=%s AIN8=%s",
    debug_component_state(ANALOG, snapshot->analog_valid), ain[0], ain[1], ain[2], ain[3], ain[4], ain[5],
    ain[6], ain[7]);
#else
  ESP_LOGI(DEBUG_MONITOR_TAG, "ADS1115 | DISABLED");
#endif

#if SENSOR_ENABLE_GYRO
  if (snapshot->gyroscope_valid) {
    ESP_LOGI(DEBUG_MONITOR_TAG, "MPU6500 raw | state=%s | Accel X=%d Y=%d Z=%d | Gyro X=%d Y=%d Z=%d",
      debug_component_state(GYRO, true), (int)snapshot->gyroscope.accel_x, (int)snapshot->gyroscope.accel_y,
      (int)snapshot->gyroscope.accel_z, (int)snapshot->gyroscope.gyro_x, (int)snapshot->gyroscope.gyro_y,
      (int)snapshot->gyroscope.gyro_z);
  } else {
    ESP_LOGI(DEBUG_MONITOR_TAG, "MPU6500 | %s", debug_component_state(GYRO, false));
  }
#else
  ESP_LOGI(DEBUG_MONITOR_TAG, "MPU6500 | DISABLED");
#endif

#if SENSOR_ENABLE_GPS
  bool gps_fix = snapshot->gps_fix &&
                 xTaskGetTickCount() - snapshot->gps_last_fix_tick <= DEBUG_GPS_FIX_TIMEOUT;
  if (snapshot->gps_position_valid) {
    double latitude  = gps_coordinate_degrees(snapshot->gps.latitude, snapshot->gps.lat_dir);
    double longitude = gps_coordinate_degrees(snapshot->gps.longitude, snapshot->gps.lon_dir);
    ESP_LOGI(DEBUG_MONITOR_TAG, "GPS | state=%s fix=%s data=%s latitude=%.6f longitude=%.6f speed=%.2f km/h",
      debug_component_state(GPS, true), gps_fix ? "YES" : "NO", gps_fix ? "LIVE" : "LAST_VALID", latitude,
      longitude, (double)snapshot->gps.speed / 100.0);
  } else {
    const char *gps_state = IS_ERROR(&logbuf.run, GPS) || IS_FATAL(&logbuf.run, GPS)
                              ? "ERROR"
                              : (snapshot->gps_online ? "NO_FIX" : "WAITING");
    ESP_LOGI(DEBUG_MONITOR_TAG, "GPS | state=%s fix=NO latitude=N/A longitude=N/A speed=N/A", gps_state);
  }
#else
  ESP_LOGI(DEBUG_MONITOR_TAG, "GPS | DISABLED");
#endif

#if SENSOR_ENABLE_CAN
  const char *can_state = IS_ERROR(&logbuf.run, CAN) || IS_FATAL(&logbuf.run, CAN)
                            ? "ERROR"
                            : "OK";

  if (snapshot->motor_seen) {
    const char *motor_state = snapshot->motor.motor_valid
                                ? "VALID"
                                : (snapshot->motor.motor_rpm_valid ? "PARTIAL" : "STALE");
    ESP_LOGI(DEBUG_MONITOR_TAG,
      "CAN | state=%s | Motor=%s RPM=%.0f rpm_valid=%s | Bus=%.1fV Current=%.1fA Phase=%.1fA",
      can_state, motor_state, (double)snapshot->motor.rpm,
      snapshot->motor.motor_rpm_valid ? "YES" : "NO",
      (double)snapshot->motor.bus_voltage, (double)snapshot->motor.bus_current,
      (double)snapshot->motor.phase_current);
    ESP_LOGI(DEBUG_MONITOR_TAG,
      "CAN MOTOR2 | Ctrl=%dC Motor=%dC Accel=%d Gear=%u Brake=%s Mode=%u DC=%s Err=%02X/%02X/%02X Life=%u",
      snapshot->motor.controller_temp, snapshot->motor.motor_temp, snapshot->motor.accelerator,
      (unsigned)snapshot->motor.gear, snapshot->motor.brake ? "ON" : "OFF",
      (unsigned)snapshot->motor.op_mode, snapshot->motor.dc_contactor ? "ON" : "OFF",
      (unsigned)snapshot->motor.err_byte4, (unsigned)snapshot->motor.err_byte5,
      (unsigned)snapshot->motor.err_byte6,
      (unsigned)snapshot->motor.life_signal);
  } else {
    ESP_LOGI(DEBUG_MONITOR_TAG,
      "CAN | state=%s | Motor=WAITING RPM=N/A | Bus=N/A Current=N/A Phase=N/A",
      can_state);
    ESP_LOGI(DEBUG_MONITOR_TAG,
      "CAN MOTOR2 | Ctrl=N/A Motor=N/A Accel=N/A Gear=N/A Brake=N/A Mode=N/A DC=N/A Err=N/A Life=N/A");
  }

  if (snapshot->bms_seen) {
    uint8_t valid_mask = 0;
    for (uint8_t i = 0; i < NUM_DALY_DATA_IDS; i++) {
      if (snapshot->bms.data_valid[i]) valid_mask |= (uint8_t)(1U << i);
    }
    const char *bms_state = valid_mask == 0xFFU ? "VALID" : (valid_mask != 0U ? "PARTIAL" : "STALE");

    ESP_LOGI(DEBUG_MONITOR_TAG,
      "BMS1 | state=%s cycle=%" PRIu32 " | Pack=%.1fV Gather=%.1fV Current=%.1fA SOC=%.1f%% | CellMax=%umV(#%u) CellMin=%umV(#%u) | TempMax=%dC(#%u) TempMin=%dC(#%u)",
      bms_state, snapshot->bms.cycle_count, (double)snapshot->bms.pack_voltage,
      (double)snapshot->bms.gather_voltage, (double)snapshot->bms.current, (double)snapshot->bms.soc,
      (unsigned)snapshot->bms.max_cell_voltage, (unsigned)snapshot->bms.max_cell_no,
      (unsigned)snapshot->bms.min_cell_voltage, (unsigned)snapshot->bms.min_cell_no,
      (int)snapshot->bms.max_temp, (unsigned)snapshot->bms.max_temp_sensor_no,
      (int)snapshot->bms.min_temp, (unsigned)snapshot->bms.min_temp_sensor_no);
    ESP_LOGI(DEBUG_MONITOR_TAG,
      "BMS2 | State=%u ChgMOS=%u DisMOS=%u Life=%u Remain=%" PRIu32 "mAh | Cells=%u Temps=%u Charger=%u Load=%u DIDO=0x%02X | Fault=%02X/%02X/%02X/%02X/%02X/%02X/%02X/%02X any=%s | valid=%02X",
      (unsigned)snapshot->bms.charge_state, (unsigned)snapshot->bms.charge_mos,
      (unsigned)snapshot->bms.discharge_mos, (unsigned)snapshot->bms.bms_life_cycles,
      snapshot->bms.remain_capacity, (unsigned)snapshot->bms.cell_string_count,
      (unsigned)snapshot->bms.temp_sensor_count, (unsigned)snapshot->bms.charger_connected,
      (unsigned)snapshot->bms.load_connected, (unsigned)snapshot->bms.di_do_flags,
      (unsigned)snapshot->bms.fault[0], (unsigned)snapshot->bms.fault[1],
      (unsigned)snapshot->bms.fault[2], (unsigned)snapshot->bms.fault[3],
      (unsigned)snapshot->bms.fault[4], (unsigned)snapshot->bms.fault[5],
      (unsigned)snapshot->bms.fault[6], (unsigned)snapshot->bms.fault[7],
      snapshot->bms.any_fault ? "YES" : "NO", (unsigned)valid_mask);
    ESP_LOGI(DEBUG_MONITOR_TAG,
      "BMS VALID | 90=%u 91=%u 92=%u 93=%u 94=%u 95=%u 96=%u 98=%u",
      (unsigned)snapshot->bms.data_valid[DALY_IDX_90],
      (unsigned)snapshot->bms.data_valid[DALY_IDX_91],
      (unsigned)snapshot->bms.data_valid[DALY_IDX_92],
      (unsigned)snapshot->bms.data_valid[DALY_IDX_93],
      (unsigned)snapshot->bms.data_valid[DALY_IDX_94],
      (unsigned)snapshot->bms.data_valid[DALY_IDX_95],
      (unsigned)snapshot->bms.data_valid[DALY_IDX_96],
      (unsigned)snapshot->bms.data_valid[DALY_IDX_98]);

    static uint32_t last_bms_detail_cycle;
    if (snapshot->bms.cycle_count > 0U && snapshot->bms.cycle_count != last_bms_detail_cycle) {
      debug_print_bms_cells(&snapshot->bms);
      debug_print_bms_temps(&snapshot->bms);
      last_bms_detail_cycle = snapshot->bms.cycle_count;
    }
  } else {
    ESP_LOGI(DEBUG_MONITOR_TAG, "BMS | WAITING");
  }
#else
  ESP_LOGI(DEBUG_MONITOR_TAG, "CAN | DISABLED");
#endif

#if SENSOR_ENABLE_DISPLAY
  const char *lcd_state = IS_ERROR(&logbuf.run, DISPLAY) || IS_FATAL(&logbuf.run, DISPLAY) ? "ERROR" : "OK";
  ESP_LOGI(DEBUG_MONITOR_TAG, "LCD | state=%s", lcd_state);
#else
  ESP_LOGI(DEBUG_MONITOR_TAG, "LCD | DISABLED");
#endif

  if (logqueue != NULL) {
    ESP_LOGI(DEBUG_MONITOR_TAG, "SD | mounted=%s logging=%s | logqueue waiting=%u",
      snapshot->sd_mounted ? "YES" : "NO", snapshot->sd_logging ? "YES" : "NO",
      (unsigned)uxQueueMessagesWaiting(logqueue));
  } else {
    ESP_LOGI(DEBUG_MONITOR_TAG, "SD | mounted=%s logging=NO | logqueue=N/A",
      snapshot->sd_mounted ? "YES" : "NO");
  }
}

void task_debug_monitor(void *pvParameters) {
  (void)pvParameters;
  TickType_t tick = xTaskGetTickCount();

  while (true) {
    vTaskDelayUntil(&tick, DEBUG_SENSOR_MONITOR_INTERVAL);

    debug_monitor_snapshot_t snapshot;
    portENTER_CRITICAL(&debug_snapshot_lock);
    snapshot = debug_snapshot;
    portEXIT_CRITICAL(&debug_snapshot_lock);

    debug_monitor_print(&snapshot);
  }
}

#endif  // DEBUG_SENSOR_MONITOR
