# ESP32 PC Stats Monitor

Show live Windows PC statistics on an ESP32-035 3.5-inch display over USB
serial. The PC runs a small Python sender; the board renders the received
metrics on its LCD.

## Hardware checked

The connected USB device enumerates as a QinHeng CH340 USB-serial adapter
(VID:PID `1A86:7523`, `/dev/ttyUSB0`). That identifies the serial bridge, not
the ESP32 chip. After connecting to the ROM bootloader for the authorized
firmware upload, esptool identified the MCU as ESP32-D0WD-V3 revision 3.1
(classic ESP32, not ESP32-S3). The provided `ESP32-035` model matches the
3.5-inch 320x480 resistive-touch CYD reference layout used by this project.

This layout uses an ESP32 Dev Module-compatible target, an ILI9341-compatible
TFT_eSPI driver, and the ESP32-035 SPI/backlight pins in `platformio.ini`.
The display is used in landscape mode; touch input is not required.

## Install

1. Install Python 3.10 or later on the Windows PC.
2. From this folder, install the sender dependencies:

   ```powershell
   py -m pip install -r requirements.txt
   ```

3. Install PlatformIO IDE in VS Code (the project uses its Arduino build
   environment). The Espressif ESP-IDF extension is also installed, but this
   specific CYD firmware uses Arduino/TFT_eSPI.
4. Open this folder in VS Code and let PlatformIO download the board framework
   and libraries on the first build.

## Build and flash

Connect the ESP32-035 with a USB-A-to-USB-C cable. The ESP32-035 reference
hardware may not work with a USB-C-to-USB-C cable. In VS Code, use the
PlatformIO **Build** and **Upload** actions. If upload cannot find the board,
set `upload_port` in `platformio.ini` to the COM port shown in Device Manager.

## Run the PC sender

With the board connected, run:

```powershell
py pc_monitor.py
```

The sender auto-selects one CH340 serial port. If detection is ambiguous or
your adapter uses another USB bridge, specify its port:

```powershell
py pc_monitor.py --port COM5
```

The monitor sends one newline-delimited JSON sample per second at 115200 baud.
The board and sender share the same USB serial connection; close PlatformIO's
serial monitor before running the sender.

The display shows CPU and memory utilization, NVIDIA GPU utilization and
temperature when NVIDIA's driver/NVML is available, GPU memory, network
download/upload rates, and the sender's uptime. GPU values show `N/A` when no
supported NVIDIA GPU is available. Press Ctrl+C to stop the sender.

## Hardware reference

The model's 3.5-inch LCD, serial-cable caveat, and SPI pin mapping were checked
against the [ESP32-035 hardware notes](https://github.com/littleCdev/ESP32-035).
