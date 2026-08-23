#ifndef PROTOCOL_H
#define PROTOCOL_H

// Binary log protocol — wire/storage data layout.
// This is the SD .log file format. The external upstream viewer
// (https://v2.monolith.luftaquila.io/) parses log_t in 24-byte chunks, so these
// struct definitions are a fixed compatibility contract: field renames are fine,
// but layout / PROTOCOL_VERSION / LOG_MAGIC / checksum must not change.

#include <float.h>
#include <stddef.h>
#include <stdint.h>

/***** log protocol *****/
#define PROTOCOL_VERSION 1
#define LOG_MAGIC 0xAE
#define LOG_FEATURE_DIGITAL_WHEEL_SPEED_FLOAT (1U << 0)
#define LOG_FEATURE_SENSOR_STATUS_EVENTS      (1U << 1)

typedef enum {
  LOG_TYPE_INVALID,
  LOG_TYPE_BOOT,
  LOG_TYPE_CAN,
  LOG_TYPE_GPS,
  LOG_TYPE_ANALOG,
  LOG_TYPE_DIGITAL,
  LOG_TYPE_GYROSCOPE,
  LOG_TYPE_SYSTEM,
  LOG_TYPE_USER_EVENT,
} log_type_t;

typedef struct {
  uint8_t protocol_version;
  uint8_t feature_flags;
  uint8_t mac[6];
  uint64_t boot_time;  // second since epoch
} boot_record_t;

typedef struct {
  uint32_t id;
  uint8_t extended;
  uint8_t remote;
  uint8_t len;
  uint8_t _reserved[1];
  uint8_t data[8];
} can_record_t;

typedef struct {
  uint32_t latitude;
  uint32_t longitude;
  uint8_t lat_dir;
  uint8_t lon_dir;
  uint8_t _reserved[2];
  uint16_t speed;
  uint16_t course;
} gps_record_t;

typedef struct {
  int16_t ain1;
  int16_t ain2;
  int16_t ain3;
  int16_t ain4;
  int16_t ain5;
  int16_t ain6;
  int16_t ain7;
  int16_t ain8;
} analog_record_t;

typedef struct {
  float wheel_speed_fl_kmh;
  float wheel_speed_fr_kmh;
  float wheel_speed_rl_kmh;
  float wheel_speed_rr_kmh;
} digital_record_t;

typedef struct {
  int16_t accel_x;
  int16_t accel_y;
  int16_t accel_z;
  int16_t temperature;
  int16_t gyro_x;
  int16_t gyro_y;
  int16_t gyro_z;
  uint8_t _reserved[2];
} gyroscope_record_t;

typedef struct {
  char msg[16];
} system_event_t;

typedef system_event_t user_event_t;

typedef struct {
  uint8_t magic;
  uint8_t type;
  uint16_t checksum;
  uint32_t timestamp;
  union {
    boot_record_t boot;
    can_record_t can;
    gps_record_t gps;
    analog_record_t analog;
    digital_record_t digital;
    gyroscope_record_t gyroscope;
    system_event_t system_event;
    user_event_t user_event;
  } payload;  // 16 bytes
} log_t;

// Keep the fixed 24-byte wire/storage contract while DIGITAL uses IEEE-754 float32.
_Static_assert(LOG_TYPE_DIGITAL == 5, "LOG_TYPE_DIGITAL wire value changed");
_Static_assert(sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128,
  "wheel-speed log requires IEEE-754 float32");
_Static_assert(sizeof(digital_record_t) == 16, "digital payload must remain 16 bytes");
_Static_assert(offsetof(digital_record_t, wheel_speed_fl_kmh) == 0, "FL wheel-speed offset changed");
_Static_assert(offsetof(digital_record_t, wheel_speed_fr_kmh) == 4, "FR wheel-speed offset changed");
_Static_assert(offsetof(digital_record_t, wheel_speed_rl_kmh) == 8, "RL wheel-speed offset changed");
_Static_assert(offsetof(digital_record_t, wheel_speed_rr_kmh) == 12, "RR wheel-speed offset changed");
_Static_assert(offsetof(log_t, payload) == 8, "log payload offset changed");
_Static_assert(sizeof(((log_t *)0)->payload) == 16, "log payload union must remain 16 bytes");
_Static_assert(sizeof(log_t) == 24, "log record must remain 24 bytes");

#endif  // PROTOCOL_H
