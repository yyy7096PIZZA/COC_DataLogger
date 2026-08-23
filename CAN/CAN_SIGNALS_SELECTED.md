# CAN 디코더 명세서 — 선택 신호 (SELECTED)

> 전체 명세 중 **실제로 디코딩/표시할 신호만** 추린 버전. ui.js 디코더는 이 항목들만 구현한다.
> ESP32(Monolith)는 모든 CAN 프레임을 SD에 그대로 기록하므로, 나중에 같은 로그에서 다른 신호를 추가 디코딩할 수 있다. 여기 해석은 전부 **웹(ui.js)**에서 하며 펌웨어/로그 포맷은 안 건드린다.

## 공통 주의 (★중요)
- **엔디안이 장치마다 반대**: EZkontrol 모터컨트롤러 = **리틀엔디안**(`B_lo | B_hi<<8`), Daly BMS = **빅엔디안**(`B_hi<<8 | B_lo`).
- **디코드 공식**: EZkontrol `실제값 = raw×분해능 + 오프셋(음수)` / Daly `실제값 = (raw − 오프셋표기) × 분해능`.
- **버스 속도**: 둘 다 250K (컨트롤러가 500K 설정이 아닌지 확인).
- **로거는 수동 도청**: 모터컨트롤러는 주기 송신이라 잡힘. **Daly BMS는 폴링-응답**이라 버스에 BMS를 폴링하는 노드가 있거나 자동송신이 켜져 있어야 데이터가 흐른다(확인 필요).

## 확인 필요 (값 확정 전 전장팀 확인 — 코드엔 상수/주석으로)
1. 모터컨트롤러 프로토콜 모드 = **METER(1)** 가정 → ID `0x1801/0217EF`. VCU(2) 모드면 ID가 `0x1801/02D0EF`로 바뀜.
2. 모터컨트롤러 SA = 기본 **0xEF**(ID 끝바이트). 컨트롤러 번호 바꿨으면 변경.
3. Daly BMS 주소 = **0x01**, PC = **0x40** 가정 → BMS 송신 ID = `0x18[DataID]4001`.

---

## 1. 모터컨트롤러 (EZkontrol, 리틀엔디안, METER 모드 가정)

### ID `0x180117EF`
| 신호 | 바이트 | 디코드(실제값) | 단위 | 범위 |
|---|---|---|---|---|
| Speed | B6–B7 (LE u16) | raw × 0.1 − 2000 | rpm | −2000~2000 |
| Bus Voltage | B0–B1 (LE u16) | raw × 0.1 | V | 0~300 |
| Bus Current | B2–B3 (LE u16) | raw × 0.1 − 3200 | A | −3200~3200 |

### ID `0x180217EF`
| 신호 | 바이트 | 디코드 | 단위 | 범위 |
|---|---|---|---|---|
| Controller Temp | B0 (u8) | raw − 40 | ℃ | −40~210 |
| Motor Temp | B1 (u8) | raw − 40 | ℃ | −40~210 |
| Accelerator | B2 (u8) | raw | % | 0~100 |
| Error 플래그 전체 | B4·B5·B6 (비트필드 3바이트) | 비트별 1=고장 | — | 아래 |

**Error 비트맵 (B4/B5/B6, 1=고장)**
- **B4**: bit0 Overcurrent · bit1 Overload · bit2 Overvoltage · bit3 Undervoltage · bit4 Controller Overheat · bit5 Motor Overheat · bit6 Motor Stalled · bit7 Motor Out of phase
- **B5**: bit0 Motor Sensor · bit1 Motor AUX Sensor · bit2 Encoder Misaligned · bit3 Anti-Runaway · bit4 Main Accelerator · bit5 AUX Accelerator · bit6 Pre-charge · bit7 DC Contactor
- **B6**: bit0 Power valve · bit1 Current Sensor · bit2 Auto-tune · bit3 RS485 · bit4 CAN · bit5 Software

---

## 2. BMS (Daly, 빅엔디안)

### ID `0x18904001` (DataID 0x90)
| 신호 | 바이트 | 디코드 | 단위 |
|---|---|---|---|
| 총전압 | B0–B1 (BE u16) | raw × 0.1 | V |
| 전류 | B4–B5 (BE u16) | (raw − 30000) × 0.1 | A (방전−/충전+) |
| SOC | B6–B7 (BE u16) | raw × 0.1 | % |

### ID `0x18934001` (0x93)
| 신호 | 바이트 | 디코드 | 단위 |
|---|---|---|---|
| 잔여 용량 | B4–B7 (BE u32) | raw | mAh |

### ID `0x18984001` (0x98) — 고장 플래그 전체 (비트필드, 1=고장)
- **B0**: 셀 과전압 lv1/lv2 · 셀 저전압 lv1/lv2 · 총전압 과전압 lv1/lv2 · 총전압 저전압 lv1/lv2 (bit0~7)
- **B1**: 충전 고온 lv1/lv2 · 충전 저온 lv1/lv2 · 방전 고온 lv1/lv2 · 방전 저온 lv1/lv2
- **B2**: 충전 과전류 lv1/lv2 · 방전 과전류 lv1/lv2 · SOC 고 lv1/lv2 · SOC 저 lv1/lv2
- **B3**: 전압차 lv1/lv2 · 온도차 lv1/lv2 (bit0~3)
- **B4**: 충/방전 MOS 고온알람 · MOS 온도센서 오류 · MOS 접착/개방 오류 (bit0~7)
- **B5**: AFE 칩 오류 · 전압수집 끊김 · 셀온도센서 오류 · EEPROM · RTC · 프리차지 실패 · 통신 실패 · 내부통신 실패
- **B6**: 전류모듈 고장 · 총전압 검출 고장 · 단락보호 고장 · 저전압 충전금지 (bit0~3)
- **B7**: Fault code (값)

---

## 검산용 예시 (디코더 자체 검증)
- 모터 Speed, raw 2바이트 = `B6=0xD0, B7=0x84` → LE = 0x84D0 = 34000 → 34000×0.1 − 32000 = **1400 rpm**
- 모터 Bus Voltage, `B0=0x70, B1=0x03` → LE = 0x0370 = 880 → 880×0.1 = **88.0 V**
- BMS 전류, `B4=0x75, B5=0x30` → BE = 0x7530 = 30000 → (30000−30000)×0.1 = **0.0 A**
- BMS SOC, `B6=0x02, B7=0x71` → BE = 0x0271 = 625 → 625×0.1 = **62.5 %**
