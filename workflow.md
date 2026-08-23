# Monolith v2 — 펌웨어 워크플로우 지도

**ESP32-S3 펌웨어 하나가 센서를 읽어 SD카드에 `.log` 파일로 기록하는 단일 시스템.** 기록된 로그는 이 레포가 아니라 외부 업스트림 사이트(https://v2.monolith.luftaquila.io/)에서 분석한다.

---

# 1. 레포 전체 지도

| 폴더 | 역할 | 언어/스택 |
|------|------|-----------|
| `device/firmware` | ESP32-S3 펌웨어 (센서 수집 → SD 기록) | C / ESP-IDF v6.0.1 |
| `CAN` | CAN 신호 정의 문서 (EZkontrol 모터컨트롤러, Daly BMS) | Markdown |
| `docs` | 사용자 문서 사이트 (ko/en) | HTML |
| `.github` | CI (펌웨어 빌드/릴리스) + 플래싱 스크립트 | YAML |

제거되어 더 이상 없는 것: `web/`(관제 웹앱), `server/`(MQTT 브로커·리버스 프록시), `device/hardware`(PCB), 펌웨어의 WiFi·MQTT·외부 RTC.

핵심 원칙 (`CLAUDE.md`): **SD 로그 바이너리 포맷 구조 변경 금지.** `device/firmware/main/include/protocol.h`의 24바이트 `log_t` 레이아웃, `LOG_MAGIC`(0xAE), `PROTOCOL_VERSION`(1), 체크섬 알고리즘, "BOOT 레코드가 항상 첫 레코드" 순서를 지켜야 외부 업스트림 뷰어가 파일을 읽는다. 필드 이름 변경(바이트 그대로)은 가능, 레이아웃/버전/매직/체크섬 변경은 사전 확인 필요.

---

# 2. 데이터 흐름

```
[센서 태스크들] → LOG() 매크로 → logqueue (24B log_t, 큐 1개)
                                        │
                                        ▼
                                [task_sdcard]
                        /sdcard/<부팅카운터>-<시각>.log 에 순서대로 append
                        (raw 24바이트 그대로, 100Hz 풀데이터)
                                        │
                                        ▼
                (오프라인) SD카드를 빼서 외부 업스트림 사이트에 업로드 → 분석
```

큐는 `logqueue` 하나뿐(옛 MQTT용 `canlogqueue`/`syslogqueue`는 제거). 모든 로그 타입(BOOT/CAN/GPS/ANALOG/DIGITAL/GYRO/SYSTEM/USER_EVENT)이 이 큐로 들어가 SD에 기록된다.

---

# 3. 펌웨어 (`device/firmware/main/`)

## 3-1. 부팅 순서 — [main.c](device/firmware/main/main.c)
`app_main()` 초기화 순서:
1. `core_init()` — 온보드 RGB(GPIO38) 소등, 상태 LED(GPIO5) + `task_led` 생성, GPIO ISR 서비스 설치.
2. `nvs_init()` — 설정 영구저장소 기본값 채우기: MAC, 타임존(기본 `KST-9`), CAN/GPS/analog/digital 활성화 여부, CAN 비트레이트·필터·마스크, GPS 장치종류. WiFi 자격증명 기본값은 네트워크 제거와 함께 삭제, `nvs_storage_t`는 옛 `wifi`/`device` 그룹을 걷어내고 `storage.mac`/`storage.tz` 최상위 필드로 평탄화.
3. `i2c0_init()` — TZ=UTC 설정, I2C0(GPIO9/10) 버스 생성(**자이로·디스플레이 공유**), `gettimeofday(&boot,...)`로 부팅 시각 기록(GPS 픽스 전이라 1970년 epoch 0일 수 있음).
4. `sdcard_init()` — SD 마운트 + `logqueue` 생성 + SD 기록 태스크 + BOOT 레코드 기록.
5. `peripheral_task_init()` — CAN/GPS/Analog/Digital/Gyroscope/Display 태스크 생성(설정에서 켜진 것만; Gyroscope·Display는 항상 생성).

**RTC 없음.** 내장 RTC 타이머는 배터리 백업이 없어 전원이 꺼지면 시각 소실 → GPS(NMEA `$GNRMC`/`$GPRMC`) 첫 픽스로 시스템 시계를 1회 설정하고([gps.c](device/firmware/main/peripheral/gps.c)), SD BOOT 레코드 `boot_time`을 역보정한다(3-4절).

**상태 시스템** ([main.h](device/firmware/main/include/main.h)): `state_t` = 32비트 비트맵. 컴포넌트 9개(CORE, NVS, I2C0, SD, CAN, GPS, ANALOG, DIGITAL, GYRO), 하위 9비트=ERROR, `+12`비트=FATAL. `logbuf.run`이 현재 상태 보유, `task_led` 점멸 속도 = 정상 1Hz / ERROR 4Hz / FATAL 10Hz.

## 3-2. `LOG()` 매크로 ([main.h:141](device/firmware/main/include/main.h#L141))
모든 센서 태스크가 `log_t`(24바이트)를 채우고 `LOG(타입, &log)` 호출:
- 헤더 채움(magic 0xAE, type, timestamp=부팅후 ms) → 체크섬 계산(8-1절) → `logqueue`에 `xQueueSend`(논블로킹).
- 큐는 `sdcard_init()`의 `xQueueCreate(2560, sizeof(log_t))` 하나뿐. `LOG_FROM_ISR`은 digital.c 디바운스 전환으로 제거 — ISR에서 직접 기록하는 곳 없음.

## 3-3. 센서 태스크들 (`main/peripheral/`)
| 파일 | 역할 | 핵심 |
|------|------|------|
| [can.c](device/firmware/main/peripheral/can.c) | TWAI(CAN) 수신, GPIO15/16 | `twai_receive()` **블로킹 수신**(100ms 타임아웃) — 프레임이 없으면 잠들어 있어 유휴 CPU 점유 최소. 모든 프레임을 그대로 SD에 기록(레이트 제한 없음). `data[]`는 8바이트 고정(짧은 프레임은 0 채움, DLC>8 방어). EZ RPM·Daly SOC는 `display_can` 스냅샷에도 복사해 디스플레이가 사용 |
| [analog.c](device/firmware/main/peripheral/analog.c) | ADS1115 외장 ADC 2개(0x48/0x49), I2C1(GPIO42 SCL/47 SDA) | **두 모듈 동시 변환** 후 1.4ms 한 번만 대기(파이프라이닝). 대기도 busy-wait이 아니라 `vTaskDelay(2ms)`로 CPU 양보 후 부족분만 짧게 spin(8-7절). ain1~8 |
| [gyroscope.c](device/firmware/main/peripheral/gyroscope.c) | MPU-6050(0x68), I2C0 공유버스, 100Hz | 부팅 시 32샘플을 **1ms 간격으로 분산 수집**해 자이로 오프셋 자동 캘리브레이션(8-9절) 후 가속도/자이로 읽기 |
| [gps.c](device/firmware/main/peripheral/gps.c) | u-blox GPS, UART1(GPIO17/18) | NMEA GPRMC 파싱 → 위경도/속도/방위. **첫 유효 픽스에서 시스템 시계를 1회 설정**하고 `boot_time_fixup_epoch`를 세팅해 SD 태스크에 보정을 트리거. 파서는 콤마 누락(깨진 문장)에도 NULL 역참조 없이 안전 |
| [digital.c](device/firmware/main/peripheral/digital.c) | 디지털 입력 4채널(GPIO11~14) | **디바운스 구조**(8-4절): ISR은 마지막 엣지 시각 기록+태스크 깨우기만, 태스크가 버스트 첫 엣지 즉시 1회 + 10ms 조용해진 뒤 안정 상태 기록(변화 없으면 스킵) |
| [display.c](device/firmware/main/peripheral/display.c) | I2C LCD(PCF8574 0x27) + HD44780, I2C0 공유버스, 1Hz | **차속 계산**: CAN RPM → 기어비 4.02 + 타이어 둘레로 km/h 환산, 대형 숫자 폰트 표시. SOC%도 상단에 표시. **diff 렌더링**(8-5절): 프레임버퍼 비교로 바뀐 글자만 I2C 전송 → 같은 버스의 자이로를 방해하지 않음. PCF8574 없으면 자동 종료 |

공통 패턴: 샘플 → `LOG()`(SD 기록). `logbuf`에는 상태 비트맵(`run`)과 digital 최종 기록 상태(변화 감지 비교용)만 남음. 디스플레이용 CAN 값은 별도 `display_can` 구조체로 전달.

## 3-4. SD 기록 — [sdcard.c](device/firmware/main/peripheral/sdcard.c)
- SPI2(SCK39/MOSI40/MISO48/CS41), **SDSPI** 모드(SDMMC 아님), 클럭 **4MHz** — 점퍼선 배선에서 기본값 20MHz는 읽기 손상, 400kHz는 CAN 만부하 로깅(~50KB/s)을 못 따라가 중간값 선정. (실카드 기록 → 뷰어 체크섬 검증 예정)
- 파일명 `/sdcard/<부팅카운터 8자리>-<YYYY-MM-DD-HH-MM-SS>.log`. 부팅카운터는 NVS 저장 단조증가 값 — `boot.tv_sec == 0`(1970년)이어도 파일명이 겹쳐 `O_TRUNC`로 이전 로그를 지우는 사고 방지.
- 첫 레코드는 항상 **BOOT 레코드**(프로토콜 버전 + MAC + 부팅 시각).
- `task_sdcard`: 200ms마다 logqueue를 비우되 레코드를 **64개(1536B = FAT 섹터 3개)씩 모아 write() 1회로 기록**(8-2절) — 파일의 바이트·순서는 개별 write와 완전히 동일. **fsync는 512건 또는 3사이클(~600ms)마다.** write/fsync 실패 시 FATAL 상태 전환.
- **BOOT 레코드 boot_time 역보정**(`correct_boot_record`): GPS가 시계를 맞추면 `gps.c`가 `boot_time_fixup_epoch`를 세팅, `task_sdcard`가 다음 루프에서 파일 레코드 0(BOOT)을 되읽어 `boot_time`만 고쳐 쓰고 체크섬 재계산. 매직/타입/레이아웃 불변, 파일 오프셋은 EOF로 복원(8-3절).
- **파일 = 와이어 포맷 100% 동일** → 외부 업스트림 뷰어가 같은 24바이트 파서로 그대로 읽음.

---

# 4. 프로토콜 — `protocol.h`

`log_t` = 헤더 8B(`magic` 0xAE / `type` / `checksum` / `timestamp`) + 페이로드 union 16B. 타입: BOOT/CAN/GPS/ANALOG/DIGITAL/GYROSCOPE/SYSTEM/USER_EVENT ([protocol.h](device/firmware/main/include/protocol.h)). 이 구조체가 SD `.log` 파일의 바이트 그대로이자 **레포 밖 업스트림 파서와의 계약** — 레이아웃·매직·버전·체크섬 변경 금지.

---

# 5. 하드웨어 핀맵

보드: ESP32-S3-DevKitC-1 **v1.1** + ESP32-S3-WROOM-1 **N16R8**(16MB Flash + 8MB Octal PSRAM).
- [device/firmware/docs/PINMAP.md](device/firmware/docs/PINMAP.md) — 실제 배선 체크리스트(핀별 연결 대상 + 코드 근거 줄번호)
- [device/firmware/docs/GPIO_USAGE.md](device/firmware/docs/GPIO_USAGE.md) — 칩 전체 GPIO 48개의 점유/자유 상태

요약: I2C0(9/10, 자이로+LCD 공유) · I2C1(42 SCL/47 SDA, ADS1115 ×2) · TWAI(15/16) · UART1(17/18, GPS) · SPI2(39/40/41/48, SD) · GPIO11-14(디지털 입력) · GPIO5(상태 LED) · GPIO38(온보드 RGB, 소등만).

---

# 6. CI/CD (`.github/`)

| 워크플로우 | 트리거 | 역할 |
|---|---|---|
| [firmware.yml](.github/workflows/firmware.yml) | `device/firmware/**` 변경 push | ESP-IDF v6.0.1로 빌드, 산출물(`monolith.*`, bootloader, partition_table) 아티팩트 업로드 |
| [release.yml](.github/workflows/release.yml) | `v*` 태그 push | firmware.yml 호출 → 산출물 + 플래싱 스크립트를 zip으로 묶어 GitHub 릴리스 생성 |

`.github/scripts/flash.{sh,ps1,bat}` = 릴리스 zip에 포함되는 사용자용 플래싱 스크립트.

---

# 7. 최적화 관점 정리

이미 들어간 설계:
- **fsync 묶기**: 512건/3사이클마다 → SD 쓰기 amplification 감소
- **SD 배치 쓰기**: 레코드 64개(1536B)씩 모아 write() 1회 — 파일 내용은 개별 write와 동일해 뷰어 호환 유지
- **FreeRTOS tick 1kHz** (`CONFIG_FREERTOS_HZ=1000`): 1 tick = 1ms — 짧은 sleep의 정밀도 확보(analog 2ms sleep, digital 디바운스 등의 전제)
- **CAN 블로킹 수신**: 1ms 폴링 → `twai_receive(100ms)` 대기. 유휴 버스에서 태스크 wakeup 1000회/s → ~10회/s
- **ADS1115 동시변환 파이프라이닝 + sleep 대기**: 두 모듈 1.4ms 한 번으로 8채널, busy-wait 대신 vTaskDelay로 CPU 양보(코어 점유 ~56%p 절감)
- **LCD diff 렌더링**: 매초 60자 전체 재전송 → 바뀐 글자만 전송. 공유 I2C0의 자이로(100Hz) 대기 시간 감소
- **디지털 입력 디바운스**: 채터링 수십 건 → 버스트당 기록 최대 2건. ISR에서 직접 기록 제거
- **자이로 오프셋 자동 캘리브레이션**: 부팅 시 32샘플을 1ms 간격 분산 평균으로 드리프트 제거
- **부팅카운터 기반 파일명**: 시계가 아직 안 맞아도 로그 파일이 덮어써지지 않음
- **BOOT 레코드 역보정**: 레이아웃/체크섬 규칙을 어기지 않고 `boot_time`만 사후 수정 — 전체 파일을 다시 쓰지 않아도 절대시각을 확보
- **고정 24B 레코드**: 파일 파싱이 O(1) 인덱싱
- **상태 비트맵 + LED 점멸 속도**: 배선 없이 컴포넌트별 OK/ERROR/FATAL을 바로 확인 가능
- **네트워크 서브시스템 제거**: WiFi/MQTT/웹서버 삭제로 바이너리 1,134,896 → 378,592B (-66.6%)

살펴볼 만한 잠재 최적화 지점:
- SPI 클럭 4MHz는 점퍼선 기준 안전값 — 배선을 납땜/짧은 케이블로 개선하면 20MHz 상향 여지
- CAN 수신이 레이트 제한 없이 전부 SD로 감 — 프레임이 매우 잦은 버스에서는 큐(`logqueue`, 2560개) 포화 여부 점검 필요

---

# 8. 핵심 알고리즘 해설

## 8-1. 로그 체크섬 — XOR 폴딩 (`log_prepare`, [main.h](device/firmware/main/include/main.h))
레코드 24바이트 무결성 검증값. `checksum` 필드를 0으로 비움 → 24바이트를 uint32 6개로 보고 전부 XOR → 상위 16비트 + 하위 16비트를 더해 16비트로 폴딩 → `checksum`에 저장. XOR은 단일 비트 깨짐 검출에 강하고 곱셈/나눗셈 없이 빠름. 뷰어가 같은 계산으로 검증해 불일치 레코드를 버림. **업스트림 뷰어와의 계약 — 변경 금지.**

## 8-2. SD 배치 쓰기 + fsync 정책 (`task_sdcard`, [sdcard.c](device/firmware/main/peripheral/sdcard.c))
write() 호출당 VFS→FATFS→SD 드라이버 고정 비용 절감: 200ms마다 큐 레코드를 `batch[64]`(1536B = FAT 섹터 3개)에 모아, 가득 차거나 큐가 비면 write() 1회. fsync()는 누적 512건 또는 3사이클(~600ms)마다 — SD 수명·지연 vs 전원차단 시 유실 구간의 절충. 파일에 적히는 바이트·순서는 한 건씩 쓸 때와 완전히 동일 → 뷰어 호환 유지.

## 8-3. BOOT 레코드 boot_time 역보정 (`correct_boot_record`, [sdcard.c](device/firmware/main/peripheral/sdcard.c))
뷰어의 절대시각 = `boot_time` + 레코드 timestamp인데 부팅 직후엔 boot_time이 0(1970년). 보정: ① GPS 첫 픽스 시 `현재시각 − 가동시간 = 부팅시각`을 `boot_time_fixup_epoch`에 발행([gps.c](device/firmware/main/peripheral/gps.c)) ② task_sdcard가 다음 루프에서 파일 레코드 0을 되읽음 ③ 매직·타입 검사로 BOOT 레코드 확인(아니면 포기) ④ `boot_time`만 수정, 체크섬을 8-1 방식으로 재계산 후 덮어쓰기 ⑤ 파일 오프셋을 EOF로 복원(append 보호). one-shot, 전체 파일 재작성 없음.

## 8-4. 디지털 입력 디바운스 ([digital.c](device/firmware/main/peripheral/digital.c))
채터링(기계식 접점이 수 ms간 수십 번 튕김) 대응. ISR은 마지막 엣지 시각 갱신 + 태스크 깨우기만(기록 없음). 태스크는 깨어나면 즉시 4채널 상태 1회 기록(반응 지연 최소화) → 마지막 엣지로부터 10ms가 조용히 지날 때까지 2ms씩 대기 → 안정된 최종 상태가 직전 기록과 다를 때만 1회 더 기록. 대기 중 쌓인 깨우기 신호는 버려도 이후 엣지가 신호를 다시 세워 마지막 변화를 놓치지 않음. 버스트당 기록 최대 2건.

## 8-5. LCD diff 렌더링 ([display.c](device/firmware/main/peripheral/display.c))
LCD가 자이로(100Hz)와 I2C0을 공유하므로 전체 재전송 대신 diff: `lcd_want[4][20]`(그리려는 화면) vs `lcd_frame[4][20]`(실제 LCD 내용)을 글자 단위로 비교, 달라진 연속 구간만 커서 이동 1회 + 전송하고 `lcd_frame`에 반영. 한 글자도 4바이트 I2C 트랜잭션 1회로 묶임(`lcd_write8`) → 자이로 대기 최소화.

## 8-6. NMEA 고정소수점 파서 (`parse_nmea_fixed`, [gps.c](device/firmware/main/peripheral/gps.c))
GPS 소수 문자열(`"3723.46587"`)을 float 없이 정수로 저장: 정수부 읽기 → 소수부는 목표 자릿수+1까지만 읽고(부족하면 ×10 채움, 남으면 반올림) → `정수부 × 10^목표자릿수 + 소수부`를 uint32로 반환. 위경도 소수 5자리(약 1m 분해능), 속도·방위 2자리 스케일링으로 `log_t` 정수 필드에 저장.

## 8-7. ADS1115 동시 변환 + sleep/spin 대기 ([analog.c](device/firmware/main/peripheral/analog.c))
변환 1회당 1.163ms(860SPS) 대기 필요. 두 모듈(0x48/0x49)에 같은 채널의 변환 시작 명령을 연달아 보내 동시 변환 → `vTaskDelay(2ms)`로 CPU 양보 → 경과가 1.4ms 미달이면 부족분만 마이크로초 spin(tick 1ms라 sleep만으로 부정확) → 두 결과 읽기(실패 시 I2C 버스 리셋 후 1회 재시도). 채널 A0~A3 반복 → 대기 4번으로 8채널 완성.

## 8-8. GPS 보레이트 자동 감지 (`init_ublox`, [gps.c](device/firmware/main/peripheral/gps.c))
후보 속도 115200 → 57600 → 38400 → 19200 → 9600을 각 최대 1.5초 수신, NMEA 시작 문자 `'$'`가 보이면 확정. 이후 GGA/GLL/GSA/GSV/VTG/ZDA/TXT를 끄고 RMC만 10Hz로 남기는 u-blox 바이너리 명령 전송 — UART 트래픽·파싱 비용 절감.

## 8-9. 자이로 오프셋 캘리브레이션 ([gyroscope.c](device/firmware/main/peripheral/gyroscope.c))
MPU-6050 정지 드리프트 보정: 부팅 직후(정지 가정) 하드웨어 오프셋 레지스터 0 리셋 → 자이로 3축 32샘플을 1ms 간격으로 분산 수집(몰아 읽으면 순간 노이즈가 평균에 유입) → 평균의 부호 반전 ÷2(오프셋 레지스터 ±1000dps : 측정 ±500dps = 2:1)를 하드웨어 오프셋 레지스터에 기록 → 이후 칩이 보정된 값을 출력, 런타임 보정 불필요.

## 8-10. 차속 계산 ([display.c](device/firmware/main/peripheral/display.c))
CAN의 EZkontrol 모터 RPM raw → `rpm = raw × 0.1 − 2000`(음수 = 후진, 절대값) → ÷ 기어비 4.02 = 바퀴 RPM → × 타이어 둘레(m) × 60 ÷ 1000 = km/h. 타이어 지름 건조 0.40m / 우천 0.50m는 `DISPLAY_WET_TRACK` 매크로로 전환. 반올림 후 상한 999. 마지막 CAN 수신이 2초를 넘으면 "NO SIGNAL" 표시.
