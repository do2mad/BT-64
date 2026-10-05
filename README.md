<!-- fork-start: this section is maintained in the do2mad fork, the original README follows below -->
# BT-64 – extended feature set (fork by do2mad)

This is a fork of [sideprojectslab/BT-64](https://github.com/sideprojectslab/BT-64) with
additional features. It is maintained by Martin Oswald ([do2mad](https://github.com/do2mad),
[1mhz.de](https://1mhz.de)) with the friendly agreement of Vittorio (SideProjectsLab), who keeps
maintaining the original firmware. Everything the original BT-64 firmware does still works the same
way – the extensions only add to it.

**The original README follows further below.**

## What's added

### BLE keyboard service – use your phone as the C64 keyboard

iOS and Android apps cannot act as a Bluetooth HID keyboard, so the fork adds a small Bluetooth LE
service that a phone app talks to directly. No extra hardware is needed.

- Use the **Blue-64 Keyboard** app: a C64-style keyboard with all PETSCII graphics legends,
  SHIFT / C= / CTRL / RESTORE, text snippets and short BASIC listings typed for you.
  - iPhone: [public TestFlight beta](https://testflight.apple.com/join/jEn4tsP7)
  - Android (8.0+): [APK in the app release](https://github.com/do2mad/blue64-keyboard-ios/releases/latest)
  - Screenshots, documentation and the protocol: [do2mad/blue64-keyboard-ios](https://github.com/do2mad/blue64-keyboard-ios)
- The BT-64 shows up as **Blue-64** in the app.
- Bluetooth keyboards and gamepads keep working at the same time.
- All keys are released when the phone disconnects.
- Nothing in Bluepad32 is changed; the firmware update routine and the bootloader are untouched.

Technical details: service UUID `C64B0001-B1E6-4A64-9C64-6B7E3F1A2D00`, key characteristic
`[flags, key]` (flags = SHIFT / C= / CTRL / RESTORE, key = C64 matrix position PA×8+PB), text
characteristic feeding the existing macro feed, info characteristic with protocol version and busy
flag. Source: `dev/sw/src/main/bt64_ble_kbd.c/.h/.gatt`.

## Install

1. Download `application.bin` from the [latest release of this fork](https://github.com/do2mad/BT-64/releases/latest).
2. Copy it to the root of a FAT-formatted SD card, insert the card into the BT-64 and switch the
   C64 on – the firmware updates itself, exactly like an official update.
3. Open the app, it finds the BT-64 by itself.

**Going back:** use the official `application.bin` from
[sideprojectslab/BT-64 releases](https://github.com/sideprojectslab/BT-64/releases) the same way.

## Branches and versions

- `ble-keyboard` – the extended firmware (this branch, default branch of the fork).
- `main` – unchanged copy of the original firmware.
- Release tags look like `v1.12-ble1`: original firmware version + extension revision.
- Updates of the original firmware (e.g. new Bluepad32 versions) are merged into this fork regularly.

## Feedback, issues

Problems with the extended features: please open an issue **in this fork**
([do2mad/BT-64/issues](https://github.com/do2mad/BT-64/issues)), not in the original project.
For the BT-64 itself, see the SPL [Discord](https://discord.gg/gJsCgebkDw).

## Related

- [Pico64 Keyboard](https://github.com/do2mad/pico64-keyboard) – a minimal DIY take on the same idea
  with a Raspberry Pi Pico 2 W, using the same BLE protocol and app.

## License

Same as the original project: software under the Apache License 2.0, hardware design files under
CC BY-NC-ND 4.0 (see below). The BLE keyboard service is © 2026 Martin Oswald, Apache-2.0.
BT-64 © SideProjectsLab. This fork is not an official SideProjectsLab product – please don't ask
Vittorio for support with the extended features.

---

<!-- fork-end -->

# BT-64

If you would like to get in touch with the developer, please join the SPL [Discord](https://discord.gg/gJsCgebkDw) server.

[FULL README (PDF)](https://raw.githubusercontent.com/sideprojectslab/blue-64/main/doc/README.pdf)

BT-64 is a plug & play bluetooth adapter for the Commodore 64 that plugs onto the keyboard header inside the computer and can control the keyboard lines and emulate keystrokes and joystick inputs. The goal of the project is to support two bluetooth controllers and a bluetooth keyboard simultaneously, in order to be able to controll the C64 completely wirelessly.

BT-64 can be purchased pre-assembled or as a DIY-Kit from these authorized shops:
- [Retro8BitShop](https://retro8bitshop.com/product/spl-blue-64-v1/)
- [Retro-Updates](https://www.retro-updates.com/product/15909946/blue-64-v1-0-plug-play-bluetooth-for-the-c64)

Sales of BT-64 by any shop other than the ones mentioned above may be in violation of the [License](#license) terms and conditions and should be reported to the developer


<figure align="center" id="blue_64_reg">
	<img src="doc/pictures/longboard_solo.png" width="40%">
	<figcaption>BT-64 "Regular" Board</figcaption>
</figure>

<figure align="center" id="blue_64_dev">
	<img src="doc/pictures/BT-64.png" width="40%">
	<figcaption>BT-64 Developer Board</figcaption>
</figure>

## Build Instructions
* Install ESP-IDF:
  * Download ESP-IDF 5.5.4 from the following [link](https://github.com/espressif/esp-idf/releases/tag/v5.5.4) and install it by following the ["get-started"](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32/get-started/index.html) instructions
  * Install dependencies with: `sudo apt install git wget flex bison gperf python3 python3-pip python3-venv cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0`
  * Copy ESP-IDF to the desired location, for instance: `~/Espressif/esp-idf-v5.5.4`
  * As explained in the guide, prior to installing ESP-IDF, set the ESP_IDF_TOOLS environment variable to the location where you want the various Espressif tools to be installed. A good way to make this permanent is to add the follwing line (or similar) to your ~/.bash_profile file, for instance: `export IDF_TOOLS_PATH="~/Espressif/tools"`
  * log-out and log-in again.
  * CD inside ESP-IDF folder and run `install.sh`
  * define the `ESP_IDF_5_5_4` environment variable in your `~/.bash_profile` pointing to the appropriate framework, for instance: `export ESP_IDF_5_5_4="~/Espressif/esp-idf-v5.5.4"`
  * Log-out and log-in again.
* CD to the BT-64 project's root
* setup the projects virtual environment by running `python3 _venv/setup.py`
* CD to `dev/sw/_make`
* activate the venv: (`source ../../../_venv/bin/activate` in Linux or `_venv/Scripts/activate` in Windows)
* run `inv`, invoke will run the complete build and generate the compilation outputs under `dump` inside the same `_make` folder

## License
License information is included on top of all software source files as well as in all schematics. Files that do not contain explicit licensing information are subject to the licensing terms stated in the LICENSE.txt provided in the main project folder:

Unless stated otherwise in individual files, all hardware design Schematics, Bill of Materials, Gerber files and manuals are licensed under Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International. To view a copy of this license, visit http://creativecommons.org/licenses/by-nc-nd/4.0/

Unless otherwise stated in individual files, all software source files are Licensed under the Apache License, Version 2.0. You may obtain a copy of this license at http://www.apache.org/licenses/LICENSE-2.0
