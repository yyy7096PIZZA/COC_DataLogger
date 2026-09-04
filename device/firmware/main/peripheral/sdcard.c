#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "main.h"

#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "esp_vfs_fat.h"

#define SD_SPI_HOST  SPI2_HOST
#define SD_PIN_SCK   GPIO_NUM_39
#define SD_PIN_MOSI  GPIO_NUM_40
#define SD_PIN_MISO  GPIO_NUM_48
#define SD_PIN_CS    GPIO_NUM_41

extern struct timeval boot;

char logpath[64];
static uint32_t log_boot_count;

static bool build_final_log_path(uint64_t boot_time, char *out, size_t out_size) {
  if (out == NULL || out_size == 0) return false;

  time_t boot_epoch = (time_t)boot_time;
  struct tm tp;
  char datetime[24];
  bool success = false;

  setenv("TZ", storage.tz, 1);
  tzset();

  if (localtime_r(&boot_epoch, &tp) != NULL &&
      strftime(datetime, sizeof(datetime), "%Y-%m-%d-%H-%M-%S", &tp) > 0) {
    int length = snprintf(out, out_size, "/sdcard/%08lu-%s.log",
      (unsigned long)log_boot_count, datetime);
    success = length > 0 && (size_t)length < out_size;
  }

  setenv("TZ", "UTC", 1);
  tzset();
  return success;
}

static int rename_log_after_gps(int fd, uint64_t boot_time) {
  if (fd < 0) return -1;

  if (fsync(fd) != 0) {
    ESP_LOGW("SD", "log rename skipped: fsync failed (%s)", strerror(errno));
    return fd;
  }

  char oldpath[sizeof(logpath)];
  char newpath[sizeof(logpath)];
  snprintf(oldpath, sizeof(oldpath), "%s", logpath);

  if (!build_final_log_path(boot_time, newpath, sizeof(newpath))) {
    ESP_LOGW("SD", "log rename skipped: final path generation failed");
    return fd;
  }

  if (strcmp(oldpath, newpath) == 0) return fd;

  if (close(fd) != 0) {
    ESP_LOGW("SD", "log close before rename failed (%s)", strerror(errno));
  }

  if (rename(oldpath, newpath) != 0) {
    int rename_errno = errno;
    ESP_LOGW("SD", "log rename failed: %s -> %s (%s)",
      oldpath, newpath, strerror(rename_errno));

    int reopened_fd = open(oldpath, O_RDWR | O_APPEND);
    if (reopened_fd >= 0) {
      lseek(reopened_fd, 0, SEEK_END);
    }
    return reopened_fd;
  }

  snprintf(logpath, sizeof(logpath), "%s", newpath);

  int reopened_fd = open(logpath, O_RDWR | O_APPEND);
  if (reopened_fd < 0) {
    ESP_LOGE("SD", "renamed log reopen failed: %s (%s)", logpath, strerror(errno));
    return -1;
  }

  lseek(reopened_fd, 0, SEEK_END);
  INFO(SD, "log renamed after GPS fix: %s -> %s", oldpath, logpath);
  return reopened_fd;
}

/*******************************************************************************
 * correct the BOOT record's boot_time in place (STEP 7).
 * The absolute time the upstream viewer reconstructs is boot_time + timestamp/1000,
 * so once GPS gives us a real clock we rewrite only the boot_time value of record 0.
 * The 24-byte log_t layout, magic, version, "first record = BOOT" invariant and the
 * checksum algorithm are all preserved — only the boot_time bytes change.
 ******************************************************************************/
