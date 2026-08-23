#include <sys/time.h>
#include <time.h>

#include <stdbool.h>

#include "main.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_mac.h"
#include "led_strip.h"
#include "nvs_flash.h"

/***** global variables *****/
struct timeval boot;
nvs_handle_t nvs;
TaskHandle_t led;
QueueHandle_t logqueue;
volatile uint64_t boot_time_fixup_epoch = 0;

log_buf_t logbuf;
nvs_storage_t storage;

state_t init = 0;

const char components[][8] = {
  "CORE", "NVS", "I2C0", "SD", "CAN", "GPS", "ANALOG", "DIGITAL", "GYRO", "DISPLAY"
};

/***** function prototypes *****/
void sdcard_init(void);
static void timebase_init(void);
static void core_init(void);
static void nvs_init(void);
#if SENSOR_ENABLE_GYRO || SENSOR_ENABLE_DISPLAY
static void i2c0_init(void);
#endif
static void sensor_config_log(void);
static void peripheral_task_init(void);

void task_can(void *pvParameters);
void task_gps(void *pvParameters);
void task_analog(void *pvParameters);
void task_digital(void *pvParameters);
void task_gyroscope(void *pvParameters);
void task_display(void *pvParameters);
static void task_led(void *pvParameters);

/*******************************************************************************
 * main application entry point
 ******************************************************************************/
void app_main(void) {
  logbuf.run = ALL_ERROR_FATAL;

  /*** Time base (independent of all optional peripherals) ***/
  timebase_init();

  /*** Core GPIO ***/
  core_init();

  /*** NVS ***/
  nvs_init();

  /*** shared I2C0 bus (gyroscope + display) ***/
#if SENSOR_ENABLE_GYRO || SENSOR_ENABLE_DISPLAY
  i2c0_init();
#else
  CLEAR_ALL(&logbuf.run, I2C0);
#endif

  /*** SDIO ***/
  sdcard_init();

  // sdcard_init() queues BOOT first. Keep all effective configuration records
  // immediately behind it and ahead of any sample-producing sensor task.
  sensor_config_log();

  /*** peripherals ***/
  peripheral_task_init();

  SYSLOG("INIT_DONE");
}

/*******************************************************************************
 * initialize the process timezone and capture boot time independently of I2C0
 ******************************************************************************/
static void timebase_init(void) {
  setenv("TZ", "UTC", 1);
  tzset();

  // May be near epoch until GPS supplies absolute time in a later step.
  gettimeofday(&boot, NULL);
}

/*******************************************************************************
 * system status LED indicator
 ******************************************************************************/
static void task_led(void *pvParameters) {
  uint32_t led_state            = true;
  state_led_interval_t interval = STATE_OK;

  while (true) {
    if (logbuf.run & (COMPONENT_ALL << COMPONENT_MAX)) {
      interval = STATE_FATAL;
    } else if (logbuf.run & COMPONENT_ALL) {
      interval = STATE_ERROR;
    } else {
      interval = STATE_OK;
    }

    led_state = !led_state;
    gpio_set_level(GPIO_NUM_5, led_state);

    vTaskDelay(interval);
  }
}

/*******************************************************************************
 * init core GPIO and LED task
 ******************************************************************************/
static void core_init(void) {
  gpio_config_t gpio;

  /*** WS2812B (DevKitC-1 v1.1 onboard RGB, GPIO38) — turn off at boot ***/
  {
    led_strip_config_t strip_cfg = {
      .strip_gpio_num = 38,
      .max_leds       = 1,
    };
    led_strip_rmt_config_t rmt_cfg = {
      .resolution_hz = 10 * 1000 * 1000,
    };
    led_strip_handle_t strip;
    if (led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &strip) == ESP_OK) {
      led_strip_clear(strip);
    }
  }

  /*** LED ***/
  gpio.pin_bit_mask = (1ULL << GPIO_NUM_5);
  gpio.mode         = GPIO_MODE_OUTPUT_OD;
  gpio.intr_type    = GPIO_INTR_DISABLE;
  gpio.pull_up_en   = GPIO_PULLUP_DISABLE;
  gpio.pull_down_en = GPIO_PULLDOWN_DISABLE;

  if (gpio_config(&gpio) != ESP_OK || xTaskCreate(task_led, "led", 2048, NULL, 5, &led) != pdPASS) {
    ERROR_LOG(&init, CORE, "LED config failure");
  }

  /*** GPIO ISR service (required only when a wheel input is enabled) ***/
