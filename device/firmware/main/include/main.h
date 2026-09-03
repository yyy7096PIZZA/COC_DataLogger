#ifndef MAIN_H
#define MAIN_H

#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"

#include "config.h"

/***** shared global variables *****/
extern nvs_handle_t nvs;
extern TaskHandle_t led;
extern QueueHandle_t logqueue;
extern volatile uint64_t boot_time_fixup_epoch;  // GPS→SD: corrected boot epoch (seconds); 0 = none yet

/***** nvs storage *****/
typedef struct {
  uint8_t mac[6];        // filled by esp_read_mac(); written into the BOOT record
  char tz[40];           // local timezone, used for the SD log filename
  // Runtime mirror of the authoritative compile-time sensor configuration.
  // The legacy NVS can_en/gps_en/anl_en/dgt_en keys are not read or changed.
  struct {
    uint8_t can;
    uint8_t gps;
    uint8_t analog;
    uint8_t digital;
  } enabled;
  struct {
    uint8_t bps;
    uint8_t _reserved[3];
    uint32_t filter;
    uint32_t mask;
  } can;
  struct {
    uint8_t dev;
    uint8_t _reserved[3];
  } gps;
} nvs_storage_t;

extern nvs_storage_t storage;

/***** system state *****/
typedef uint32_t state_t;
extern state_t init;
extern const char components[][8];

typedef enum {
  CORE,
  NVS,
  I2C0,  // shared I2C0 bus (gyroscope + display) init state; formerly the RTC slot
  SD,
  CAN,
  GPS,
  ANALOG,
  DIGITAL,
  GYRO,
  DISPLAY,
  COMPONENT_MAX = 12,     // FATAL bits live at (component + COMPONENT_MAX); must exceed the max index (9)
  COMPONENT_ALL = 0x03FF,  // 10 components -> error bits 0-9
} state_component_t;

#define ALL_ERROR_FATAL (COMPONENT_ALL | (COMPONENT_ALL << COMPONENT_MAX))

typedef enum {
  STATE_OK    = pdMS_TO_TICKS(1000),
  STATE_ERROR = pdMS_TO_TICKS(250),
  STATE_FATAL = pdMS_TO_TICKS(100),
} state_led_interval_t;

// LED 태스크 생성 자체가 실패한 경우 led == NULL — 그 상태로 xTaskAbortDelay(NULL)를
// 부르면 assert로 리셋되므로, 상태 변경은 그대로 두고 LED 갱신만 건너뛴다
static inline void led_refresh(void) {
  if (led != NULL) xTaskAbortDelay(led);
}

static inline void SET_ERROR(state_t *state, state_component_t component) {
  *state |= (1 << component);
  led_refresh();
}

static inline void SET_FATAL(state_t *state, state_component_t component) {
  *state |= (1 << (component + COMPONENT_MAX));
  led_refresh();
}

static inline void CLEAR_ERROR(state_t *state, state_component_t component) {
  *state &= ~(1 << component);
  led_refresh();
}

static inline void CLEAR_FATAL(state_t *state, state_component_t component) {
  *state &= ~(1 << (component + COMPONENT_MAX));
  led_refresh();
}

static inline void CLEAR_ALL(state_t *state, state_component_t component) {
  *state &= ~((1 << component) | (1 << (component + COMPONENT_MAX)));
  led_refresh();
}

static inline void COPY_STATE(state_t *dest, state_t *src, state_component_t component) {
  *dest = (*dest & ~(1 << component)) | (*src & (1 << component));
  *dest = (*dest & ~(1 << (component + COMPONENT_MAX))) | (*src & (1 << (component + COMPONENT_MAX)));
  led_refresh();
}

#define IS_ERROR(state, component) (*state & (1 << component))
#define IS_FATAL(state, component) (*state & (1 << (component + COMPONENT_MAX)))
#define IS_OK(state, component) (!IS_ERROR(state, component) && !IS_FATAL(state, component))

/***** log protocol (wire/storage data layout — see protocol.h) *****/
#include "protocol.h"

