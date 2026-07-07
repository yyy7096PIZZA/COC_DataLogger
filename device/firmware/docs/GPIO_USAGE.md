# ESP32-S3 GPIO 사용 현황 (Monolith v2)

- **보드/모듈:** ESP32-S3-DevKitC-1 **v1.1** + ESP32-S3-WROOM-1 **N16R8** (16MB Flash + 8MB Octal PSRAM)
- **온보드 RGB LED:** GPIO38 (v1.1 기준. 공식 문서 *"Addressable RGB LED, driven by GPIO38"*)
- **분류 범례:** `사용중` = 현재 펌웨어가 점유 / `자유(안전)` = 제약 없이 사용 가능 / `자유(조건부)` = 스트래핑·USB·콘솔 등 주의 후 사용 / `예약·금지` = 칩에 없거나 내장 플래시·PSRAM
- 코드 근거는 `main/` 소스 기준. 칩 제약은 ESP32-S3 / WROOM-1 N16R8 datasheet 기준.

| GPIO | 기능 | 분류 | 비고 | 코드 근거 |
|:----:|------|:----:|------|-----------|
| 0  | 부팅 스트래핑 (BOOT_SEL) | 자유(조건부) | LOW=다운로드/HIGH=일반부팅, 내부 약풀업. 부팅 후 런타임 사용 가능, 부팅 중 LOW 인가 금지 | — |
| 1  | 미점유 | 자유(안전) | ADC1_CH0 (아날로그 확장 가능) | — |
| 2  | 미점유 | 자유(안전) | ADC1_CH1 | — |
| 3  | JTAG 스트래핑 (JTAG_SEL) | 자유(조건부) | IDF 기본 JTAG 활성 시 점유, 비활성화 후 사용 가능 | — |
| 4  | 미점유 | 자유(안전) | ADC1_CH3 | — |
| 5  | 상태 LED (OUTPUT_OD) | 사용중 | 외부 상태표시 LED **장착됨**. task_led가 상태별 점멸 | `main/main.c:83,117` |
| 6  | 미점유 | 자유(안전) | ADC1_CH5 | — |
| 7  | 미점유 | 자유(안전) | ADC1_CH6 | — |
| 8  | 미점유 | 자유(안전) | ADC1_CH7 (아날로그 확장 가능) | — |
| 9  | I2C0 SDA — 자이로·LCD | 사용중 | 공유 버스 (MPU6050 0x68, PCF8574 0x27) | `main/main.c:231` |
| 10 | I2C0 SCL — 자이로·LCD | 사용중 | 공유 버스 | `main/main.c:230` |
| 11 | DIN1 디지털 입력 (ISR) | 사용중 | pull-down + ANYEDGE | `main/peripheral/digital.c:19,36` |
| 12 | DIN2 디지털 입력 (ISR) | 사용중 | | `main/peripheral/digital.c:19,37` |
| 13 | DIN3 디지털 입력 (ISR) | 사용중 | | `main/peripheral/digital.c:19,38` |
| 14 | DIN4 디지털 입력 (ISR) | 사용중 | | `main/peripheral/digital.c:19,39` |
| 15 | TWAI TX (CAN) | 사용중 | CAN 트랜시버 | `main/peripheral/can.c:51` |
| 16 | TWAI RX (CAN) | 사용중 | CAN 트랜시버 | `main/peripheral/can.c:51` |
| 17 | UART1 TX → GPS RX | 사용중 | u-blox GPS | `main/peripheral/gps.c:64` |
| 18 | UART1 RX ← GPS TX | 사용중 | u-blox GPS | `main/peripheral/gps.c:64` |
| 19 | USB D− (Serial/JTAG) | 자유(조건부) | 칩 내장 USB PHY. 플래싱·JTAG 사용 시 필수 점유, USB 미사용 PCB만 GPIO 전용 가능 | — |
| 20 | USB D+ (Serial/JTAG) | 자유(조건부) | 위와 동일 | — |
| 21 | 미점유 | 자유(안전) | 설정 리셋 버튼을 사용하지 않기로 하여 펌웨어에서 제거됨 | — |
| 22 | 칩에 없음 | 예약·금지 | ESP32-S3 GPIO 범위: 0–21, 26–48 | — |
| 23 | 칩에 없음 | 예약·금지 | | — |
| 24 | 칩에 없음 | 예약·금지 | | — |
| 25 | 칩에 없음 | 예약·금지 | | — |
| 26 | 내장 SPI 플래시 | 예약·금지 | WROOM-1 모듈 내부 배선, 외부 미노출 | — |
| 27 | 내장 SPI 플래시 | 예약·금지 | | — |
| 28 | 내장 SPI 플래시 | 예약·금지 | | — |
| 29 | 내장 SPI 플래시 | 예약·금지 | | — |
| 30 | 내장 SPI 플래시 | 예약·금지 | | — |
| 31 | 내장 SPI 플래시 | 예약·금지 | | — |
| 32 | 내장 SPI 플래시 | 예약·금지 | | — |
| 33 | Octal PSRAM (N16R8) | 예약·금지 | `CONFIG_SPIRAM` 꺼져도 물리 배선 존재 → 외부 사용 금지 | — |
| 34 | Octal PSRAM (N16R8) | 예약·금지 | | — |
| 35 | Octal PSRAM (N16R8) | 예약·금지 | | — |
| 36 | Octal PSRAM (N16R8) | 예약·금지 | | — |
| 37 | Octal PSRAM (N16R8) | 예약·금지 | | — |
| 38 | WS2812B 온보드 RGB LED (led_strip RMT, task_led 상태색 표시) | 사용중 | DevKit v1.1 온보드 LED. 베어 모듈 PCB엔 LED 없으나 펌웨어가 RMT로 점유 → 외부 신호 금지 권장 | `main/main.c` |
| 39 | SD SPI SCK | 사용중 | SDSPI, 400kHz 프로빙 | `main/peripheral/sdcard.c:13` |
| 40 | SD SPI MOSI | 사용중 | | `main/peripheral/sdcard.c:14` |
| 41 | SD SPI CS | 사용중 | | `main/peripheral/sdcard.c:16` |
| 42 | I2C1 SCL — ADS1115 ×2 | 사용중 | 모듈 0x48 / 0x49 | `main/peripheral/analog.c:58` |
| 43 | UART0 TX (콘솔 기본) | 자유(조건부) | 코드 명시 배정 없음(IDF 자동). 콘솔 미사용·재배정 시 자유 | — |
| 44 | UART0 RX (콘솔 기본) | 자유(조건부) | 위와 동일 | — |
| 45 | 전압 스트래핑 (VDD_SPI) | 자유(조건부) | LOW=1.8V/HIGH=3.3V SPI 플래시 전압. 부팅 후 런타임 사용 가능, 외부 풀다운 금지 | — |
| 46 | ROM 메시지 스트래핑 | 자유(조건부) | LOW=억제/HIGH=출력. 부팅 후 런타임 사용 가능 (내부 약풀다운 — 추측) | — |
| 47 | I2C1 SDA — ADS1115 ×2 | 사용중 | | `main/peripheral/analog.c:59` |
| 48 | SD SPI MISO | 사용중 | v1.0에선 온보드 LED였으나 v1.1에선 자유 핀 → SD MISO로 정상 사용 | `main/peripheral/sdcard.c:15` |

