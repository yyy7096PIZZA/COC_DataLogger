#include "main.h"

#include "driver/gpio.h"

// 기계식 스위치는 한 번 눌러도 접점이 수 ms 동안 수십 번 튕긴다(채터링).
// 엣지마다 그대로 기록하면 로그 큐/SD가 쓰레기 레코드로 넘치므로:
//   ISR   — 마지막 엣지 시각만 남기고 태스크를 깨운다 (기록하지 않음)
//   task  — 버스트 첫 엣지에 즉시 1회 기록(지연 최소화), 이후 신호가
//           DEBOUNCE_QUIET_MS 동안 조용해지면 안정된 최종 상태를 기록
// 기록되는 레코드 자체는 기존과 동일한 4채널 상태 스냅샷 — 로그 형식 불변.
#define DEBOUNCE_QUIET_MS 10

static TaskHandle_t digital_task;
static volatile TickType_t last_edge_tick;  // 1kHz tick == 1ms; 32-bit write is atomic

static void digital_isr(void *arg) {
  last_edge_tick = xTaskGetTickCountFromISR();

  BaseType_t woken = pdFALSE;
  vTaskNotifyGiveFromISR(digital_task, &woken);
  portYIELD_FROM_ISR(woken);
}

/* read all four inputs; log only when the state differs from the last logged one */
static void digital_sample_and_log(bool force) {
  uint32_t din1 = gpio_get_level(GPIO_NUM_11);
  uint32_t din2 = gpio_get_level(GPIO_NUM_12);
  uint32_t din3 = gpio_get_level(GPIO_NUM_13);
  uint32_t din4 = gpio_get_level(GPIO_NUM_14);

  if (!force && logbuf.digital.payload.digital.din1 == din1 && logbuf.digital.payload.digital.din2 == din2 &&
      logbuf.digital.payload.digital.din3 == din3 && logbuf.digital.payload.digital.din4 == din4) {
    return;
  }

  logbuf.digital.payload.digital.din1 = din1;
  logbuf.digital.payload.digital.din2 = din2;
  logbuf.digital.payload.digital.din3 = din3;
  logbuf.digital.payload.digital.din4 = din4;
  LOG(LOG_TYPE_DIGITAL, &logbuf.digital);
}

/*******************************************************************************
 * Digital input interrupt monitor task
 ******************************************************************************/
void task_digital(void *pvParameters) {
  gpio_config_t gpio;

  gpio.pin_bit_mask = (1ULL << GPIO_NUM_11) | (1ULL << GPIO_NUM_12) | (1ULL << GPIO_NUM_13) | (1ULL << GPIO_NUM_14);
  gpio.mode         = GPIO_MODE_INPUT;
  gpio.intr_type    = GPIO_INTR_ANYEDGE;
  gpio.pull_up_en   = GPIO_PULLUP_DISABLE;
  gpio.pull_down_en = GPIO_PULLDOWN_ENABLE;

  if (gpio_config(&gpio) != ESP_OK) {
    ERROR_SYSLOG(&init, DIGITAL, "GPIO init failure", "DGT_INIT_FAIL");
  }

  // record initial state
  digital_sample_and_log(true);

  digital_task = xTaskGetCurrentTaskHandle();

  esp_err_t ret = gpio_isr_handler_add(GPIO_NUM_11, digital_isr, NULL);
  ret |= gpio_isr_handler_add(GPIO_NUM_12, digital_isr, NULL);
  ret |= gpio_isr_handler_add(GPIO_NUM_13, digital_isr, NULL);
  ret |= gpio_isr_handler_add(GPIO_NUM_14, digital_isr, NULL);

  if (ret != ESP_OK) {
    ERROR_SYSLOG(&init, DIGITAL, "GPIO isr install failure", "DGT_ISR_FAIL");
  }

  if (IS_OK(&init, DIGITAL)) {
    CLEAR_ALL(&logbuf.run, DIGITAL);
    SYSLOG("DGT_RDY");
  } else {
    COPY_STATE(&logbuf.run, &init, DIGITAL);
  }

  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  // wait for the first edge of a burst

    digital_sample_and_log(false);

    // wait until the inputs have been quiet for DEBOUNCE_QUIET_MS
    while (xTaskGetTickCount() - last_edge_tick < pdMS_TO_TICKS(DEBOUNCE_QUIET_MS)) {
      vTaskDelay(pdMS_TO_TICKS(2));
    }

    // drop notifications accumulated during the burst, then log the settled state.
    // an edge arriving after this take sets the notification again, so the outer
    // loop reruns and no final state change is ever lost.
    ulTaskNotifyTake(pdTRUE, 0);
    digital_sample_and_log(false);
  }
}