/***** decoded EZkontrol motor runtime state (not part of the SD log protocol) *****/
typedef struct {
  float bus_voltage;
  float bus_current;
  float phase_current;
  float rpm;
  int controller_temp;
  int motor_temp;
  int accelerator;
  uint8_t gear;
  bool brake;
  uint8_t op_mode;
  bool dc_contactor;
  uint8_t err_byte4;
  uint8_t err_byte5;
  uint8_t err_byte6;
  uint8_t life_signal;
  TickType_t last_motor_rx;
  TickType_t motor_msg1_tick;
  bool motor_msg1_ready;
  bool motor_valid;
  bool motor_rpm_valid;
} motor_runtime_t;

typedef enum {
  DALY_IDX_90,
  DALY_IDX_91,
  DALY_IDX_92,
  DALY_IDX_93,
  DALY_IDX_94,
  DALY_IDX_95,
  DALY_IDX_96,
  DALY_IDX_98,
  NUM_DALY_DATA_IDS,
} daly_data_index_t;

typedef struct {
  float pack_voltage;
  float gather_voltage;
  float current;
  float soc;
  uint16_t max_cell_voltage;
  uint8_t max_cell_no;
  uint16_t min_cell_voltage;
  uint8_t min_cell_no;
  int8_t max_temp;
  uint8_t max_temp_sensor_no;
  int8_t min_temp;
  uint8_t min_temp_sensor_no;
  uint8_t charge_state;
  uint8_t charge_mos;
  uint8_t discharge_mos;
  uint8_t bms_life_cycles;
  uint32_t remain_capacity;
  uint8_t cell_string_count;
  uint8_t temp_sensor_count;
  uint8_t charger_connected;
  uint8_t load_connected;
  uint8_t di_do_flags;
  uint16_t cell_voltage[DALY_MAX_CELLS];
  uint8_t cell_voltage_count;
  int8_t cell_temp[DALY_MAX_TEMPS];
  uint8_t cell_temp_count;
  uint8_t fault[8];
  bool any_fault;
  bool data_valid[NUM_DALY_DATA_IDS];
  uint32_t cycle_count;
  TickType_t last_cycle_tick;
} bms_runtime_t;

/***** optional shared sensor snapshot for the 1 Hz debug monitor *****/
#if DEBUG_SENSOR_MONITOR
void debug_monitor_publish_wheels(const float speed_kmh[4], const uint32_t pulse_count[4]);
void debug_monitor_publish_analog(const analog_record_t *sample);
void debug_monitor_publish_gyroscope(const gyroscope_record_t *sample);
void debug_monitor_publish_gps(const gps_record_t *sample, bool fix);
void debug_monitor_publish_gps_fix(bool fix);
void debug_monitor_publish_gps_online(bool online);
void debug_monitor_publish_motor(const motor_runtime_t *sample);
void debug_monitor_publish_bms(const bms_runtime_t *sample);
void debug_monitor_publish_sd_status(bool mounted, bool logging);
void task_debug_monitor(void *pvParameters);
#else
static inline void debug_monitor_publish_wheels(const float speed_kmh[4], const uint32_t pulse_count[4]) {
  (void)speed_kmh;
  (void)pulse_count;
}
static inline void debug_monitor_publish_analog(const analog_record_t *sample) { (void)sample; }
static inline void debug_monitor_publish_gyroscope(const gyroscope_record_t *sample) { (void)sample; }
static inline void debug_monitor_publish_gps(const gps_record_t *sample, bool fix) {
  (void)sample;
  (void)fix;
}
static inline void debug_monitor_publish_gps_fix(bool fix) { (void)fix; }
static inline void debug_monitor_publish_gps_online(bool online) { (void)online; }
static inline void debug_monitor_publish_motor(const motor_runtime_t *sample) { (void)sample; }
static inline void debug_monitor_publish_bms(const bms_runtime_t *sample) { (void)sample; }
static inline void debug_monitor_publish_sd_status(bool mounted, bool logging) {
  (void)mounted;
  (void)logging;
}
#endif

