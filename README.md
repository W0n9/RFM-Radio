# RFM Radio [![Downloads](https://img.shields.io/github/downloads/W0n9/RFM-Radio/total)](https://github.com/W0n9/RFM-Radio/releases/latest)

English | [简体中文](README.zh-CN.md)

Real FM hardware radio app for Android. It drives the built-in FM tuner directly through a root native backend — no internet streaming involved.

This is an actively maintained fork of [vladislav805/RFM-Radio](https://github.com/vladislav805/RFM-Radio); APK releases are published from this repository. Compared to upstream, this fork adds MediaTek device support, a redesigned UI, media button handling, and keeps pace with current Android target SDK requirements.

## Requirements

- Android 6.0+ (SDK 23+).
- Root access is required.
- A wired headset — it usually serves as the antenna.

## Supported hardware

The native backend detects the available FM hardware at runtime:

| Backend | Detection | Verified devices |
| --- | --- | --- |
| Qualcomm legacy (V4L2) | `/dev/radio0` exists | Redmi 4A (Snapdragon 425), Mi A1 (Snapdragon 625) |
| Qualcomm HAL (`fm_helium.so`) | HAL library present, no `/dev/radio0` | Poco X3 Pro (Snapdragon 860) |
| MediaTek (`/dev/fm`) | `/dev/fm` exists | Redmi Note 9 5G (MT6853 / MT6631) |

## Features

- Live FM playback using the real tuner (Qualcomm or MediaTek).
- RDS metadata: PS, RT, PTY, PI, AF, and station country.
- Recording to `.wav` and `.mp3` (Qualcomm devices).
- Favorites: multiple named lists with import/export; the main screen shows them as a horizontal chip row.
- Play through speaker (MediaTek; keep the headset connected — on some hardware the speaker amp taps the headphone path).
- Media button and wired headset control while playing.
- Region/band selection (Europe, US, Japan, Japan-wide), channel spacing, stereo, soft mute, antenna, power saving.

## Screenshots

<img src="fastlane/metadata/android/en-US/images/phoneScreenshots/1.png" width="33%" alt="Main screen"/><img src="fastlane/metadata/android/en-US/images/phoneScreenshots/2.png" width="33%" alt="Searching"/><img src="fastlane/metadata/android/en-US/images/phoneScreenshots/3.png" width="33%" alt="Settings screen"/>

## Building

Clone with submodules (the MP3 encoder), or initialize them in an existing checkout:

```bash
git clone --recursive https://github.com/W0n9/RFM-Radio.git
# or: git submodule update --init --recursive
```

Then build — an Android SDK must be available via `ANDROID_HOME` or `local.properties`:

```bash
./gradlew :app:assembleDebug
```

Gradle cross-compiles the native FM backend under `native/` into assets automatically (requires Android NDK 29.0.14033849 and CMake 3.22.1). Native host tests: `make test-native`.

## Inspired by

> Spirit1 and Spirit2 by Mike Reid.
>
> R.I.P. 2016

## Disclaimer

The project is based on hard-to-piece collected documentation around Tavarua, Iris, I2C, V4L2 — and, in this fork, on reverse-engineered MediaTek driver interfaces.

## Contributors

- Vladislav Veluga ([@vladislav805](https://github.com/vladislav805)) — developer of the original project
- Mike Reid ([@mikereidis](https://github.com/mikereidis)) — developer of Spirit 1/2
- Konstantin Manaev ([@SoSlowMan](https://github.com/SoSlowMan)) — translator (English)
- Andus ([@AndusDEV](https://github.com/AndusDEV)) — translator (Polish)
- TsungWing ([@W0n9](https://github.com/W0n9)) — fork maintainer (MediaTek backend, UI, modernization)

## License

[GPL-3.0](LICENSE)