#if SENSOR_DIGITAL_ACTIVE
  if (gpio_install_isr_service(0) != ESP_OK) {
    ERROR_LOG(&init, CORE, "ISR service install failure");
  }
#endif

  if (IS_OK(&init, CORE)) {
    CLEAR_ALL(&logbuf.run, CORE);
  } else {
    COPY_STATE(&logbuf.run, &init, CORE);
  }
}

/*******************************************************************************
 * init NVS and set default values
 ******************************************************************************/
static void nvs_init(void) {
  // Compile-time selection is authoritative. Keep a runtime mirror for code
  // that consumes storage, but leave legacy *_en NVS keys untouched.
  storage.enabled.can     = SENSOR_ENABLE_CAN;
  storage.enabled.gps     = SENSOR_ENABLE_GPS;
  storage.enabled.analog  = SENSOR_ANALOG_ACTIVE;
  storage.enabled.digital = SENSOR_DIGITAL_ACTIVE;

  if (nvs_flash_init() != ESP_OK) {
    FATAL_LOG(&init, NVS, "NVS flash init failure");
    goto finish;
  }

  if (nvs_open("storage", NVS_READWRITE, &nvs) != ESP_OK) {
    FATAL_LOG(&init, NVS, "open failure");
    goto finish;
  }

  // read MAC (unique device ID written into the BOOT record)
  esp_read_mac(storage.mac, ESP_MAC_WIFI_STA);

  // set device timezone default value (used for the SD log filename)
  size_t len = sizeof(storage.tz);

  if (nvs_get_str(nvs, "tz", storage.tz, &len) != ESP_OK) {
    snprintf(storage.tz, sizeof(storage.tz), "KST-9");
    nvs_set_str(nvs, "tz", storage.tz);
  }

  // set CAN default values
  if (nvs_get_u8(nvs, "can_bps", &storage.can.bps) != ESP_OK) {
    nvs_set_u8(nvs, "can_bps", CAN_BPS_250K);
    storage.can.bps = CAN_BPS_250K;
  }

  if (nvs_get_u32(nvs, "can_filter", &storage.can.filter) != ESP_OK) {
    nvs_set_u32(nvs, "can_filter", 0x00000000);
    storage.can.filter = 0x00000000;  // accept all
  }

  if (nvs_get_u32(nvs, "can_mask", &storage.can.mask) != ESP_OK) {
    nvs_set_u32(nvs, "can_mask", 0xFFFFFFFF);
    storage.can.mask = 0xFFFFFFFF;  // accept all
  }

  // set GPS default values
  if (nvs_get_u8(nvs, "gps_dev", &storage.gps.dev) != ESP_OK) {
    nvs_set_u8(nvs, "gps_dev", GPS_DEV_UBLOX);
    storage.gps.dev = GPS_DEV_UBLOX;
  }

  // commit changes
  if (nvs_commit(nvs) != ESP_OK) {
    ERROR_SYSLOG(&logbuf.run, NVS, "commit failure", "NVS_COMMIT_FAIL");
  }

finish:
  if (IS_OK(&init, NVS)) {
    CLEAR_ALL(&logbuf.run, NVS);
  } else {
    COPY_STATE(&logbuf.run, &init, NVS);
  }
}

/*******************************************************************************
 * init shared I2C0 bus (only when gyroscope and/or display is enabled)
 ******************************************************************************/
