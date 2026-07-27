# monolith

![](.github/assets/wide.jpg)

DIY data logging platform for Student Formula and Baja: ESP32-S3 firmware records sensors straight to an SD card, and recorded logs are analyzed on the external upstream site (https://v2.monolith.luftaquila.io/).

## Development Environment

- **Required ESP-IDF version: v6.0.1** — verify with `idf.py --version` before building. Any other version may break the firmware.

## Features

* 💾 Firmware logs straight to SD — no WiFi, no server, no live telemetry link
   * GPS (NMEA GPRMC) sets the wall clock so recorded logs carry correct absolute time

* 📀 Up to 100 Hz data rate with various signals
   * 1x CAN 2.0(A/B)
   * 1x External GPS
   * 1x Internal 6-axis Accelerometer & Gyroscope
   * 4x Digital input channels
   * 8x Analog input channels

* 🍺 Fully Open-source & Open-hardware under the Beerware license

## Documentation

[Full documentation](https://github.com/CreatOurCar/monolith/tree/main/docs) for DIY and usage details.

## Others

The project name was inspired by Arthur C. Clark's novel `2001: A Space Odyssey`.

## Sponsors

### Individuals

<!-- sponsors --><a href="https://github.com/"><img src="https:&#x2F;&#x2F;raw.githubusercontent.com&#x2F;JamesIves&#x2F;github-sponsors-readme-action&#x2F;dev&#x2F;.github&#x2F;assets&#x2F;placeholder.png" width="60px" alt="User avatar: Private Sponsor" /></a><!-- sponsors -->

## LICENSE

For non-commercial use only:

```
"THE BEERWARE LICENSE" (Revision 42):
LUFT-AQUILA wrote this project. As long as you retain this notice,
you can do whatever you want with this stuff. If we meet someday,
and you think this stuff is worth it, you can buy me a beer in return.
```

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/svg?repos=CreatOurCar/monolith&type=Date&theme=dark" />
  <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/svg?repos=CreatOurCar/monolith&type=Date" />
  <img alt="Star History Chart" src="https://api.star-history.com/svg?repos=star-history/star-history&type=Date" />
</picture>