static void correct_boot_record(int fd, uint64_t boot_time) {
  log_t rec;

  // read back the existing first record
  if (lseek(fd, 0, SEEK_SET) != 0 || read(fd, &rec, sizeof(rec)) != (ssize_t)sizeof(rec)) {
    ESP_LOGW("SD", "boot_time fixup: could not read record 0");
    goto restore;
  }

  // invariant guard: record 0 must still be a valid BOOT record before we touch it
  if (rec.magic != LOG_MAGIC || rec.type != LOG_TYPE_BOOT) {
    ESP_LOGW("SD", "boot_time fixup: record 0 is not a BOOT record, skipping");
    goto restore;
  }

  // patch only boot_time; magic/type/timestamp/mac/reserved bytes are left untouched
  rec.payload.boot.boot_time = boot_time;

  // recompute the folded-XOR checksum exactly as log_prepare() does (checksum field = 0 first)
  rec.checksum    = 0;
  uint32_t *ptr   = (uint32_t *)&rec;
  uint32_t chksum = 0;
  for (size_t i = 0; i < sizeof(log_t) / sizeof(uint32_t); i++) chksum ^= ptr[i];
  rec.checksum = (chksum & 0xFFFF) + (chksum >> 16);

  // write the corrected record back over record 0
  if (lseek(fd, 0, SEEK_SET) != 0 || write(fd, &rec, sizeof(rec)) != (ssize_t)sizeof(rec)) {
    ESP_LOGW("SD", "boot_time fixup: could not rewrite record 0");
    goto restore;
  }

  fsync(fd);
  INFO(SD, "boot_time corrected to %llu (epoch); BOOT record 0 rewritten, checksum valid",
    (unsigned long long)boot_time);

restore:
  // always leave the offset at EOF so the append stream below is never corrupted
  lseek(fd, 0, SEEK_END);
}

/*******************************************************************************
 * save log queue to SD card every 1000 ms
 ******************************************************************************/
static void write_batch(int fd, const log_t *batch, int n) {
  if (write(fd, batch, n * sizeof(log_t)) != (ssize_t)(n * sizeof(log_t)) && !IS_FATAL(&logbuf.run, SD)) {
    debug_monitor_publish_sd_status(true, false);
    FATAL_LOG(&logbuf.run, SD, "write failure");
  }
}

static void task_sdcard(void *pvParameters) {
  TickType_t xLastWakeTime = xTaskGetTickCount();

  int fd = (int)pvParameters;
  int write_count = 0;
  int cycle_count = 0;
  bool boot_fixed = false;  // BOOT record boot_time corrected once from GPS

  // 레코드를 모아서 write() 호출 횟수를 줄인다 (호출당 VFS+FATFS 오버헤드).
  // 파일에 기록되는 바이트와 레코드 순서는 개별 write와 완전히 동일 — 뷰어 호환 유지.
  // 64개 × 24B = 1536B = FAT 섹터 3개. 태스크 스택(4KB)에 안 올리려고 static.
  static log_t batch[64];

  while (true) {
    // one-time boot_time correction once GPS has set the clock (STEP 7 trigger)
    uint64_t fixup = boot_time_fixup_epoch;
    if (!boot_fixed && fixup != 0) {
      correct_boot_record(fd, fixup);
      int new_fd = rename_log_after_gps(fd, fixup);
      if (new_fd < 0) {
        debug_monitor_publish_sd_status(true, false);
        FATAL_LOG(&logbuf.run, SD, "log reopen failure after GPS rename");
        vTaskDelete(NULL);
        return;
      }
      fd = new_fd;
      boot_fixed = true;
    }

    int n = 0;

    while (xQueueReceive(logqueue, &batch[n], 0) == pdTRUE) {
      n++;

      if (n == (int)(sizeof(batch) / sizeof(batch[0]))) {
        write_batch(fd, batch, n);
        write_count += n;
        n = 0;
      }
    }

    if (n > 0) {
      write_batch(fd, batch, n);
      write_count += n;
    }

    cycle_count++;

    if (write_count > 0 && (write_count >= 512 || cycle_count >= 3)) {
      if (fsync(fd) != 0 && !IS_FATAL(&logbuf.run, SD)) {
        debug_monitor_publish_sd_status(true, false);
        FATAL_LOG(&logbuf.run, SD, "fsync failure");
      }
      write_count = 0;
      cycle_count = 0;
      INFO(SD, "log sync");
    }

    xTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(200));
  }
}

/*******************************************************************************
 * init SDSPI, mount filesystem and create task
 ******************************************************************************/