#if SENSOR_ENABLE_GYRO || SENSOR_ENABLE_DISPLAY
static void i2c0_init(void) {
  // create the I2C0 master bus; gyroscope.c and display.c retrieve this handle
  // via i2c_master_get_bus_handle(I2C_NUM_0).
  i2c_master_bus_handle_t i2c0;
  i2c_master_bus_config_t i2c_config = {
    .clk_source                   = I2C_CLK_SRC_DEFAULT,
    .i2c_port                     = I2C_NUM_0,
    .scl_io_num                   = GPIO_NUM_10,
    .sda_io_num                   = GPIO_NUM_9,
    .glitch_ignore_cnt            = 7,
    // Keep an enabled-but-unplugged optional bus at a defined idle level. The
    // weak internal pull-ups coexist with the stronger module-board pull-ups.
    .flags.enable_internal_pullup = true,
  };

  if (i2c_new_master_bus(&i2c_config, &i2c0) != ESP_OK) {
    ERROR_LOG(&init, I2C0, "I2C init failure");
  }

  if (IS_OK(&init, I2C0)) {
    CLEAR_ALL(&logbuf.run, I2C0);
  } else {
    COPY_STATE(&logbuf.run, &init, I2C0);
  }
}
#endif

/*******************************************************************************
 * record the authoritative sensor configuration after BOOT and before samples
 ******************************************************************************/
static void sensor_config_log(void) {
  SENSOR_CFG_SYSLOG("CAN", SENSOR_ENABLE_CAN);
  SENSOR_CFG_SYSLOG("GPS", SENSOR_ENABLE_GPS);
  SENSOR_CFG_SYSLOG("GYR", SENSOR_ENABLE_GYRO);
  SENSOR_CFG_SYSLOG("LCD", SENSOR_ENABLE_DISPLAY);
  SENSOR_CFG_SYSLOG("ADS48", SENSOR_ENABLE_ADS48);
  SENSOR_CFG_SYSLOG("ADS49", SENSOR_ENABLE_ADS49);

  SENSOR_CFG_SYSLOG("AIN1", SENSOR_EFFECTIVE_ANALOG_MASK & (1U << 0));
  SENSOR_CFG_SYSLOG("AIN2", SENSOR_EFFECTIVE_ANALOG_MASK & (1U << 1));
  SENSOR_CFG_SYSLOG("AIN3", SENSOR_EFFECTIVE_ANALOG_MASK & (1U << 2));
  SENSOR_CFG_SYSLOG("AIN4", SENSOR_EFFECTIVE_ANALOG_MASK & (1U << 3));
  SENSOR_CFG_SYSLOG("AIN5", SENSOR_EFFECTIVE_ANALOG_MASK & (1U << 4));
  SENSOR_CFG_SYSLOG("AIN6", SENSOR_EFFECTIVE_ANALOG_MASK & (1U << 5));
  SENSOR_CFG_SYSLOG("AIN7", SENSOR_EFFECTIVE_ANALOG_MASK & (1U << 6));
  SENSOR_CFG_SYSLOG("AIN8", SENSOR_EFFECTIVE_ANALOG_MASK & (1U << 7));

  SENSOR_CFG_SYSLOG("WHL_FL", SENSOR_EFFECTIVE_WHEEL_MASK & (1U << 0));
  SENSOR_CFG_SYSLOG("WHL_FR", SENSOR_EFFECTIVE_WHEEL_MASK & (1U << 1));
  SENSOR_CFG_SYSLOG("WHL_RL", SENSOR_EFFECTIVE_WHEEL_MASK & (1U << 2));
  SENSOR_CFG_SYSLOG("WHL_RR", SENSOR_EFFECTIVE_WHEEL_MASK & (1U << 3));
}

/*******************************************************************************
 create peripheral recorder tasks
 ******************************************************************************/
