# Monolith v2

![](images/wide.jpg)

## 개요

모노리스는 TMA-1 데이터로거와 웹 기반 Control Hub로 구성된 무선 데이터로깅 플랫폼입니다.

Formula Student 및 Baja Student 대회에 참가하는 학생들의 차량 데이터 수집을 위해 개발되었으나, 다른 분야의 데이터 계측이나 ESP32 개발 보드로도 활용할 수 있습니다.

모든 소스코드가 [GitHub](https://github.com/CreatOurCar/monolith)에 공개되어 있으며, 비상업적 용도에 한해 🍺[Beerware License](https://spdx.org/licenses/Beerware.html)로 자유롭게 사용할 수 있습니다.

### 기능

📡 무선 통신 지원

* 실시간 텔레메트리
* 로그 데이터 원격 다운로드
* 원격 사용자 이벤트 전송
* 원격 CAN 메시지 전송
* 장치 설정 변경

📀 최대 100 Hz 데이터로깅

* 1x CAN 2.0(A/B)
* 1x 외장 GPS 모듈
* 1x 6축 가속도계 & 자이로
* 4x 디지털 입력<sup>1</sup>
* 6x 아날로그 입력<sup>1</sup>
* 1x 전원 전압 센서
* 1x 칩 온도 센서

<sup>1</sup> mini 버전에서는 지원하지 않습니다.

💡 웹 기반 데이터 분석 도구

모든 무선 통신 기능은 Wi-Fi 연결이 필요합니다. 차량에서 사용하기 위해서는 드라이버가 Wi-Fi 핫스팟을 켠 휴대폰을 가지고 타야 합니다.

### 미리보기

#### v1 대비 변경사항

![](images/compare.jpg)

* 크기와 제작 비용이 모두 3분의 1로 줄었습니다.
* 데이터 처리 성능과 무선통신 안정성이 대폭 개선되었습니다.
* 원격으로 로그를 다운로드할 수 있습니다. 데이터를 확인하기 위해 SD카드를 뽑을 필요가 없습니다.
* 원격으로 사용자 이벤트를 로그에 삽입하거나 CAN 메시지를 전송할 수 있습니다.
* 펌웨어 플래싱 없이도 원격으로 데이터로거 설정을 변경할 수 있습니다.
* 웹 UI로 모든 기능을 제공하므로 전용 프로그램을 따로 설치할 필요가 없습니다.

#### Original vs mini

Mini 버전은 Original 대비 절반 가까운 크기로, 신용카드보다도 작은 크기를 자랑합니다.

대신, 작은 크기를 위해 디지털 및 아날로그 입력 채널이 제거되었습니다. 그 외의 다른 기능은 모두 동일합니다.

## Do It Yourself!

### 펌웨어 업로드

1. 3.3V UART to USB 컨버터를 준비합니다.
    * ⚠️ 컨버터에는 `RX`, `TX` 외에도 `DTR`, `RTS` 핀이 **반드시** 있어야 합니다.
    * ⚠️ 5V 컨버터는 별도의 3.3V 전압 선택 스위치가 없다면 사용할 수 없습니다.
1. 보드의 UART 커넥터에 2x3 2.54mm 핀 헤더를 납땜합니다.
1. 각 핀을 다음과 같이 연결합니다.
    * `3V3`, `GND`, `DTR`, `RTS`: 컨버터에 있는 같은 이름의 핀과 연결
    * `RX`, `TX`: 컨버터의 핀과 서로 교차하여 연결 (`RX` ↔ `TX`)
1. [Release](https://github.com/CreatOurCar/monolith/releases/latest)에서 `monolith-{version}.zip`을 다운받고 압축을 해제합니다.
1. `flash.sh` (Linux/macOS) 또는 `flash.bat` (Windows) 파일을 실행합니다.
    * `python` 이 설치되어 있어야 합니다.

### 서버 준비

TMA-1과 Control Hub가 서로 통신하려면 MQTT 서버(브로커)가 필요합니다.

서버는 로컬 네트워크에 직접 호스팅합니다. 아래 가이드를 따라 직접 배포할 수 있습니다.

<details>

<summary>직접 서버 배포하기</summary>

<h4>서버 배포</h4>

기본 서버를 사용하지 않는다면 상용 MQTT 브로커를 사용하거나 아래 가이드를 따라 직접 서버를 배포해 사용할 수 있습니다.

아래 가이드는 DNS, 방화벽 등 서버 관련 지식이 어느 정도 있다고 가정합니다.

[Docker Engine](https://docs.docker.com/engine/install/)과 [Node.js](https://nodejs.org/en/download)가 설치된 리눅스 머신이 필요합니다.
없다면 [여기](https://www.oracle.com/cloud/free/)서 무료로 인스턴스를 만들 수 있습니다.

이론상 Docker Desktop과 Windows 머신으로도 할 수 있지만, 테스트해본 적은 없습니다.

아래 명령을 실행합니다. `<YOUR_CHANNEL_NAME>`은 사용할 이름으로 변경합니다.

```sh
sudo apt install -y mosquitto
git clone https://github.com/CreatOurCar/monolith.git

cd monolith/web
npm install
npm run build

cd ../server/config
# set your channel key as the password
mosquitto_passwd -c mosquitto.passwd <YOUR_CHANNEL_NAME>

cd ..
cp .env.example .env
vi .env # set `ACME_EMAIL` and `DOMAIN_NAME` to your own

sudo docker compose up -d
```

##### 서버 공지사항

`.env` 파일의 `ANNOUNCEMENT` 환경변수를 설정하면 Control Hub 접속 시 공지사항 팝업을 표시할 수 있습니다.

```sh
ANNOUNCEMENT=서버 점검 예정: 2025-01-15 02:00 ~ 04:00 (KST)
```

</details>

## 사용법

### TMA-1

#### Wi-Fi

TMA-1의 무선 통신 기능(실시간 텔레메트리 및 데이터 다운로드 등)을 사용하기 위해서는 2.4GHz Wi-Fi 연결이 필요합니다.

데이터로깅 기능은 Wi-Fi 연결이 없어도 동작합니다. 오프라인으로 사용하려면 주행 이후 SD 카드를 분리하여 PC에 마운트하면 됩니다.

다만 TMA-1의 내부 시계는 네트워크를 통해 SNTP로 자동 동기화되므로, 최소한 한 번은 인터넷에 연결해 시간을 동기화해야 합니다.

##### 초기 설정

1. 장치에 전원을 공급합니다.
1. 첫 부팅 시 `Monolith v2 XXXXXX` 라는 자체 Wi-Fi AP가 생성됩니다. 비밀번호는 `monolith`입니다.
1. 해당 AP에 연결하면 자동으로 설정 페이지가 열립니다. 열리지 않으면 브라우저에서 [http://192.168.4.1](http://192.168.4.1)에 직접 접속합니다.\
   ![](images/ap.png)
1. TMA-1이 주행 중 연결할 휴대폰(드라이버가 들고 탈 휴대폰)의 핫스팟 정보를 `Wi-Fi SSID`와 `Wi-Fi Password`에 입력합니다.
1. `Server Address`에 사용할 서버 주소를 입력합니다.
1. 서버의 채널 이름 및 키와 동일한 값을 `Device Name`과 `Device Key`에 입력합니다.
1. `Save`를 클릭하고 `Reboot`를 클릭합니다.

재부팅 이후 TMA-1은 설정된 Wi-Fi로 연결을 시도합니다.

##### 초기화

Wi-Fi와 서버 설정을 초기화하려면 리셋 버튼을 3초 이상 누르고 뗍니다.
Wi-Fi, 서버, 디바이스 이름과 키 등 모든 설정이 초기값으로 복원됩니다.

초기화 후 위 `초기 설정` 단계부터 다시 진행합니다.

### Control Hub

Control Hub는 직접 호스팅한 서버에서 제공되는 웹 앱입니다 (위 `서버 준비` 참고).

#### Live Telemetry

실시간 텔레메트리를 사용하려면 먼저 `Device Configuration` 탭에서 서버 정보를 설정해야 합니다.

또한, 수신되는 CAN 데이터를 보려면 먼저 `UI Configuration` 탭에서 CAN Decoder를 설정해야 합니다.

서버를 잘 설정했고 TMA-1이 네트워크에 연결되어 있다면 모든 기능은 알아서 잘 작동합니다.

GPS 카드에서 `Fix` / `No Fix` 태그로 위성 수신 상태를 확인할 수 있습니다. 궤적 표시 모드를 `Speed` (초록=저속, 빨강=고속) 또는 `Time` (남색=과거, 초록=최근)으로 전환할 수 있습니다.

##### Console

사용자 이벤트나 CAN 메시지를 장치로 전송할 수 있습니다.

![](images/console.png)

* 사용자 이벤트
    * 사용자 이벤트는 주행 이후 데이터 리뷰에 도움이 되는 의미 있는 지점을 표시합니다.
    * 이벤트를 전송하려면 이름을 입력하고 전송 버튼을 누릅니다.
    * 이벤트 이름을 입력하지 않으면 `USREVT`로 기록됩니다.
    * 이벤트 이름은 16바이트 이하의 ASCII 문자열만 사용 가능합니다.
* CAN 메시지
    * 전송할 CAN 메시지 ID(11/29비트)와 데이터를 입력하고 전송 버튼을 클릭합니다.
    * 입력하지 않은 데이터 바이트들은 `0x00`으로 전송됩니다.

#### Data Viewer

주행 데이터를 열람하려면 먼저 기록을 다운로드해야 합니다.
`Device Configuration` 탭의 `Data Downloader` 섹션을 참고해 데이터를 다운로드합니다.

* `Select`를 눌러 다운받은 `*.log` 파일을 엽니다.
* `Graph` 카드에서 입력 카테고리 버튼이나 범례의 신호 이름을 눌러 그래프를 활성화합니다.
* `GPS` 카드에서 차량의 이동 궤적을 색상 그라디언트로 확인할 수 있습니다. `Speed` (초록=저속, 빨강=고속) 또는 `Time` (남색=과거, 초록=최근) 모드로 전환할 수 있으며, 슬라이더를 조절하면 특정 시점의 위치, 속도, 방위각을 볼 수 있습니다.
* `CAN` 카드에서 기록된 CAN 메시지의 통계를 확인할 수 있습니다: 메시지 ID, 총 수신 횟수, 평균 주기(Hz / ms), DLC, 마지막 데이터 바이트를 표시합니다.

기록된 CAN 데이터를 보려면 `UI Configuration` 탭에서 CAN Decoder를 먼저 설정해야 합니다.

#### UI Configuration

Live Telemetry와 Data Viewer의 각 카드의 표시 여부를 조정하거나 입력 신호의 이름과 단위, 배율을 설정할 수 있습니다.

변경 사항을 적용하려면 페이지를 새로고침해야 합니다.

##### Import/Export

현재 UI 설정을 내보내고 다른 기기나 브라우저에서 가져올 수 있습니다.

##### Display

Live Telemetry 및 Data Viewer에서 각 카드의 표시 여부를 제어합니다.

##### Units

아날로그 및 CAN 데이터에 사용할 사용자 정의 단위를 관리합니다.

##### Digital

채널 이름을 변경할 수 있습니다.

##### Analog

![](images/analog_ui.png)

* `Name`: 그래프에 표시할 이름
* `Voltage Divider`: 해당 채널 입력이 1/2로 분배되는 경우 스위치를 켭니다.
* `Multiplier`: 측정된 실제 전압에 곱해지는 값입니다. 다음과 같은 경우에 수정합니다.
    * 센서에 추가 전압 분배 회로가 있는 경우
    * 원래 물리량을 계산하는 수식이 있는 경우
* `Unit`: 해당 데이터의 단위. 적절한 단위가 없다면 `Units` 카드에서 추가합니다.

##### CAN

CAN 메시지 디코더를 관리합니다. 디코더는 CAN 페이로드에서 유효한 데이터를 추출합니다.

![](images/can_decoder.png)

* `Name`: 그래프에 표시할 이름
* `CAN Message ID`: 원하는 데이터가 포함된 CAN 메시지 ID
* `Multiplier`: 원본 값에 곱할 배율
* `Offset`: 곱셈 후 더할 값. 최종 값은 `multiplier × 원본 + offset`이며, 기본값은 `0`입니다.
* `Unit`: 해당 데이터의 단위. 적절한 단위가 없다면 `Units` 카드에서 추가합니다.
* `Data Range`: CAN 페이로드에서 데이터가 포함된 범위
    * CAN 페이로드는 최대 8바이트입니다.
        * `Byte` 모드: #0 ~ #7
        * `Bit` 모드: #0 ~ #63
        * 단일 바이트나 비트를 선택하려면 시작과 끝 값을 동일하게 설정합니다.
* `Data Signedness`
    * `Unsigned`: 데이터를 부호 없는 값으로 해석
    * `Signed`: 데이터를 부호 있는 2의 보수로 해석
* `Data Endianness`
    * `Byte` 모드에서만 선택 가능합니다.
    * 멀티바이트 데이터의 엔디언을 정의합니다.
* `Data Filter` / `Data Mask` (선택): CAN 페이로드 내용으로 메시지를 필터링하는 HEX 값입니다. `(data & mask) == filter` 조건을 만족하는 메시지만 디코딩됩니다. 둘 다 함께 입력하거나 모두 비워야 합니다.

#### Device Configuration

Control Hub가 연결할 서버와 장치 설정을 변경합니다.

장치가 인터넷에 연결되어 있다면 설정값을 자동으로 불러옵니다.

##### Server

* `Address`: Control Hub가 사용할 서버 주소
* `Name` / `Key`: 채널 이름 및 키

모든 값은 `TMA-1 - 초기 설정` 단계에서 장치에 입력한 값과 일치해야 합니다.

저장 버튼을 누르면 자동으로 서버에 연결합니다.

##### Device

* `SSID` / `Password`: TMA-1이 연결할 Wi-Fi 네트워크
* `Timezone`: 지역에 맞는 POSIX 타임존 문자열. [변환기](https://phpsecu.re/tz/)의 `TZ_INFO` 값을 사용하세요.
    * 이 값은 로그 파일 이름에 사용될 시간대를 결정합니다. 기록되는 데이터들의 시간은 UTC를 사용하므로 무관합니다.
* `T. Interval`: 텔레메트리 전송 주기

##### Inputs

디지털 및 아날로그 입력 채널의 로깅 여부를 제어합니다.

##### CAN

* `Enabled`: CAN 버스 로깅 여부
* `Bit rate`: CAN 버스의 Baud rate
* `Filter`: 수신할 메시지 ID (11/29비트)
* `Mask`: 필터의 각 비트에 대한 통과 규칙
    * `0`: 해당하는 필터 비트가 일치해야 통과
    * `1`: 해당하는 필터 비트 값을 무시 (don’t care)

`Filter`와 `Mask`에 대한 자세한 내용은 [ESP32-S3 API Reference](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32s3/api-reference/peripherals/twai.html#acceptance-filter)의 *Acceptance Filter* 항목을 참고하세요. 기본 설정은 모든 CAN 메시지를 허용합니다.

##### GPS

* `Enabled`: GPS 위치 로깅 여부
* `Device`: GPS 모듈 종류를 선택합니다. 현재는 `UBLOX`만 사용 가능합니다.

##### [Danger Zone](https://www.youtube.com/watch?v=siwpn14IE7E)

* `Refresh`: 장치 설정값을 다시 불러옵니다.
    * 변경된 설정은 장치를 재시작해야 적용됩니다.
    * 재시작하기 전에 새로고침하면 이전 설정값이 다시 로드됩니다.
* `Restart`: 장치 재시작
* `Reset`: 장치 초기화

##### Data Downloader

![](images/downloader.png)

* `Load List`: 현재 세션을 제외한 모든 로그 파일 목록을 불러옵니다.
* `Delete All`: 현재 세션을 제외한 모든 로그 파일을 삭제합니다.

파일 목록을 불러온 뒤, 원하는 파일의 다운로드 버튼을 눌러 해당 파일을 다운로드합니다.

## Development

직접 소프트웨어를 고쳐 사용하고 싶은 경우에만 필요합니다.

<details>

<summary>세부 사항</summary>

<h3>펌웨어</h3>

1. [GitHub 저장소](https://github.com/CreatOurCar/monolith)에 ⭐를 누릅니다.
1. [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/index.html)를 설치하고 아래 명령을 실행합니다.

```
git clone https://github.com/CreatOurCar/monolith.git
cd monolith/device/firmware
make build
make run   # build & flash
```

<h3>Control Hub</h3>

1. [GitHub 저장소](https://github.com/CreatOurCar/monolith)에 ⭐를 누릅니다.
1. 아래 명령을 실행합니다.

```
git clone https://github.com/CreatOurCar/monolith.git
cd monolith/web
npm install
npm run vite
npm run build
```

</details>