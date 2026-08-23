# ESP32-S3 배선 체크리스트 (Monolith v2 펌웨어)

- **보드:** ESP32-S3-DevKitC-1 **v1.1** + ESP32-S3-WROOM-1 **N16R8**
- **기준:** `device/firmware/main/` 소스 코드 (코드 대조 검증 2026-07-02)
- **이 문서는 "손으로 직접 배선해야 하는 핀"만 담은 체크리스트입니다.** 보드 온보드 부품, 칩 내부 자동 동작, ESP-IDF 기본 점유(USB로만 접근) 항목은 메인 표에서 제외하고 맨 아래 [배선 불필요(참고)](#배선-불필요-참고)에 따로 모았습니다.
- 표는 모든 센서가 ON인 기본 빌드 기준이다. 센서별 OFF 설정과 SD 상태 이벤트는 [SENSOR_CONFIGURATION.md](SENSOR_CONFIGURATION.md)를 참고한다.

## 구성품 구분 — 내장 / 외부 추가 / 사용 센서

펌웨어가 다루는 부품을 ① 보드·칩에 이미 있는 것, ② 외부에 직접 달아야 하는 것, ③ 우리가 실제 사용하는 센서로 나눴습니다.

### ① v1.1 보드 · WROOM-1 모듈에 이미 내장 (외부 배선 불필요)

| 항목 | GPIO | 비고 |
|------|:----:|------|
| 온보드 RGB LED (WS2812B) | 38 | DevKitC-1 v1.1 내장. task_led가 상태색 표시 (초록 OK / 노랑 ERROR / 빨강 FATAL, 저휘도 점멸) |
| BOOT 버튼 | 0 | 다운로드 모드 진입용, 온보드 |
| RESET 버튼 | (EN) | 칩 리셋, 온보드 (GPIO 아님) |
| 네이티브 USB | 19/20 | 온보드 USB 커넥터 |
| USB-UART 콘솔 | 43/44 | 온보드 USB-UART 브리지 경유 (모니터·플래싱) |
| 내장 SPI 플래시 | 26–32 | WROOM-1 모듈 내부 |
| Octal PSRAM | 33–37 | N16R8 모듈 내부 |
| 칩 내장 RTC 타이머 | — | ESP32-S3 RTC 도메인. **배터리 백업·캘린더 없음** → 전원 꺼지면 시간 소실. 외부 RTC 모듈은 쓰지 않고, 대신 GPS(NMEA GPRMC) 첫 픽스로 벽시계를 1회 설정 |

### ② 외부에 추가로 달아야 하는 모듈 / 부품

| 모듈 · 부품 | 인터페이스 | GPIO | 용도 |
|-------------|-----------|------|------|
| 캔 트랜시버 모듈 | TWAI(CAN) | 15/16 | CAN 버스 |
| GPS 모듈 (u-blox) | UART1 | 17/18 | 위치·속도 |
| SD 카드 모듈 | SPI2 | 39/40/41/48 | 로그 저장 |
| ADS1115 모듈 ×2 | I2C1 | 42/47 | 아날로그 입력 (포텐쇼미터) |
| 자이로 모듈 (MPU-6050, `0x68`) | I2C0 | 9/10 | 가속도·자이로 |
| LCD 모듈 (PCF8574+HD44780, `0x27`) | I2C0 | 9/10 | 디스플레이 |
| 휠스피드센서 ×4 (FL/FR/RL/RR) | GPIO In | 11–14 | rising-edge 펄스 |
| 상태 LED | GPIO OD | 5 | **장착 완료** (외부 LED, 이미 결선). 상태 표시 |

> I2C0(GPIO9/10) 한 버스에 자이로(`0x68`)·LCD(`0x27`) 두 모듈이 병렬로 붙습니다.
> 설정 리셋(GPIO21) 버튼은 사용하지 않으므로 제거함.

### ③ 우리가 실제 사용하는 센서 → 연결 위치

| 사용자 센서 | 연결 방식 | GPIO / 채널 |
|-------------|-----------|-------------|
| 휠스피드센서 FL | 디지털 입력 DIN1 | GPIO11 |
| 휠스피드센서 FR | 디지털 입력 DIN2 | GPIO12 |
| 휠스피드센서 RL | 디지털 입력 DIN3 | GPIO13 |
| 휠스피드센서 RR | 디지털 입력 DIN4 | GPIO14 |
| 선형 포텐쇼미터 ×4 | ADS1115 모듈 A채널 | adc1 A0~A3 (ain1~ain4) |
| ADS1115 모듈 ×2 | I2C1 | GPIO42/47 |
| GPS 모듈 | UART1 | GPIO17/18 |
| 디스플레이 모듈 | I2C0 | GPIO9/10 |
| 캔 트랜시버 모듈 | TWAI | GPIO15/16 |
| SD카드 모듈 | SPI2 | GPIO39/40/41/48 |
| 자이로센서 | I2C0 | GPIO9/10 |

## 배선 체크리스트 (GPIO 번호순)

| GPIO | 인터페이스 | 신호 | 연결 대상 | 정의 위치 |
|:----:|-----------|------|-----------|-----------|
| 9  | I2C0 SDA        | SDA       | 자이로·LCD 공유 버스       | `main/main.c:231` |
| 10 | I2C0 SCL        | SCL       | 자이로·LCD 공유 버스       | `main/main.c:230` |
| 11 | GPIO In (ISR)   | DIN1      | 휠스피드센서 FL (앞 왼쪽)   | `main/peripheral/digital.c` |
| 12 | GPIO In (ISR)   | DIN2      | 휠스피드센서 FR (앞 오른쪽) | `main/peripheral/digital.c` |
| 13 | GPIO In (ISR)   | DIN3      | 휠스피드센서 RL (뒤 왼쪽)   | `main/peripheral/digital.c` |
| 14 | GPIO In (ISR)   | DIN4      | 휠스피드센서 RR (뒤 오른쪽) | `main/peripheral/digital.c` |
| 15 | TWAI (CAN) TX   | CAN TX    | CAN 트랜시버              | `main/peripheral/can.c:51` |
| 16 | TWAI (CAN) RX   | CAN RX    | CAN 트랜시버              | `main/peripheral/can.c:51` |
| 17 | UART1 TX        | → GPS RX  | u-blox GPS                | `main/peripheral/gps.c:64` |
| 18 | UART1 RX        | ← GPS TX  | u-blox GPS                | `main/peripheral/gps.c:64` |
| 39 | SPI2 SCK        | SD SCK    | SD 카드 모듈 (SDSPI)      | `main/peripheral/sdcard.c:13` |
| 40 | SPI2 MOSI       | SD MOSI   | SD 카드 모듈 (SDSPI)      | `main/peripheral/sdcard.c:14` |
| 41 | SPI2 CS         | SD CS     | SD 카드 모듈 (SDSPI)      | `main/peripheral/sdcard.c:16` |
| 42 | I2C1 SCL        | SCL       | ADS1115 모듈 ×2 (헤더 SCL) | `main/peripheral/analog.c:58` |
| 47 | I2C1 SDA        | SDA       | ADS1115 모듈 ×2 (헤더 SDA) | `main/peripheral/analog.c:59` |
| 48 | SPI2 MISO       | SD MISO   | SD 카드 모듈 (SDSPI)      | `main/peripheral/sdcard.c:15` |

---

## 인터페이스별 결선

### I2C

| 버스 | SDA | SCL | 디바이스 (주소) | 정의 위치 |
|------|:---:|:---:|-----------------|-----------|
| `I2C_NUM_0` | GPIO9 | GPIO10 | MPU-6050 자이로 `0x68`, PCF8574 LCD `0x27` | `main/main.c:230-231` |
| `I2C_NUM_1` | GPIO47 | GPIO42 | ADS1115 모듈 `0x48`, ADS1115 모듈 `0x49` | `main/peripheral/analog.c:58-59` |

- I2C0는 `i2c0_init()`에서 1회 초기화하고, 자이로/디스플레이 태스크는 `i2c_master_get_bus_handle()`로 핸들을 공유합니다.

#### ADS1115 파란색 모듈보드 ×2 (I2C1 버스 공유)

I2C는 버스라 두 모듈이 SDA/SCL을 공유하고, **ADDR 핀 결선으로 주소만 분리**합니다. 모듈 헤더 → ESP32 결선:

| 모듈 헤더 | 연결 | 비고 |
|-----------|------|------|
| VDD  | **3.3V** | ★ 5V 금지 — 온보드 풀업이 I2C 라인을 5V로 끌어올려 ESP32-S3 손상 |
| GND  | GND | |
| SCL  | GPIO42 | 두 모듈 공통 |
| SDA  | GPIO47 | 두 모듈 공통 |
| ADDR | 모듈1→GND / 모듈2→VDD | 주소 분리 (아래) |
| ALRT | **미연결** | ALERT/RDY 펌웨어 미사용 |
| A0~A3 | 측정 대상 입력 | 채널 매핑은 아래 |

| 모듈 | I2C 주소 | ADDR | A0 | A1 | A2 | A3 |
|------|:--------:|------|----|----|----|----|
| 모듈1 (adc1) | `0x48` | →GND | ain1 | ain2 | ain3 | ain4 |
| 모듈2 (adc2) | `0x49` | →VDD | ain5 | ain6 | ain7 | ain8 |

- 모듈도 동일 ADS1115 칩이라 펌웨어 로직(레지스터·MUX·±4.096V FSR·860SPS 변환)은 베어 IC와 동일하며 코드 변경 없음. 정의: `main/peripheral/analog.c:20-31,37,42`.
- 선형 포텐쇼미터 등 아날로그 센서는 모듈 헤더 A0~A3에 물립니다.
- 두 모듈의 온보드 10k 풀업이 병렬(≈5k)로 걸립니다. 기존 보드에 베어 IC용 외부 풀업이 따로 있었다면 풀업이 과해지므로 한쪽을 제거하는 게 안전합니다(하드웨어 확인 사항).

### SPI (SPI2_HOST) — SD 카드 모듈

| 신호 | GPIO | 정의 위치 |
|------|:----:|-----------|
| SCK  | GPIO39 | `main/peripheral/sdcard.c:13` |
| MOSI | GPIO40 | `main/peripheral/sdcard.c:14` |
| MISO | GPIO48 | `main/peripheral/sdcard.c:15` |
| CS   | GPIO41 | `main/peripheral/sdcard.c:16` |

- SD 카드는 SDMMC가 아닌 **SDSPI** 모드, 프로빙 속도 400kHz.

### UART — GPS

| 포트 | TX | RX | 대상 | 정의 위치 |
|------|:--:|:--:|------|-----------|
| `UART_NUM_1` | GPIO17 | GPIO18 | u-blox GPS (보율 자동탐지) | `main/peripheral/gps.c:64` |

### CAN (TWAI)

| 신호 | GPIO | 정의 위치 |
|------|:----:|-----------|
| TX | GPIO15 | `main/peripheral/can.c:51` |
| RX | GPIO16 | `main/peripheral/can.c:51` |

### 디지털 입력

| GPIO | 신호 | 모드 | 정의 위치 |
|:----:|------|------|-----------|
| 11 | DIN1 (FL) | Input + ISR (pull-down, posedge) | `main/peripheral/digital.c` |
| 12 | DIN2 (FR) | Input + ISR (pull-down, posedge) | `main/peripheral/digital.c` |
| 13 | DIN3 (RL) | Input + ISR (pull-down, posedge) | `main/peripheral/digital.c` |
| 14 | DIN4 (RR) | Input + ISR (pull-down, posedge) | `main/peripheral/digital.c` |

- 휠스피드센서 4개(FL/FR/RL/RR)가 여기에 연결됩니다.

---

## 배선 불필요 (참고)

아래 항목은 **외부 배선이 필요 없어** 메인 체크리스트에서 제외했습니다. 코드에는 그대로 남아 있습니다.

| GPIO | 항목 | 분류 | 이유 | 정의 위치 |
|:----:|------|------|------|-----------|
| 38 | 온보드 RGB LED  | 온보드        | DevKitC-1 v1.1 보드 내장, 배선 대상 아님. task_led 상태색 표시 (초록/노랑/빨강) | `main/main.c` |
| 43/44 | UART0 콘솔   | ESP-IDF 기본  | 콘솔/모니터용. USB로 접근, 따로 배선 안 함 | ESP-IDF 기본 |
| 19/20 | USB         | ESP-IDF 기본  | USB 케이블로만 사용 | ESP-IDF 기본 |

---

## 핀 중복·충돌 / 여유 핀

- **배선 대상 핀(5, 9–18, 39–42, 47, 48) 간 GPIO 중복 없음.** 모두 고유합니다.
- **GPIO48:** v1.0에서는 온보드 WS2812B였으나, **우리 보드는 v1.1로 확정** — v1.1에서 GPIO48은 자유 핀이므로 SD MISO 배정 정상. v1.0 데이터 인용 금지. (I2C1 SCL은 커밋 `1734a02`에서 GPIO48→42로 재배정 완료.)
- **GPIO21:** 설정 리셋 버튼을 사용하지 않기로 하여 펌웨어에서 제거됨 → 현재 미사용 자유 핀.
- **여유 핀:** GPIO8(ADC1_CH7, 미사용), GPIO21(리셋 버튼 제거 후 자유), GPIO45/46(스트래핑, 미사용). 스트래핑 핀 GPIO0/3도 미사용이라 부팅 안전. GPIO38은 온보드 RGB LED가 점유합니다.