static void peripheral_task_init(void) {
  /***** CAN *****/
#if SENSOR_ENABLE_CAN
  if (xTaskCreate(task_can, "can", 4096, NULL, 4, NULL) != pdPASS) {
    ERROR_SYSLOG(&init, CORE, "CAN task create failure", "CAN_TASK_FAIL");
    CLEAR_FATAL(&logbuf.run, CAN);
    SET_ERROR(&logbuf.run, CAN);
    SENSOR_STATUS_SYSLOG("CAN", "ERR");
  }
#else
  CLEAR_ALL(&logbuf.run, CAN);
#endif

  /***** GPS *****/
#if SENSOR_ENABLE_GPS
  if (xTaskCreate(task_gps, "gps", 4096, NULL, 5, NULL) != pdPASS) {
    ERROR_SYSLOG(&init, CORE, "GPS task create failure", "GPS_TASK_FAIL");
    CLEAR_FATAL(&logbuf.run, GPS);
    SET_ERROR(&logbuf.run, GPS);
    SENSOR_STATUS_SYSLOG("GPS", "ERR");
  }
#else
  CLEAR_ALL(&logbuf.run, GPS);
#endif

  /***** ANALOG *****/
#if SENSOR_ANALOG_ACTIVE
  if (xTaskCreate(task_analog, "analog", 4096, NULL, 5, NULL) != pdPASS) {
    ERROR_SYSLOG(&init, CORE, "ANALOG task create failure", "ANL_TASK_FAIL");
    CLEAR_FATAL(&logbuf.run, ANALOG);
    SET_ERROR(&logbuf.run, ANALOG);
#if SENSOR_ADS48_ACTIVE
    SENSOR_STATUS_SYSLOG("ADS48", "ERR");
#endif
#if SENSOR_ADS49_ACTIVE
    SENSOR_STATUS_SYSLOG("ADS49", "ERR");
#endif
  }
#else
  CLEAR_ALL(&logbuf.run, ANALOG);
#endif

  /***** DIGITAL *****/
#if SENSOR_DIGITAL_ACTIVE
  if (xTaskCreate(task_digital, "digital", 4096, NULL, 5, NULL) != pdPASS) {
    ERROR_SYSLOG(&init, CORE, "DIGITAL task create failure", "DGT_TASK_FAIL");
    CLEAR_FATAL(&logbuf.run, DIGITAL);
    SET_ERROR(&logbuf.run, DIGITAL);
    SENSOR_STATUS_SYSLOG("WHL", "ERR");
  }
#else
  CLEAR_ALL(&logbuf.run, DIGITAL);
#endif

  /***** GYROSCOPE *****/
#if SENSOR_ENABLE_GYRO
  if (xTaskCreate(task_gyroscope, "gyroscope", 4096, NULL, 5, NULL) != pdPASS) {
    ERROR_SYSLOG(&init, CORE, "GYROSCOPE task create failure", "GYR_TASK_FAIL");
    CLEAR_FATAL(&logbuf.run, GYRO);
    SET_ERROR(&logbuf.run, GYRO);
    SENSOR_STATUS_SYSLOG("GYR", "ERR");
  }
#else
  CLEAR_ALL(&logbuf.run, GYRO);
#endif

  /***** DISPLAY *****/
#if SENSOR_ENABLE_DISPLAY
  if (xTaskCreate(task_display, "display", 4096, NULL, 2, NULL) != pdPASS) {
    CLEAR_FATAL(&logbuf.run, DISPLAY);
    ERROR_LOG(&logbuf.run, DISPLAY, "task create failure");
    SENSOR_STATUS_SYSLOG("LCD", "ERR");
  }
#else
  CLEAR_ALL(&logbuf.run, DISPLAY);
#endif

#if DEBUG_SENSOR_MONITOR
  /***** 1 Hz serial-only sensor snapshot monitor *****/
  if (xTaskCreate(task_debug_monitor, "sensor_debug", 4096, NULL, 1, NULL) != pdPASS) {
    ESP_LOGW("CORE", "SENSOR DEBUG task create failure");
  }
#endif
}
