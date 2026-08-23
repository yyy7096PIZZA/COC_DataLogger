# monolith

![](.github/assets/wide.jpg)

DIY data logging platform for Student Formula and Baja: ESP32-S3 firmware records sensors straight to an SD card, and recorded logs are analyzed on the external upstream site (https://v2.monolith.luftaquila.io/).

## Authorship & Attribution / 저작권 및 기여

- **Original author:** The **monolith** project was created by **luftaquila** (오병준, <mail@luftaquila.io>) and is licensed under the Beerware license — see [LICENSE](LICENSE). Upstream: https://github.com/luftaquila/monolith
- **This fork:** This is **CreatOurCar's** fork. The refactor into the current **SD-only firmware** (removing WiFi/MQTT/web/RTC, converting SD to SDSPI, pin remapping, and related docs) was contributed by **Lee Min-hyeong (이민형)** — Dept. of Energy Systems Engineering '21, Chung-Ang University — in **June 2026**. Per-commit authorship is preserved in the git history.
- This is a **derivative work**, not an original solo creation; the Beerware notice and the original attribution above are retained.

**한국어 요약:** 원본 **monolith**의 저작자는 **luftaquila(오병준)**이며 Beerware 라이선스를 따릅니다([LICENSE](LICENSE) 참고). 이 저장소는 **CreatOurCar의 포크**이고, 현재의 **SD 전용 펌웨어**로의 리팩터(WiFi/MQTT/웹/RTC 제거, SDSPI 전환, 핀 재배치, 문서화)는 **이민형(중앙대학교 에너지시스템공학부 21학번)**이 2026년 6월에 기여했습니다. 커밋별 상세 기여는 git 히스토리에 남아 있으며, 본 저장소는 단독 창작물이 아닌 2차적 저작물입니다.

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
