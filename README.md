# ESP32 Bluetooth Audio Controller

A polished ESP32-based Bluetooth audio controller with a live LED visualizer, 3-band equalizer, and Android companion app for streamlined control.

This project combines a PlatformIO firmware for the ESP32 with an Android app that can connect to the board over Bluetooth SPP, provide media control, adjust visualizer settings, and tune the audio equalizer in real time.

## Overview

The system is designed for a compact audio experience with:

- Bluetooth Audio Receiver using ESP32-A2DP
- Real-time LED visualizer driven by FastLED
- 3-band equalizer with low/mid/high gain controls
- Persistent preferences storage for saved settings
- Android app for full device control and configuration
- Serial control support for debugging, automation, and custom integration

## Key Features

### Audio and Bluetooth
- ESP32 acts as a Bluetooth receiver and audio sink
- Supports playback control such as play, pause, stop, next, and previous
- Keeps track of connection state and device metadata
- Stores the configured Bluetooth name and last-used volume

### LED Visualizer
- Multi-pattern LED effects for left/right audio strips
- Adjustable brightness, delay, LED count, and pattern selection
- Bass-sensitive detection for dynamic visual response
- Configurations saved into ESP32 flash memory

### Equalizer
- 3-band EQ with gain control for low, mid, and high frequencies
- EQ can be enabled or disabled remotely
- Settings are persisted using Preferences to survive reboots
- Commands are available through Bluetooth Serial and serial UART

### Android App
- Pair and connect to ESP32 over Bluetooth
- Display connection status and media metadata
- Control playback and transport functions
- Adjust visualizer values from the app
- Enable/disable EQ and tune low/mid/high levels with sliders
- Save settings directly to the ESP32

## Project Structure

```text
.
├── src/
│   └── main.cpp                 # ESP32 firmware
├── android app/
│   └── main/
│       ├── java/
│       └── res/
├── lib/
│   └── audio-tools/             # AudioTools library sources
├── platformio.ini               # PlatformIO configuration
├── README.md                    # Project documentation
└── test/
```

## Hardware

This project is intended for an ESP32 DevKit V1 board with:

- I2S audio output
- WS2812B LED strips for left and right channels
- Bluetooth audio sink via ESP32-A2DP
- Serial UART support for debugging and custom control

Typical audio pins used in the firmware:

- BCK: GPIO 26
- WS: GPIO 27
- DATA: GPIO 25
- RGB strip pins: GPIO 32 and GPIO 33

## Requirements

### Firmware
- PlatformIO
- VS Code or another supported IDE
- ESP32 board support for Arduino framework

### Android App
- Android Studio
- Android SDK
- Bluetooth permissions enabled for the target device

## Getting Started

### 1. Clone the repository

```bash
git clone https://github.com/fhiagani372-web/esp32-bt.git
cd esp32-bluetooth-audio-controller
```

### 2. Build and upload the firmware

```bash
pio run
pio run --target upload
```

Make sure the correct serial port is selected and the board is connected to the computer.

### 3. Open the Android app

- Open the Android project inside the folder named `android app`
- Sync Gradle dependencies
- Connect an Android device or emulator
- Pair the ESP32 using Bluetooth
- Select the ESP32 device from the list and connect

## Supported Bluetooth Commands

The firmware accepts commands over Bluetooth Serial and UART. Examples include:

```text
print
save
next
prev
play
pause
stop
brightness#70
delay#10
bassf#7
bassl#4
leds#8
pattern#0
name#MyESP32

eq
eq on
eq off
eq low=1.20
eq mid=0.80
eq high=1.50
saveEQ
```

EQ values are constrained to a safe range and are saved in ESP32 flash memory using Preferences.

## Example EQ Commands

```text
eq low=1.20
eq mid=0.90
eq high=1.40
eq on
saveEQ
```

The app sends equivalent commands automatically when the user adjusts the EQ sliders.

## Persistence

The project stores user configuration in the ESP32 NVS area using the Preferences API. Saved settings include:

- Bluetooth device name
- audio volume
- visualizer configuration
- equalizer state and gains

This allows the controller to retain its setup after reboot.

## Development Notes

This project integrates:

- ESP32-A2DP for Bluetooth audio streaming
- FastLED for LED rendering
- AudioTools for signal processing and equalization
- Android Bluetooth APIs for app-side control

## License

This project is distributed under the MIT license unless otherwise specified.

## Acknowledgements

- ESP32 Arduino ecosystem
- ESP32-A2DP library
- FastLED
- AudioTools
- Android Bluetooth framework

### Special Thanks

- [pschatzmann](https://github.com/pschatzmann) — for the excellent AudioTools and ESP32-A2DP libraries, which provided the foundation for this project.
- [arduino-audio-tools](https://github.com/pschatzmann/arduino-audio-tools/)
- [ESP32-A2DP](https://github.com/pschatzmann/ESP32-A2DP)


## Project Status

The project is actively used as a custom Bluetooth audio controller and visualizer platform. It is suitable for experimentation, personal audio setups, and further extension into advanced DSP or smart home integrations.

---

