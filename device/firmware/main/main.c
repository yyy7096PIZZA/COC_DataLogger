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

const char components[][8] = { "CORE", "NVS", "I2C0", "SD", "CAN", "GPS", "ANALOG", "DIGITAL", "GYRO" };

/***** function prototypes *****/
void sdcard_init(void);
static void core_init(void);
static void nvs_init(void);
static void i2c0_init(void);
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
  /*** Core GPIO ***/
  core_init();

  /*** NVS ***/
  nvs_init();

  /*** shared I2C0 bus (gyroscope + display) ***/
  i2c0_init();

  /*** SDIO ***/
  sdcard_init();

  /*** peripherals ***/
  peripheral_task_init();

  SYSLOG("INIT_DONE");
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

  /*** GPIO ISR service (required by digital input ISRs in digital.c) ***/
  if (gpio_install_isr_service(0) != ESP_OK) {
    ERROR_LOG(&init, CORE, "ISR service install failure");
  }

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

  // set peripheral enabled default values
  if (nvs_get_u8(nvs, "can_en", &storage.enabled.can) != ESP_OK) {
    nvs_set_u8(nvs, "can_en", true);
    storage.enabled.can = true;
  }

  if (nvs_get_u8(nvs, "gps_en", &storage.enabled.gps) != ESP_OK) {
    nvs_set_u8(nvs, "gps_en", true);
    storage.enabled.gps = true;
  }

  if (nvs_get_u8(nvs, "anl_en", &storage.enabled.analog) != ESP_OK) {
    nvs_set_u8(nvs, "anl_en", true);
    storage.enabled.analog = true;
  }

  if (nvs_get_u8(nvs, "dgt_en", &storage.enabled.digital) != ESP_OK) {
    nvs_set_u8(nvs, "dgt_en", true);
    storage.enabled.digital = true;
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
 * init shared I2C0 bus (gyroscope + display) and timezone
 ******************************************************************************/
static void i2c0_init(void) {
  // set timezone
  setenv("TZ", "UTC", 1);
  tzset();

  // create the I2C0 master bus; gyroscope.c and display.c retrieve this handle
  // via i2c_master_get_bus_handle(I2C_NUM_0), so this bus must always be created
  i2c_master_bus_handle_t i2c0;
  i2c_master_bus_config_t i2c_config = {
    .clk_source                   = I2C_CLK_SRC_DEFAULT,
    .i2c_port                     = I2C_NUM_0,
    .scl_io_num                   = GPIO_NUM_10,
    .sda_io_num                   = GPIO_NUM_9,
    .glitch_ignore_cnt            = 7,
    .flags.enable_internal_pullup = false,
  };

  if (i2c_new_master_bus(&i2c_config, &i2c0) != ESP_OK) {
    ERROR_LOG(&init, I2C0, "I2C init failure");
  }

  if (IS_OK(&init, I2C0)) {
    CLEAR_ALL(&logbuf.run, I2C0);
  } else {
    COPY_STATE(&logbuf.run, &init, I2C0);
  }

  // read boot time (may be 0 until GPS sets the clock in a later step)
  gettimeofday(&boot, NULL);
}

/*******************************************************************************
 create peripheral recorder tasks
 ******************************************************************************/
static void peripheral_task_init(void) {
  /***** CAN *****/
  if (storage.enabled.can) {
    if (xTaskCreate(task_can, "can", 4096, NULL, 4, NULL) != pdPASS) {
      ERROR_SYSLOG(&init, CORE, "CAN task create failure", "CAN_TASK_FAIL");
    }
  } else {
    CLEAR_ALL(&logbuf.run, CAN);
  }

  /***** GPS *****/
  if (storage.enabled.gps) {
    if (xTaskCreate(task_gps, "gps", 4096, NULL, 5, NULL) != pdPASS) {
      ERROR_SYSLOG(&init, CORE, "GPS task create failure", "GPS_TASK_FAIL");
    }
  } else {
    CLEAR_ALL(&logbuf.run, GPS);
  }

  /***** ANALOG *****/
  if (storage.enabled.analog) {
    if (xTaskCreate(task_analog, "analog", 4096, NULL, 5, NULL) != pdPASS) {
      ERROR_SYSLOG(&init, CORE, "ANALOG task create failure", "ANL_TASK_FAIL");
    }
  } else {
    CLEAR_ALL(&logbuf.run, ANALOG);
  }

  /***** DIGITAL *****/
  if (storage.enabled.digital) {
    if (xTaskCreate(task_digital, "digital", 4096, NULL, 5, NULL) != pdPASS) {
      ERROR_SYSLOG(&init, CORE, "DIGITAL task create failure", "DGT_TASK_FAIL");
    }
  } else {
    CLEAR_ALL(&logbuf.run, DIGITAL);
  }

  /***** GYROSCOPE (always enabled) *****/
  if (xTaskCreate(task_gyroscope, "gyroscope", 4096, NULL, 5, NULL) != pdPASS) {
    ERROR_SYSLOG(&init, CORE, "GYROSCOPE task create failure", "GYR_TASK_FAIL");
  }

  /***** DISPLAY (always enabled, self-deactivates if PCF8574 absent) *****/
  if (xTaskCreate(task_display, "display", 4096, NULL, 2, NULL) != pdPASS) {
    ESP_LOGW("CORE", "DISPLAY task create failure");
  }
}
