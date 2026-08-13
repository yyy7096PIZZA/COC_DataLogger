#ifndef CONFIG_H
#define CONFIG_H

// Compile-time configuration: peripheral option enums, CAN device IDs, bus
// timeouts and task intervals, plus small pure helpers.
// Note: pdMS_TO_TICKS() requires FreeRTOS; this header is included by main.h
// after the FreeRTOS-providing includes, so include config.h via main.h.

#include <stdint.h>

/***** CAN device IDs — 확인 필요: CAN_SIGNALS_SELECTED.md 참조 *****/
#define CAN_EZ_SA      0xEFU
#define CAN_EZ_MODE    0x17U   // METER=0x17, VCU=0xD0
#define CAN_EZ_ID1     (0x18010000U | ((uint32_t)CAN_EZ_MODE << 8) | CAN_EZ_SA)   // 0x180117EF
#define CAN_DALY_PC    0x40U
#define CAN_DALY_ADDR  0x01U
#define CAN_DALY_ID(d) (0x18000000U | ((uint32_t)(d) << 16) | ((uint32_t)CAN_DALY_PC << 8) | CAN_DALY_ADDR)
#define CAN_DALY_ID90  CAN_DALY_ID(0x90U)   // 0x18904001

/***** peripheral configs *****/
enum {
  CAN_BPS_1K,
  CAN_BPS_5K,
  CAN_BPS_10K,
  CAN_BPS_12_5K,
  CAN_BPS_16K,
  CAN_BPS_20K,
  CAN_BPS_25K,
  CAN_BPS_50K,
  CAN_BPS_100K,
  CAN_BPS_125K,
  CAN_BPS_250K,
  CAN_BPS_500K,
  CAN_BPS_800K,
  CAN_BPS_1M,
  CAN_BPS_MAX
};

enum {
  GPS_DEV_UBLOX,
  GPS_DEV_MAX,
};

/***** I2C timeout *****/
#define I2C_TIMEOUT_MS 10

/***** sensor task intervals *****/
#define TASK_INTERVAL_GYRO   pdMS_TO_TICKS(10)   // 100Hz

#define TASK_INTERVAL_ANALOG pdMS_TO_TICKS(10)   // 100Hz (ADS1115 모듈 2개 8ch, 변환 대기 sleep 포함 ~8.5ms/cycle)

#endif  // CONFIG_H
