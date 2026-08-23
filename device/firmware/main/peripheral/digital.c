#include "main.h"

#include "driver/gpio.h"

// Ignore only pulses far shorter than the ~5 ms minimum expected at 60 km/h.
// Unlike the former switch debounce, rejected pulses do not move the baseline.
#define WHEEL_MIN_PULSE_INTERVAL_US 1000LL
#define WHEEL_STOP_TIMEOUT_US 500000LL
#define WHEEL_TASK_INTERVAL pdMS_TO_TICKS(10)

#define FRONT_WHEEL_PULSES_PER_REV 15U
#define REAR_WHEEL_PULSES_PER_REV 14U

typedef enum {
  WHEEL_FL,
  WHEEL_FR,
  WHEEL_RL,
  WHEEL_RR,
  WHEEL_COUNT,
} wheel_index_t;

typedef struct {
  volatile int64_t last_pulse_time_us;
  volatile uint32_t pulse_interval_us;
  volatile float wheel_speed_kmh;
  volatile uint32_t pulse_count;
} wheel_state_t;

typedef struct {
  int64_t last_pulse_time_us;
  uint32_t pulse_interval_us;
  float wheel_speed_kmh;
  uint32_t pulse_count;
} wheel_snapshot_t;

static const uint8_t wheel_pulses_per_revolution[WHEEL_COUNT] = {
  [WHEEL_FL] = FRONT_WHEEL_PULSES_PER_REV,
  [WHEEL_FR] = FRONT_WHEEL_PULSES_PER_REV,
  [WHEEL_RL] = REAR_WHEEL_PULSES_PER_REV,
  [WHEEL_RR] = REAR_WHEEL_PULSES_PER_REV,
};

static wheel_state_t wheel_states[WHEEL_COUNT];
static portMUX_TYPE wheel_state_lock = portMUX_INITIALIZER_UNLOCKED;

static inline bool wheel_enabled(wheel_index_t wheel) {
  return (SENSOR_WHEEL_MASK & (1U << wheel)) != 0;
}

static gpio_num_t wheel_gpio(wheel_index_t wheel) {
  static const gpio_num_t gpio_by_wheel[WHEEL_COUNT] = {
    [WHEEL_FL] = GPIO_NUM_11,
    [WHEEL_FR] = GPIO_NUM_12,
    [WHEEL_RL] = GPIO_NUM_13,
    [WHEEL_RR] = GPIO_NUM_14,
  };

  return gpio_by_wheel[wheel];
}

/*******************************************************************************
 * Wheel pulse ISR — timestamp/count only; calculation and logging stay in task
 ******************************************************************************/
static void digital_isr(void *arg) {
  wheel_state_t *wheel = (wheel_state_t *)arg;
  int64_t now_us       = esp_timer_get_time();
  int64_t previous_us;

  portENTER_CRITICAL_ISR(&wheel_state_lock);
  previous_us = wheel->last_pulse_time_us;

  if (previous_us == 0 || now_us - previous_us >= WHEEL_MIN_PULSE_INTERVAL_US) {
    int64_t interval_us = now_us - previous_us;

    // The first pulse (and the first after a stop) establishes a fresh baseline.
    wheel->pulse_interval_us  = previous_us != 0 && interval_us < WHEEL_STOP_TIMEOUT_US ? (uint32_t)interval_us : 0;
    wheel->last_pulse_time_us = now_us;
    wheel->pulse_count++;
  }

  portEXIT_CRITICAL_ISR(&wheel_state_lock);
}

static float wheel_speed_from_interval(uint32_t pulse_interval_us, uint8_t pulses_per_revolution) {
  float pulse_interval_sec = (float)pulse_interval_us / 1000000.0f;
  float wheel_rev_per_sec  = 1.0f / (pulse_interval_sec * (float)pulses_per_revolution);
  float wheel_speed_mps    = wheel_rev_per_sec * VEHICLE_TIRE_CIRC_M;

  return wheel_speed_mps * 3.6f;
}

static void digital_log_wheel_speeds(const float speeds[WHEEL_COUNT]) {
  logbuf.digital.payload.digital.wheel_speed_fl_kmh = speeds[WHEEL_FL];
  logbuf.digital.payload.digital.wheel_speed_fr_kmh = speeds[WHEEL_FR];
  logbuf.digital.payload.digital.wheel_speed_rl_kmh = speeds[WHEEL_RL];
  logbuf.digital.payload.digital.wheel_speed_rr_kmh = speeds[WHEEL_RR];
  LOG(LOG_TYPE_DIGITAL, &logbuf.digital);
}

