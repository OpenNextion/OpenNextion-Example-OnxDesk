# ONX Desk for ONX2424G013

An ESP-IDF and LVGL 9 desktop information display for the OpenNextion ONX2424G013 (ESP32-S3R8, 240x240 GC9A01N round LCD, 16 MB flash and 8 MB OPI PSRAM).

## First-release scope

| Channel | Data source | User configuration |
| --- | --- | --- |
| Clock | SNTP | Wi-Fi only |
| Weather | Open-Meteo | Select a city in the local setup page; no API key |
| Crypto | Binance public spot market data | None; BTC, ETH, SOL are fixed initially |
| Markets | Finnhub | User supplies a personal API key; Dow Jones, Nasdaq-100, S&P 500 ETF proxies |
| Focus | Device-local Pomodoro and countdown timer | None; 25 minutes by default, adjustable from 1 to 120 minutes |

The firmware does not scrape finance websites. Market API keys stay in the device's NVS and must never be committed or printed to logs.

## Controls

- Rotate: choose a visible channel, menu entry, or Focus duration while the timer is stopped.
- Encoder short press: enter or confirm.
- Encoder long press: return one level.
- BOOT (GPIO0) long press for three seconds: restore factory settings, clearing Wi-Fi, city, time zone, Finnhub API key, preferences and cached data.

On the Weather channel, a short press switches between current conditions and the three-day forecast.

On Focus, the default 25-minute duration is a Pomodoro. Rotate while stopped for a 1–120 minute countdown, then press to start or pause. When it reaches zero, OnxDesk opens the Focus page and flashes red and white for six seconds; press once to stop the alert, then again to restart the selected duration.

Settings → **Home pages** can hide Weather, Crypto, Markets, and Focus from the top-level rotation. Clock and Settings remain available so the device always has a home screen and a way to re-enable pages. Page order stays fixed in this release.

## Wi-Fi setup

On a new device (or after a BOOT factory reset), OnxDesk starts an open `OnxDesk-ABCDE` Wi-Fi network and shows its exact name on the display. The final five characters are generated per device and persist across normal restarts; a factory reset generates a new name. Join that network with a phone, then open [http://192.168.4.1](http://192.168.4.1) if the captive-portal browser does not appear automatically. Choose a nearby 2.4 GHz network or enter its SSID, enter its password, and submit the form. The first-run page is deliberately Wi-Fi-only. If no city is stored after the connection succeeds, OnxDesk shows a large QR code for its LAN `/settings` page; connect the phone to the same router before scanning it. Saving a city starts the weather request, then opens the Clock screen.

After provisioning, an independent settings centre remains available at the device's LAN address while it is powered on. OnxDesk displays its `http://…/settings` address when you choose **City** or **Finnhub Key** in Settings. Connect the phone to the same Wi-Fi, open the shown URL, then update the city or Finnhub API key. City search now only runs after OnxDesk has network access. The API key is saved only in device NVS and is never shown again.

## Hardware pin map

| Function | GPIO |
| --- | --- |
| LCD SCLK / MOSI / CS / DC / BL / RST | 5 / 1 / 2 / 3 / 6 / 8 |
| Encoder A / B | 48 / 47 |
| Encoder press | 9 |
| BOOT | 0 |

## Status

The hardware bring-up, provisioning, SNTP time setup, city search and Open-Meteo weather client are in place. Other channel data clients remain staged separately.

## Data notice

Data sources may delay, change, be unavailable or return incorrect data. This project is for informational use only and is not investment advice. Users must comply with each provider's terms of use.
