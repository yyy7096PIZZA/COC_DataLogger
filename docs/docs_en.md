# Monolith v2

![](images/wide.jpg)

## Overview

Monolith is a wireless data logger platform: logger hardware (a.k.a. TMA-1) plus a web-based Control Hub. Built to help Formula Student / Baja teams collect data from their cars, but usable in any other application, or just as an ESP32-S3 development board.

Open-source & open-hardware, licensed under the 🍺[Beerware License](https://spdx.org/licenses/Beerware.html) for all non-commercial use. Details on [GitHub](https://github.com/CreatOurCar/monolith).

### Features

📡 Full wireless support

* Real-time telemetry
* Download recorded data
* Transmit User Events
* Transmit CAN messages
* Configure the device (e.g., CAN bit rate)

📀 Up to 100 Hz data rate across various signals

* 1x CAN 2.0(A/B)
* 1x External GPS
* 1x Internal 6-axis accelerometer & gyroscope
* 4x Digital input channels<sup>1</sup>
* 6x Analog input channels<sup>1</sup>
* 1x Power supply voltage sensor
* 1x Chip temperature sensor

<sup>1</sup> Not supported on mini version.

💡 Customizable web-based data analysis tool

All wireless features require a Wi-Fi connection to the Internet — on the car, bring a phone with a Wi-Fi hotspot on board.

### Preview

#### Compare with v1

![](images/compare.jpg)

* Size and build cost reduced to about 1/3.
* Better performance and telemetry stability.
* Wireless data download & configuration.
* Remote user event & CAN message transmit.

#### Original vs mini

The Mini version is nearly half the size of the Original — smaller than a credit card. Digital and analog input channels are removed; all other functionality is identical.

## Do It Yourself!

### Upload Firmware

1. Prepare a 3.3V UART to USB converter.
    * ⚠️ The converter **MUST** have both `DTR` and `RTS` in addition to `RX` and `TX`.
    * ⚠️ Do **NOT** use a converter with 5V output unless it has a 3.3V voltage selector.
1. Solder a 2x3 2.54mm pin header to the board's UART connector.
1. Connect each pin with the following pinout:
    * `3V3`, `GND`, `DTR`, `RTS`: corresponding pins on the converter.
    * `RX`, `TX`: cross-connect with the converter (`RX` ↔ `TX`).
1. Download and unzip `monolith-{version}.zip` from the [Release](https://github.com/CreatOurCar/monolith/releases/latest).
1. Run `flash.sh` (Linux/macOS) or `flash.bat` (Windows). Requires `python`.

### Prepare Server

The TMA-1 and the Control Hub need an MQTT server (broker) to communicate. We self-host the broker on our local network.

<details>

<summary>I want to set up the server on my own.</summary>

<h4>Deploy (Optional)</h4>

Use a commercial MQTT broker service, or deploy your own server:

***Prerequisites***: A Linux machine with [Docker Engine](https://docs.docker.com/engine/install/) and [Node.js](https://nodejs.org/en/download) — free option: [Oracle Cloud](https://www.oracle.com/cloud/free/). Assumes basic server knowledge (DNS records, firewalls). Docker Desktop on Windows is untested.

Run the commands below; replace `<YOUR_CHANNEL_NAME>` with your own name.

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

##### Server Announcement

Set the `ANNOUNCEMENT` environment variable in the `.env` file to display a notice popup when users open the Control Hub.

```sh
ANNOUNCEMENT=Scheduled maintenance: 2025-01-15 02:00 ~ 04:00 (UTC)
```

</details>

## Usage

### Device (TMA-1)

#### Wi-Fi

* TMA-1 requires a 2.4GHz Wi-Fi connection for wireless features (live telemetry, remote record downloads).
* Data logging works without Wi-Fi: remove the SD card and mount it on your computer after driving.
* The internal clock is set via SNTP — connect the device to the Internet at least once for time synchronization.

##### Initial Setup

1. Power up the device.
1. On first boot, it creates its own Wi-Fi access point (AP) named `Monolith v2 XXXXXX`. Password is `monolith`.
1. Connect to that AP. The setup page opens automatically in your browser; if not, navigate to [http://192.168.4.1](http://192.168.4.1) manually.\
    ![](images/ap.png)
1. Set `Wi-Fi SSID` and `Wi-Fi Password` to the phone's Wi-Fi hotspot that TMA-1 will connect to while driving (the phone the driver brings onboard).
1. Set `Server Address` to your server.
1. Set `Device Name` and `Device Key` to match your server's channel name and key.
1. Click `Save`, then click `Reboot`. TMA-1 connects to the configured Wi-Fi after rebooting.

##### Reset

Hold the reset button for 3 seconds and release: Wi-Fi, server, device name/key, and all configuration data return to factory defaults. Then redo `Initial Setup`.

### Control Hub

Control Hub is the web app served from your own server (see `Prepare Server` above).

#### Live Telemetry

Set the server information in the `Device Configuration` tab first. To view incoming CAN data, set the CAN Decoders in the `UI Configuration` tab.

The GPS card shows live position with a `Fix` / `No Fix` tag for satellite lock. Trail visualization: `Speed` mode (green=slow, red=fast) or `Time` mode (indigo=old, green=recent).

##### Console

Send a user event or a CAN message to the device.

![](images/console.png)

* User Events
    * Marks meaningful points for later data review.
    * Fill in the event name and click the send button. An empty name is recorded as `USREVT`.
    * Only ASCII characters up to 16 bytes are allowed.
* CAN message
    * Enter the CAN message ID (11/29 bits) and the data bytes, then click send. Empty data byte fields are sent as `0x00`.

#### Data Viewer

Download the recorded data first — see `Data Downloader` in the `Device Configuration` tab.

* Click `Select` and open a downloaded `*.log` file.
* `Graph` card: toggle the input category button or the signal name in the legend.
* `GPS` card: vehicle trajectory with a color gradient trail — `Speed` (green=slow, red=fast) or `Time` (indigo=old, green=recent). Use the slider to scrub position, speed, and heading at any point.
* `CAN` card: statistics for all recorded CAN messages — message ID, total count, average interval (Hz / ms), DLC, last data bytes.

To view recorded CAN data, set the CAN Decoders in the `UI Configuration` tab first.

#### UI Configuration

Adjust card visibility in Live Telemetry and Data Viewer, and set input signal names, units, and value multipliers. Refresh the page for changes to take effect.

##### Import/Export

Export/import the current UI configuration across devices/browsers.

##### Display

Control the visibility of the cards in each view.

##### Units

Manage custom units for analog and CAN data.

##### Digital

Change the channel name (e.g., `RUINED IF ON` instead of `DIN1`).

##### Analog

![](images/analog_ui.png)

* `Name`: The name shown on the graphs.
* `Voltage Divider`: Turn on if the channel's input is divided by half.
* `Multiplier`: The number multiplied to the measured voltage — edit if the sensor has another voltage divider circuit, or a formula to calculate the original physical value.
* `Unit`: The unit of the value. Add one at the `Units` card if missing.

##### CAN

Manage CAN message decoders. A decoder extracts data from the CAN message bytes.

![](images/can_decoder.png)

* `Name`: The name shown on the graphs.
* `CAN Message ID`: The message ID that contains the desired data.
* `Multiplier`: The number multiplied to the original value.
* `Offset`: Added after multiplication — final value is `multiplier × original + offset`. Default `0`.
* `Unit`: The data unit added on the `Units` card.
* `Data Range`: The part of the CAN payload (max 8 bytes) that contains the data.
    * `Byte` mode: #0 to #7. `Bit` mode: #0 to #63.
    * To select a single byte/bit, set the start and end range to the same value.
* `Data Signedness`: `Unsigned`, or `Signed` (2's complement).
* `Data Endianness`: `Byte` mode only — endianness of multi-byte data.
* `Data Filter` / `Data Mask` (optional): Hex values to filter CAN messages by payload content — only messages where `(data & mask) == filter` are decoded. Specify both together or leave both empty.

#### Device Configuration

Configures the server for the Control Hub and the device peripherals. Configurations load automatically if the device is online.

##### Server

* `Address`: The server for the Control Hub.
* `Name` / `Key`: Channel name and key.

All values must match those set during `Device - Initial Setup`. The server connects automatically after saving.

##### Device

* `SSID` / `Password`: The Wi-Fi network the device connects to.
* `Timezone`: The POSIX timezone string for your location — use the [converter](https://phpsecu.re/tz/)'s `TZ_INFO` value. Only affects the log file name's time; recorded logs use UTC.
* `T. Interval`: The interval at which the device transmits telemetry.

##### Inputs

Whether the digital/analog input channels are logged.

##### CAN

* `Enabled`: Whether the CAN bus is logged.
* `Bit rate`: The bus baud rate.
* `Filter`: The expected message ID (11/29 bits)
* `Mask`: The filtering rule for each bit of the filter.
    * `0`: The corresponding filter bit must match to pass.
    * `1`: The corresponding filter bit is ignored (don't care).

See the *Acceptance Filter* section of the [ESP32-S3 API Reference](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32s3/api-reference/peripherals/twai.html#acceptance-filter) for details. All CAN messages are accepted by default.

##### GPS

* `Enabled`: Whether the GPS location is logged.
* `Device`: The device type. Only `UBLOX` is currently supported.

##### [Danger Zone](https://www.youtube.com/watch?v=siwpn14IE7E)

* `Refresh`: Loads the device's configuration. Changed configurations require a device restart to apply; refreshing before restarting reloads the previous values.
* `Restart`: Restart the device.
* `Reset`: Reset the device.

##### Data Downloader

![](images/downloader.png)

* `Load List`: Lists all recorded log files except the current boot session.
* `Delete All`: Deletes all recorded log files except the current boot session.

After loading the file list, click the download button to download a specific file.

## Development

<details>

<summary>Details</summary>

<h3>Firmware</h3>

Install [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/index.html), then run:

```
git clone https://github.com/CreatOurCar/monolith.git
cd monolith/device/firmware
make build
make run   # build & flash
```

<h3>Control Hub</h3>

```
git clone https://github.com/CreatOurCar/monolith.git
cd monolith/web
npm install
npm run vite
npm run build
```

</details>