static void digital_update_wheel_speeds(void) {
  static uint32_t processed_pulse_count[WHEEL_COUNT];
  wheel_snapshot_t snapshot[WHEEL_COUNT] = { 0 };
  float speeds[WHEEL_COUNT]              = { 0 };
  bool should_log = false;
  int64_t now_us  = esp_timer_get_time();

  // The lock makes the 64-bit timestamps coherent on the dual-core 32-bit MCU.
  portENTER_CRITICAL(&wheel_state_lock);
  for (int i = 0; i < WHEEL_COUNT; i++) {
    snapshot[i].last_pulse_time_us = wheel_states[i].last_pulse_time_us;
    snapshot[i].pulse_interval_us  = wheel_states[i].pulse_interval_us;
    snapshot[i].wheel_speed_kmh    = wheel_states[i].wheel_speed_kmh;
    snapshot[i].pulse_count        = wheel_states[i].pulse_count;
  }
  portEXIT_CRITICAL(&wheel_state_lock);

  for (int i = 0; i < WHEEL_COUNT; i++) {
    if (!wheel_enabled((wheel_index_t)i)) continue;

    speeds[i] = snapshot[i].wheel_speed_kmh;

    if (snapshot[i].pulse_count != processed_pulse_count[i]) {
      processed_pulse_count[i] = snapshot[i].pulse_count;
      should_log               = true;

      if (snapshot[i].pulse_interval_us != 0) {
        speeds[i] = wheel_speed_from_interval(snapshot[i].pulse_interval_us, wheel_pulses_per_revolution[i]);
      } else {
        speeds[i] = 0.0f;
      }
    }

    if (snapshot[i].last_pulse_time_us != 0 && now_us - snapshot[i].last_pulse_time_us >= WHEEL_STOP_TIMEOUT_US &&
        speeds[i] != 0.0f) {
      speeds[i]  = 0.0f;
      should_log = true;
    }
  }

  portENTER_CRITICAL(&wheel_state_lock);
  for (int i = 0; i < WHEEL_COUNT; i++) {
    wheel_states[i].wheel_speed_kmh = wheel_enabled((wheel_index_t)i) ? speeds[i] : 0.0f;
  }
  portEXIT_CRITICAL(&wheel_state_lock);

  if (should_log) {
    uint32_t pulse_counts[WHEEL_COUNT];
    for (int i = 0; i < WHEEL_COUNT; i++) pulse_counts[i] = snapshot[i].pulse_count;
    debug_monitor_publish_wheels(speeds, pulse_counts);
    digital_log_wheel_speeds(speeds);
  }
}

/*******************************************************************************
 * Wheel-speed input task
 ******************************************************************************/
void task_digital(void *pvParameters) {
  (void)pvParameters;
  gpio_config_t gpio = { 0 };

  uint64_t enabled_gpio_mask = 0;
  for (int i = 0; i < WHEEL_COUNT; i++) {
    if (wheel_enabled((wheel_index_t)i)) enabled_gpio_mask |= 1ULL << wheel_gpio((wheel_index_t)i);
  }

  gpio.pin_bit_mask = enabled_gpio_mask;
  gpio.mode         = GPIO_MODE_INPUT;
  gpio.intr_type    = GPIO_INTR_POSEDGE;
  gpio.pull_up_en   = GPIO_PULLUP_DISABLE;
  gpio.pull_down_en = GPIO_PULLDOWN_ENABLE;

  if (gpio_config(&gpio) != ESP_OK) {
    ERROR_SYSLOG(&init, DIGITAL, "GPIO init failure", "DGT_INIT_FAIL");
    CLEAR_FATAL(&logbuf.run, DIGITAL);
    SET_ERROR(&logbuf.run, DIGITAL);
    SYSLOG("SNS:WHL:ERR");
    vTaskDelete(NULL);
    return;
  }

  esp_err_t ret = ESP_OK;
  bool handler_added[WHEEL_COUNT] = { false };
  for (int i = 0; i < WHEEL_COUNT; i++) {
    if (!wheel_enabled((wheel_index_t)i)) continue;

    esp_err_t add_ret = gpio_isr_handler_add(wheel_gpio((wheel_index_t)i), digital_isr, &wheel_states[i]);
    if (add_ret == ESP_OK) {
      handler_added[i] = true;
    } else {
      ret = add_ret;
    }
  }

  if (ret != ESP_OK) {
    for (int i = 0; i < WHEEL_COUNT; i++) {
      if (handler_added[i]) gpio_isr_handler_remove(wheel_gpio((wheel_index_t)i));
    }
    ERROR_SYSLOG(&init, DIGITAL, "GPIO isr install failure", "DGT_ISR_FAIL");
    CLEAR_FATAL(&logbuf.run, DIGITAL);
    SET_ERROR(&logbuf.run, DIGITAL);
    SYSLOG("SNS:WHL:ERR");
    vTaskDelete(NULL);
    return;
  }

  if (IS_OK(&init, DIGITAL)) {
    CLEAR_ALL(&logbuf.run, DIGITAL);
    SYSLOG("DGT_RDY");
    SYSLOG("SNS:WHL:OK");
  } else {
    COPY_STATE(&logbuf.run, &init, DIGITAL);
  }

  const float stopped[WHEEL_COUNT] = { 0 };
  const uint32_t no_pulses[WHEEL_COUNT] = { 0 };
  debug_monitor_publish_wheels(stopped, no_pulses);
  digital_log_wheel_speeds(stopped);

  TickType_t tick = xTaskGetTickCount();

  while (true) {
    vTaskDelayUntil(&tick, WHEEL_TASK_INTERVAL);
    digital_update_wheel_speeds();
  }
}
