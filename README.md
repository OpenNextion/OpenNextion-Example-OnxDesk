# ONX Desk for ONX2424G013

An ESP-IDF and LVGL 9 desktop information display for the OpenNextion ONX2424G013: ESP32-S3R8, 240×240 GC9A01N round LCD, 16 MB flash, and 8 MB OPI PSRAM.

## First-release scope

| Channel | Data source | User configuration |
| --- | --- | --- |
| Clock | SNTP | Wi-Fi only |
| Weather | Open-Meteo | Select a city in the local setup page; no API key |
| Crypto | Binance public spot market data | None; BTC, ETH, SOL are fixed initially |
| Markets | Finnhub | User supplies a personal API key; Dow Jones, Nasdaq-100, S&P 500 ETF proxies |
| Focus | Device-local Pomodoro and countdown timer | None; 25 minutes by default, adjustable from 1 to 120 minutes |

- The firmware does not scrape finance websites.
- Market API keys stay in the device's NVS and are never committed or printed to logs.

## Controls

### Everyday navigation

- Rotate the encoder to choose a visible channel or menu entry.
- Short-press the encoder to enter or confirm.
- Long-press the encoder to return one level.
- On the Weather channel, short-press to switch between current conditions and the three-day forecast.

### Focus timer

1. Rotate normally to move between channels; the default Focus duration is a 25-minute Pomodoro.
2. Short-press on Focus to enter time adjustment.
3. Rotate in five-minute steps from 5–120 minutes, or in one-minute steps between 1–5 minutes.
4. Short-press to confirm the duration. Long-press to start or pause the timer.
5. While paused, short-press to resume, or long-press to cancel and restore the selected duration.

- A small orange progress ring appears on other top-level pages while a timer runs.
- At zero, OnxDesk opens the Focus page and alternates red and white for six seconds. Either encoder press dismisses the alert without starting another timer.

### Settings and reset

- **Settings → Home pages** can hide Weather, Crypto, Markets, and Focus from top-level rotation. Clock and Settings always remain available, and page order is fixed in this release.
- **Settings → About** shows the project name, exact ESP-IDF firmware version, and a QR code for the [GitHub Issues page](https://github.com/OpenNextion/OpenNextion-Example-OnxDesk/issues).
- Long-press **BOOT** (GPIO0) for three seconds to restore factory settings. This clears Wi-Fi, city, time zone, Finnhub API key, preferences, and cached data.

## Wi-Fi and city setup

### First-time setup

1. On a new device, or after a BOOT factory reset, OnxDesk starts an open `OnxDesk-ABCDE` Wi-Fi network and displays its exact name.
2. Join that network with a phone. If the captive portal does not open automatically, browse to [http://192.168.4.1](http://192.168.4.1).
3. Choose a nearby 2.4 GHz network or enter its SSID, then enter the password and submit the form.
4. The first-run page only configures Wi-Fi. After the connection succeeds, if no city is stored, the device shows a large QR code for its LAN `/settings` page.
5. Connect the phone to the same router before scanning the QR code. Save a city to start the weather request and open the Clock screen.

The final five characters of the setup-network name are generated per device and persist across normal restarts. A factory reset generates a new name.

### Later changes

1. Open **Settings → City** or **Settings → Finnhub Key** on the device.
2. On the same Wi-Fi as the device, open the displayed `http://…/settings` address with a phone.
3. Update the city or Finnhub API key in the local settings centre.

City search only runs after OnxDesk has network access. The API key is saved only in device NVS and is never shown again.

## Hardware reference

The pin map is included for firmware developers and hardware troubleshooting. It is not needed for normal use.

| Function | GPIO |
| --- | --- |
| LCD SCLK / MOSI / CS / DC / BL / RST | 5 / 1 / 2 / 3 / 6 / 8 |
| Encoder A / B | 48 / 47 |
| Encoder press | 9 |
| BOOT | 0 |

## Status

- Hardware bring-up, provisioning, SNTP time setup, city search, and the Open-Meteo weather client are in place.
- Other channel data clients remain staged separately.

## Data disclaimer

- Data is retrieved from third-party internet services.
- It may be delayed, incomplete, unavailable, changed, or incorrect.
- This project is provided for informational purposes only and does not constitute investment, financial, or trading advice.
- You are responsible for independently verifying data and complying with each data provider's terms of use.