void sdcard_init(void) {
  esp_vfs_fat_sdmmc_mount_config_t mount_config = {
    .format_if_mount_failed   = false,
    .max_files                = 4,
    .allocation_unit_size     = 32 * 512,
    .disk_status_check_enable = false,
    .use_one_fat              = false,
  };

  sdmmc_card_t *card;
  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot         = SD_SPI_HOST;
  host.max_freq_khz = 4000;  // 4 MHz — 점퍼선 배선은 20MHz 기본값에서 읽기 손상, 400kHz는 CAN 만부하 로깅(~50KB/s)을 못 따라감

  spi_bus_config_t bus_cfg = {
    .mosi_io_num     = SD_PIN_MOSI,
    .miso_io_num     = SD_PIN_MISO,
    .sclk_io_num     = SD_PIN_SCK,
    .quadwp_io_num   = -1,
    .quadhd_io_num   = -1,
    .max_transfer_sz = 4000,
  };

  if (spi_bus_initialize(SD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO) != ESP_OK) {
    FATAL_LOG(&init, SD, "SPI bus init failure");
    goto finish;
  }

  sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
  slot_config.gpio_cs = SD_PIN_CS;
  slot_config.host_id = SD_SPI_HOST;

  if (esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_config, &mount_config, &card) != ESP_OK) {
    FATAL_LOG(&init, SD, "mount failure");
    goto finish;
  }
  debug_monitor_publish_sd_status(true, false);

  // monotonic boot counter in NVS: guarantees a unique filename even when the clock is
  // unset (boot.tv_sec == 0 → every boot would otherwise render the same "1970-..." name
  // and O_TRUNC would clobber the previous log).
  uint32_t bootcnt = 0;
  nvs_get_u32(nvs, "bootcnt", &bootcnt);
  bootcnt++;
  nvs_set_u32(nvs, "bootcnt", bootcnt);
  nvs_commit(nvs);
  log_boot_count = bootcnt;

  // set log file (datetime is 1970 until GPS sets the clock; the counter keeps it unique)
  setenv("TZ", storage.tz, 1);
  tzset();

  struct tm tp;
  struct tm *tm = localtime_r(&boot.tv_sec, &tp);

  char datetime[24];
  strftime(datetime, sizeof(datetime), "%Y-%m-%d-%H-%M-%S", tm);

  setenv("TZ", "UTC", 1);
  tzset();

  snprintf(logpath, sizeof(logpath), "/sdcard/%08lu-%s.log", (unsigned long)bootcnt, datetime);

  int fd = open(logpath, O_RDWR | O_CREAT | O_TRUNC, 0);

  if (fd < 0) {
    FATAL_LOG(&init, SD, "file open failure");
  }

  // create log queue and sdcard task
  logqueue = xQueueCreate(2560, sizeof(log_t));

  if (xTaskCreate(task_sdcard, "sdcard", 4096, (void *)fd, 7, NULL) != pdPASS) {
    FATAL_LOG(&init, SD, "task create failure");
    goto finish;
  }

  if (fd >= 0 && logqueue != NULL) debug_monitor_publish_sd_status(true, true);

  INFO(SD, "log file: %s", logpath);

  log_t boot_record = { 0 };  // 미사용 payload 바이트가 스택 쓰레기로 기록되지 않도록 전체 0 초기화
  boot_record.payload.boot.protocol_version = PROTOCOL_VERSION;
  boot_record.payload.boot.feature_flags =
    LOG_FEATURE_DIGITAL_WHEEL_SPEED_FLOAT | LOG_FEATURE_SENSOR_STATUS_EVENTS;
  boot_record.payload.boot.boot_time        = (uint64_t)boot.tv_sec;
  memcpy(boot_record.payload.boot.mac, storage.mac, sizeof(storage.mac));

  if (LOG(LOG_TYPE_BOOT, &boot_record) != true) {
    debug_monitor_publish_sd_status(true, false);
    FATAL_LOG(&init, SD, "boot record failure");
    goto finish;
  }

finish:
  if (IS_OK(&init, SD)) {
    CLEAR_ALL(&logbuf.run, SD);
  } else {
    COPY_STATE(&logbuf.run, &init, SD);
  }
}
