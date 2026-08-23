# 선택형 센서 설정과 SD 상태 기록

센서 사용 여부는 `main/include/config.h`에서 정한 뒤 펌웨어를 다시 빌드·플래시한다. 예전에 NVS에 저장된
`can_en`, `gps_en`, `anl_en`, `dgt_en` 값은 삭제하지 않지만 더 이상 활성화 판단에 사용하지 않는다. SD 카드는
BOOT·설정·상태·측정 로그를 보존해야 하므로 항상 필수다.

## 설정값

| 설정 | 의미 | 기본값 |
|---|---|---:|
| `SENSOR_ENABLE_CAN` | CAN/TWAI | `1` |
| `SENSOR_ENABLE_GPS` | UART1 GPS | `1` |
| `SENSOR_ENABLE_GYRO` | MPU-6050 | `1` |
| `SENSOR_ENABLE_DISPLAY` | PCF8574 LCD | `1` |
| `SENSOR_ENABLE_ADS48` | ADS1115 주소 `0x48` | `1` |
| `SENSOR_ENABLE_ADS49` | ADS1115 주소 `0x49` | `1` |
| `SENSOR_ANALOG_MASK` | bit 0~7 = AIN1~AIN8 | `0xFF` |
| `SENSOR_WHEEL_MASK` | bit 0~3 = FL/FR/RL/RR | `0x0F` |
| `SENSOR_RETRY_MS` | 고장 상태 재탐색 간격 | `5000` |
| `SENSOR_FAILURE_THRESHOLD` | 정상 동작 중 연속 실패 허용 횟수 | `3` |

각 `SENSOR_ENABLE_*` 값은 `0` 또는 `1`이어야 한다. 마스크 범위를 벗어나거나 재시도 설정이 잘못되면
컴파일 단계에서 오류가 난다. ADS 모듈 설정이 채널 마스크보다 우선하므로, 예를 들어
`SENSOR_ENABLE_ADS49=0`이면 `SENSOR_ANALOG_MASK`의 AIN5~AIN8 비트가 켜져 있어도 실제 유효 채널은 OFF다.

OFF인 장치는 태스크, GPIO ISR, I2C/UART/TWAI 드라이버를 만들거나 주소를 탐색하지 않는다. I2C0은 자이로와
LCD가 모두 OFF일 때 생성하지 않으며, I2C1은 유효한 AIN이 하나도 없으면 생성하지 않는다. 휠은 마스크에 켜진
GPIO에만 ISR을 설치한다.

## SD 로그에서 ON/OFF와 고장 판별

로그 파일의 첫 레코드는 기존과 동일한 24바이트 BOOT다. 그 직후 센서 태스크를 시작하기 전에 모든 설정을
16바이트 `SYSTEM` 이벤트로 기록한다.

```text
CFG:GYR:OFF
CFG:ADS48:ON
CFG:AIN3:OFF
CFG:WHL_RR:ON
```

활성 장치의 런타임 상태도 같은 이벤트 타입을 사용한다.

```text
SNS:GYR:MISS
SNS:ADS49:ERR
SNS:ADS49:OK
```

- `MISS`: 부팅 후 처음 탐색했을 때 장치를 찾지 못함
- `ERR`: 정상 동작하던 장치가 연속 실패 임계값에 도달했거나 드라이버 오류가 발생함
- `OK`: 첫 탐색 성공 또는 고장 뒤 복구

같은 장애가 계속되는 동안 `MISS` 또는 `ERR`는 한 번만 기록하며, 이후에는 5초 간격으로 조용히 재탐색한다.
복구하면 장치를 다시 초기화하고 `OK`를 한 번 기록한다. CAN 프레임이 없는 idle 상태와 GPS의 위성 fix 없음은
장치 고장이 아니다.

모듈 전체가 OFF면 해당 측정 레코드를 만들지 않는다. 일부 AIN이나 휠만 OFF면 기존 고정 레이아웃을 유지하기
위해 그 필드를 `0`으로 기록한다. 실제 영점과 OFF를 구분하는 권위 있는 정보는 앞쪽의 `CFG:*:OFF` 이벤트다.
한 ADS1115가 빠져 있어도 다른 ADS1115가 정상이면 정상 모듈의 채널은 계속 기록한다.

BOOT의 `feature_flags`에는 `LOG_FEATURE_SENSOR_STATUS_EVENTS`가 추가되지만 `PROTOCOL_VERSION=1`, 레코드
크기, 타입 번호, 체크섬 및 필드 오프셋은 바뀌지 않는다.

## 시간 관련 주의

GPS를 OFF하면 GPS UTC를 이용한 절대시각 보정도 수행되지 않는다. 외부에서 시간을 설정하지 않았다면 파일명은
1970년 기반이며, NVS 부팅 카운터가 파일명 충돌을 막는다. 각 레코드의 타임스탬프는 부팅 후 경과시간 기준으로
계속 기록된다.

포텐쇼미터 단선은 정상적인 0 또는 잡음과 구별하기 어렵고, 정지한 휠은 미장착 휠과 구별할 수 없다. CAN도
무통신 자체는 정상 idle일 수 있으므로, 이 세 경우의 장착 여부는 자동 추정하지 않고 컴파일 설정을 기준으로 한다.