typedef struct {
  state_t run;      // component OK/ERROR/FATAL bitmap (see state_component_t)
  log_t digital;    // last logged FL/FR/RL/RR wheel speeds, owned by task_digital (digital.c)
} log_buf_t;

extern log_buf_t logbuf;

static inline void log_prepare(uint8_t type, log_t *log) {
  uint32_t *ptr   = (uint32_t *)log;
  uint32_t chksum = 0;

  // set log header
  log->magic     = LOG_MAGIC;
  log->type      = type;
  log->checksum  = 0;
  log->timestamp = (uint32_t)(esp_timer_get_time() / 1000);

  // calculate checksum
  for (size_t i = 0; i < sizeof(log_t) / sizeof(uint32_t); i++) {
    chksum ^= ptr[i];
  }

  // fold to 16 bit
  log->checksum = (chksum & 0xFFFF) + (chksum >> 16);
}

static inline int LOG(uint8_t type, log_t *log) {
  if (logqueue == NULL) return 0;
  log_prepare(type, log);
  return xQueueSend(logqueue, log, 0);
}

static inline void SYSLOG(const char *msg) {
  if (logqueue == NULL) return;
  log_t log;
  strncpy(log.payload.system_event.msg, msg, sizeof(log.payload.system_event.msg));  // no need to null-terminate
  LOG(LOG_TYPE_SYSTEM, &log);
}

// sensor and status must be string literals; resulting SYSTEM payloads must fit
// the protocol's fixed 16-byte message field (for example: SNS:ADS48:MISS).
#define SENSOR_CFG_SYSLOG(sensor, enabled)                                      \
  do {                                                                          \
    _Static_assert(sizeof("CFG:" sensor ":ON") - 1 <= sizeof(system_event_t),   \
      "sensor configuration ON event exceeds 16-byte payload");                \
    _Static_assert(sizeof("CFG:" sensor ":OFF") - 1 <= sizeof(system_event_t),  \
      "sensor configuration OFF event exceeds 16-byte payload");               \
    SYSLOG((enabled) ? "CFG:" sensor ":ON" : "CFG:" sensor ":OFF");           \
  } while (0)

#define SENSOR_STATUS_SYSLOG(sensor, status)                                    \
  do {                                                                          \
    _Static_assert(sizeof("SNS:" sensor ":" status) - 1 <= sizeof(system_event_t), \
      "sensor status event exceeds 16-byte payload");                          \
    SYSLOG("SNS:" sensor ":" status);                                         \
  } while (0)

static inline void ERROR_LOG(state_t *state, state_component_t component, const char *msg) {
  SET_ERROR(state, component);
  ESP_LOGW(components[component], "%s", msg);
}

static inline void FATAL_LOG(state_t *state, state_component_t component, const char *msg) {
  SET_FATAL(state, component);
  ESP_LOGE(components[component], "%s", msg);
}

static inline void ERROR_SYSLOG(state_t *state, state_component_t component, const char *msg, const char *log) {
  SYSLOG(log);
  ERROR_LOG(state, component, msg);
}

static inline void FATAL_SYSLOG(state_t *state, state_component_t component, const char *msg, const char *log) {
  SYSLOG(log);
  FATAL_LOG(state, component, msg);
}

/***** utility functions *****/
#define INFO(component, fmt, ...) ESP_LOGI(components[component], fmt, ##__VA_ARGS__)

/***** display CAN data snapshot (written by task_can, read by task_display) *****/
typedef struct {
  uint16_t   ez_rpm_raw;     // EZ 0x180117EF B6-B7 LE; rpm = raw - 32000
  uint16_t   bms_soc_raw;    // Daly 0x18904001 B6-B7 BE; SOC % = raw * 0.1
  uint8_t    ez_rpm_valid;
  uint8_t    bms_soc_valid;
  TickType_t ez_rpm_tick;
  TickType_t bms_soc_tick;
} display_can_t;

extern volatile display_can_t display_can;

#endif  // MAIN_H