---

## 구성품 구분 — 내장 / 외부 추가 / 사용 센서

위 GPIO 표를 부품 단위로 다시 묶은 것. 자세한 결선·주소는 [PINMAP.md](PINMAP.md) 참고.

### ① v1.1 보드 · WROOM-1 모듈에 이미 내장 (외부 배선 불필요)

| 항목 | GPIO | 비고 |
|------|:----:|------|
| 온보드 RGB LED (WS2812B) | 38 | DevKitC-1 v1.1 내장, task_led 상태색 표시 (초록/노랑/빨강, 저휘도) |
| BOOT 버튼 | 0 | 온보드 (다운로드 모드용) |
| RESET 버튼 | (EN) | 온보드 칩 리셋 (GPIO 아님) |
| 네이티브 USB | 19/20 | 온보드 USB 커넥터 |
| USB-UART 콘솔 | 43/44 | 온보드 브리지 경유 |
| 내장 SPI 플래시 | 26–32 | WROOM-1 모듈 내부 |
| Octal PSRAM | 33–37 | N16R8 모듈 내부 |
| 칩 내장 RTC 타이머 | — | **배터리 백업 없음** → 전원 끄면 시간 소실. 외부 RTC 모듈은 쓰지 않고, GPS(NMEA GPRMC) 첫 픽스로 벽시계를 1회 설정 |

### ② 외부에 추가로 달아야 하는 모듈 / 부품

| 모듈 · 부품 | GPIO | 용도 |
|-------------|------|------|
| 캔 트랜시버 모듈 | 15/16 | CAN |
| GPS 모듈 (u-blox) | 17/18 | 위치·속도 |
| SD 카드 모듈 | 39/40/41/48 | 로그 저장 |
| ADS1115 모듈 ×2 | 42/47 (I2C1) | 아날로그 입력 |
| 자이로 모듈 (MPU-6050, `0x68`) | 9/10 (I2C0) | 가속도·자이로 |
| LCD 모듈 (PCF8574, `0x27`) | 9/10 (I2C0) | 디스플레이 |
| 휠스피드센서 등 디지털 입력 ×4 | 11–14 | 펄스·온오프 |
| 상태 LED | 5 | 외부 LED, **장착됨** (상태 표시) |

> 설정 리셋(GPIO21) 버튼은 사용하지 않아 펌웨어에서 제거함.

### ③ 우리가 실제 사용하는 센서 → 연결 위치

| 사용자 센서 | GPIO / 채널 |
|-------------|-------------|
| 휠스피드센서 | DIN1–4 = GPIO11–14 |
| 선형 포텐쇼미터 ×4 | ADS1115 A0~A3 (ain1~ain4) |
| ADS1115 모듈 ×2 | GPIO42/47 (I2C1) |
| GPS 모듈 | GPIO17/18 (UART1) |
| 디스플레이 모듈 | GPIO9/10 (I2C0) |
| 캔 트랜시버 모듈 | GPIO15/16 (TWAI) |
| SD카드 모듈 | GPIO39/40/41/48 (SPI2) |
| 자이로센서 | GPIO9/10 (I2C0) |

---

## 요약

- **제약 없이 바로 쓸 수 있는 자유 핀:** `GPIO 1, 2, 4, 6, 7, 8` (모두 ADC1 채널) · `21` (리셋 버튼 제거 후 자유)
- **조건부 자유 핀 (주의 후 사용):** `0, 3` (스트래핑) · `19, 20` (USB) · `43, 44` (UART0 콘솔) · `45, 46` (스트래핑)
- **외부 신호 배선 금지:** `22–25` (칩에 없음) · `26–32` (내장 플래시) · `33–37` (N16R8 Octal PSRAM) · `38` (온보드 LED, DevKit 호환 위해 회피 권장)
- 펌웨어 무수정 전제 시 `사용중` 기능은 PCB에서 해당 GPIO로 그대로 배선해야 함.
