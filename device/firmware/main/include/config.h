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

/***** compile-time sensor selection *****/
// These switches are the authoritative source for peripheral enablement.  The
// legacy NVS *_en keys are intentionally ignored so a flashed build behaves the
// same on boards with different NVS contents.
#ifndef SENSOR_ENABLE_CAN
#define SENSOR_ENABLE_CAN 1
#endif

#ifndef SENSOR_ENABLE_GPS
#define SENSOR_ENABLE_GPS 1
#endif

#ifndef SENSOR_ENABLE_GYRO
#define SENSOR_ENABLE_GYRO 1
#endif

#ifndef SENSOR_ENABLE_DISPLAY
#define SENSOR_ENABLE_DISPLAY 1
#endif

#ifndef SENSOR_ENABLE_ADS48
#define SENSOR_ENABLE_ADS48 1
#endif

#ifndef SENSOR_ENABLE_ADS49
#define SENSOR_ENABLE_ADS49 1
#endif

// bit 0..7 = AIN1..AIN8; bit 0..3 = FL/FR/RL/RR respectively.
#ifndef SENSOR_ANALOG_MASK
#define SENSOR_ANALOG_MASK 0xFFU
#endif

#ifndef SENSOR_WHEEL_MASK
#define SENSOR_WHEEL_MASK 0x0FU
#endif

#ifndef SENSOR_RETRY_MS
#define SENSOR_RETRY_MS 5000
#endif

#ifndef SENSOR_FAILURE_THRESHOLD
#define SENSOR_FAILURE_THRESHOLD 3
#endif

#if (SENSOR_ENABLE_CAN != 0) && (SENSOR_ENABLE_CAN != 1)
#error "SENSOR_ENABLE_CAN must be 0 or 1"
#endif
#if (SENSOR_ENABLE_GPS != 0) && (SENSOR_ENABLE_GPS != 1)
#error "SENSOR_ENABLE_GPS must be 0 or 1"
#endif
#if (SENSOR_ENABLE_GYRO != 0) && (SENSOR_ENABLE_GYRO != 1)
#error "SENSOR_ENABLE_GYRO must be 0 or 1"
#endif
#if (SENSOR_ENABLE_DISPLAY != 0) && (SENSOR_ENABLE_DISPLAY != 1)
#error "SENSOR_ENABLE_DISPLAY must be 0 or 1"
#endif
#if (SENSOR_ENABLE_ADS48 != 0) && (SENSOR_ENABLE_ADS48 != 1)
#error "SENSOR_ENABLE_ADS48 must be 0 or 1"
#endif
#if (SENSOR_ENABLE_ADS49 != 0) && (SENSOR_ENABLE_ADS49 != 1)
#error "SENSOR_ENABLE_ADS49 must be 0 or 1"
#endif
#if ((SENSOR_ANALOG_MASK) & ~0xFFU)
#error "SENSOR_ANALOG_MASK may only use bits 0..7"
#endif
#if ((SENSOR_WHEEL_MASK) & ~0x0FU)
#error "SENSOR_WHEEL_MASK may only use bits 0..3"
#endif
#if (SENSOR_RETRY_MS <= 0)
#error "SENSOR_RETRY_MS must be greater than zero"
#endif
#if (SENSOR_FAILURE_THRESHOLD <= 0)
#error "SENSOR_FAILURE_THRESHOLD must be greater than zero"
#endif

// ADS module switches take precedence over their channel bits.  ADS49 retains
// the global AIN5..AIN8 bit positions so both masks can be ORed or indexed alike.
#define SENSOR_ADS48_EFFECTIVE_MASK ((SENSOR_ENABLE_ADS48) ? ((SENSOR_ANALOG_MASK) & 0x0FU) : 0U)
#define SENSOR_ADS49_EFFECTIVE_MASK ((SENSOR_ENABLE_ADS49) ? ((SENSOR_ANALOG_MASK) & 0xF0U) : 0U)
#define SENSOR_EFFECTIVE_ANALOG_MASK (SENSOR_ADS48_EFFECTIVE_MASK | SENSOR_ADS49_EFFECTIVE_MASK)
#define SENSOR_EFFECTIVE_WHEEL_MASK  ((SENSOR_WHEEL_MASK) & 0x0FU)

#define SENSOR_ADS48_ACTIVE (SENSOR_ADS48_EFFECTIVE_MASK != 0U)
#define SENSOR_ADS49_ACTIVE (SENSOR_ADS49_EFFECTIVE_MASK != 0U)
#define SENSOR_ANALOG_ACTIVE (SENSOR_EFFECTIVE_ANALOG_MASK != 0U)
#define SENSOR_DIGITAL_ACTIVE (SENSOR_EFFECTIVE_WHEEL_MASK != 0U)

/***** vehicle tire configuration (shared by display and wheel-speed sensors) *****/
#define VEHICLE_WET_TRACK 0
#define VEHICLE_TIRE_DIAM_DRY_M 0.40f  // dry: diameter 40 cm
#define VEHICLE_TIRE_DIAM_WET_M 0.50f  // wet: diameter 50 cm

#if VEHICLE_WET_TRACK
#define VEHICLE_TIRE_CIRC_M (3.14159265f * VEHICLE_TIRE_DIAM_WET_M)
#else
#define VEHICLE_TIRE_CIRC_M (3.14159265f * VEHICLE_TIRE_DIAM_DRY_M)
#endif

/***** optional 1 Hz serial sensor monitor *****/
#ifndef DEBUG_SENSOR_MONITOR
#define DEBUG_SENSOR_MONITOR 1
#endif

#define DEBUG_SENSOR_MONITOR_INTERVAL pdMS_TO_TICKS(1000)

/***** sensor task intervals *****/
#define TASK_INTERVAL_GYRO   pdMS_TO_TICKS(10)   // 100Hz

#define TASK_INTERVAL_ANALOG pdMS_TO_TICKS(10)   // 100Hz (ADS1115 모듈 2개 8ch, 변환 대기 sleep 포함 ~8.5ms/cycle)

#endif  // CONFIG_H
