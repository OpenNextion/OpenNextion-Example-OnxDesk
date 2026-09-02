# Build and flash

This guide is for contributors and developers building OnxDesk from source.
The project uses ESP-IDF and targets the ESP32-S3 based OpenNextion
ONX2424G013. The board has 16 MiB Flash and 8 MiB OPI PSRAM; these settings
are already supplied by `sdkconfig.defaults`.

For a ready-to-flash firmware, use the shorter [Release firmware instructions]
(../README.md#firmware-download-and-flashing) instead.

## Prerequisites

Install a supported ESP-IDF release and initialise its tools according to the
[Espressif ESP-IDF installation guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/).
Then open a terminal in the repository and activate the ESP-IDF environment:

```sh
. <ESP-IDF-PATH>/export.sh
```

Replace `<ESP-IDF-PATH>` with the directory containing your ESP-IDF checkout.
Run this command in every new terminal before using `idf.py`.

## Edit and build

Edit source files under `main/`, then build from the repository root:

```sh
idf.py set-target esp32s3
idf.py build
```

`set-target` is needed only for a new build directory or when changing target.
It recreates target-specific build configuration. The resulting application is
`build/onxdesk.bin`; it is not a standalone firmware image.

To remove generated build output and build again:

```sh
idf.py fullclean
idf.py build
```

`fullclean` removes the local `build/` directory only. It does not erase the
device and does not change source files.

## Flash a development build

Connect the board with a data-capable USB cable, identify its serial port, and
run:

```sh
idf.py -p <PORT> flash
```

Examples of a port are `/dev/cu.usbmodem…` on macOS or `COM3` on Windows. The
command writes the bootloader, partition table, and application at the offsets
defined by the project. If automatic download-mode entry fails, hold **BOOT**,
start the command, and release **BOOT** after the connection starts.

## Monitor logs

Use the same port to view boot and runtime logs:

```sh
idf.py -p <PORT> monitor
```

Press `Ctrl+]` to exit the monitor. To build, flash, and then monitor in one
step:

```sh
idf.py -p <PORT> flash monitor
```

Close the serial monitor before another program accesses the same serial port.

## Erase Flash

Factory reset from the device UI is usually enough to remove user settings.
Use a full erase only for recovery, such as when testing a clean installation:

```sh
idf.py -p <PORT> erase-flash
idf.py -p <PORT> flash
```

`erase-flash` removes the firmware, Wi-Fi credentials, saved city, Finnhub API
key, preferences, and all other data stored in Flash. The device cannot boot
again until a firmware is flashed.

## Create and flash one merged BIN

After a successful build, create a raw merged image containing the bootloader,
partition table, application, and configured binary partitions. Create the
release directory in the repository root first:

```sh
mkdir -p release
idf.py merge-bin -o ../release/onxdesk-v0.1.0-onx2424g013.bin -f raw
```

`idf.py merge-bin` invokes `esptool` from within `build/`, so the `../release/`
prefix deliberately points back to the repository's `release/` directory. The
resulting `release/onxdesk-v0.1.0-onx2424g013.bin` is a single image intended
for offset `0x0`. Flash it with `esptool`:

```sh
python -m esptool --chip esp32s3 --port <PORT> --baud 460800 \
  write-flash 0x0 release/onxdesk-v0.1.0-onx2424g013.bin
```

This is the format intended for GitHub Release assets. Do not flash the regular
`build/onxdesk.bin` at offset `0x0`; it is application-only and belongs at the
offset described by the generated flash arguments.
