이 폴더(`CAN/`)는 자작 포뮬러 차량의 CAN 신호 정의 문서를 담는다. 실제 디코딩/표시는 이 레포가 아니라 외부 업스트림 사이트(https://v2.monolith.luftaquila.io/)에서 한다.

[프로젝트 현재 구조]
이 저장소는 CreatOurCar/monolith — luftaquila/monolith(Monolith v2) 기반(포크) 저장소다. **ESP32-S3 단일 칩 펌웨어 하나**가 CAN + 센서를 수집해 SD카드에 `.log` 파일로 기록하는 단독 로거다.
- WiFi / MQTT / 웹앱 / 중계 서버 / 외부 RTC는 모두 제거됨. 실시간 발행 없음.
- 기록된 SD `.log`는 오프라인에서 위 업스트림 사이트에 업로드해 분석한다.
- 펌웨어: `device/firmware/`, ESP-IDF v6.0.1, C.

[사용 센서 — 전부 사용]
- CAN: TWAI(GPIO15 TX/16 RX), 트랜시버 Adafruit CAN Pal(TJA1051T/3). 모든 프레임을 그대로 SD에 기록.
- GPS: u-blox(BN-880), UART1(GPIO17/18), NMEA GPRMC. 첫 유효 픽스로 시스템 시계 1회 설정.
- IMU: MPU-6500/6050 호환, I2C0 0x68, 100Hz.
- 아날로그: ADS1115 외장 ADC 2개(I2C1 0x48/0x49, SCL=GPIO42 / SDA=GPIO47), ain1~8.
- 디지털(휠스피드): GPIO11~14, ANYEDGE 인터럽트, 내부풀업 OFF(옵토커플러 경유).
- 디스플레이: I2C LCD(PCF8574 0x27 + HD44780), I2C0 공유버스, 1Hz. CAN RPM→차속 환산 표시.

[CAN 신호 (이 폴더의 주제 — `CAN_SIGNALS_SELECTED.md` 참조)]
- EZkontrol 모터컨트롤러: ID `0x180117EF`(METER 모드). B6~B7 LE = RPM raw (rpm = raw × 0.1 − 2000). 디스플레이 차속 계산에 사용.
- Daly BMS: ID `0x18904001`. B6~B7 BE = SOC raw (SOC% = raw × 0.1). 디스플레이 상단 표시.
- CAN ID 상수 정의: `device/firmware/main/include/config.h`.

[원칙]
- **SD 로그 바이너리 포맷 구조 변경 금지.** `device/firmware/main/include/protocol.h`의 24바이트 `log_t` 레이아웃 / `LOG_MAGIC`(0xAE) / `PROTOCOL_VERSION`(1) / 체크섬 / "BOOT 레코드가 항상 첫 레코드" 순서를 유지해야 업스트림 뷰어가 파일을 읽는다. 필드 이름 변경(바이트 그대로)은 괜찮지만 레이아웃/버전/매직/체크섬 변경은 사전 확인 필요.
- 하드웨어 검증이 필요한 부분(플래시/실차 테스트)은 보드가 있어야 확인 가능하므로 따로 표시할 것.

이 폴더(`CAN/`)는 자작 포뮬러 차량의 CAN 신호 정의 문서를 담는다. 실제 디코딩/표시는 이 레포가 아니라 외부 업스트림 사이트(https://v2.monolith.luftaquila.io/)에서 한다.

[프로젝트 현재 구조]
이 저장소는 CreatOurCar/monolith — luftaquila/monolith(Monolith v2) 기반(포크) 저장소다. **ESP32-S3 단일 칩 펌웨어 하나**가 CAN + 센서를 수집해 SD카드에 `.log` 파일로 기록하는 단독 로거다.
- WiFi / MQTT / 웹앱 / 중계 서버 / 외부 RTC는 모두 제거됨. 실시간 발행 없음.
- 기록된 SD `.log`는 오프라인에서 위 업스트림 사이트에 업로드해 분석한다.
- 펌웨어: `device/firmware/`, ESP-IDF v6.0.1, C.

[사용 센서 — 전부 사용]
- CAN: TWAI(GPIO15 TX/16 RX), 트랜시버 Adafruit CAN Pal(TJA1051T/3). 모든 프레임을 그대로 SD에 기록.
- GPS: u-blox(BN-880), UART1(GPIO17/18), NMEA GPRMC. 첫 유효 픽스로 시스템 시계 1회 설정.
- IMU: MPU-6500/6050 호환, I2C0 0x68, 100Hz.
- 아날로그: ADS1115 외장 ADC 2개(I2C1 0x48/0x49, SCL=GPIO42 / SDA=GPIO47), ain1~8.
- 디지털(휠스피드): GPIO11~14, ANYEDGE 인터럽트, 내부풀업 OFF(옵토커플러 경유).
- 디스플레이: I2C LCD(PCF8574 0x27 + HD44780), I2C0 공유버스, 1Hz. CAN RPM→차속 환산 표시.

[CAN 신호 (이 폴더의 주제 — `CAN_SIGNALS_SELECTED.md` 참조)]
- EZkontrol 모터컨트롤러: ID `0x180117EF`(METER 모드). B6~B7 LE = RPM raw (rpm = raw × 0.1 − 2000). 디스플레이 차속 계산에 사용.
- Daly BMS: ID `0x18904001`. B6~B7 BE = SOC raw (SOC% = raw × 0.1). 디스플레이 상단 표시.
- CAN ID 상수 정의: `device/firmware/main/include/config.h`.

[원칙]
- **SD 로그 바이너리 포맷 구조 변경 금지.** `device/firmware/main/include/protocol.h`의 24바이트 `log_t` 레이아웃 / `LOG_MAGIC`(0xAE) / `PROTOCOL_VERSION`(1) / 체크섬 / "BOOT 레코드가 항상 첫 레코드" 순서를 유지해야 업스트림 뷰어가 파일을 읽는다. 필드 이름 변경(바이트 그대로)은 괜찮지만 레이아웃/버전/매직/체크섬 변경은 사전 확인 필요.
- 하드웨어 검증이 필요한 부분(플래시/실차 테스트)은 보드가 있어야 확인 가능하므로 따로 표시할 것.
