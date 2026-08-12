# ONX Desk for ONX2424G013

An ESP-IDF and LVGL 9 desktop information display for the OpenNextion ONX2424G013 (ESP32-S3R8, 240x240 GC9A01N round LCD, 16 MB flash and 8 MB OPI PSRAM).

## First-release scope

| Channel | Data source | User configuration |
| --- | --- | --- |
| Clock | SNTP | Wi-Fi only |
| Weather | Open-Meteo | Select a city in the local setup page; no API key |
| Crypto | Binance public spot market data | None; BTC, ETH, SOL are fixed initially |
| Markets | Finnhub | User supplies a personal API key; Dow Jones, Nasdaq Composite, S&P 500 |
| News | GDELT DOC 2.0 | None; World, Business, Technology |

The firmware does not scrape finance or news websites. Market API keys stay in the device's NVS and must never be committed or printed to logs.

## Controls

- Rotate: choose a channel, menu entry or news item.
- Encoder short press: enter or confirm.
- Encoder long press: return one level.
- BOOT (GPIO0) long press for three seconds: restore factory settings, clearing Wi-Fi, city, time zone, Finnhub API key, preferences and cached data.

On the Weather channel, a short press switches between current conditions and the three-day forecast.

## Wi-Fi setup

On a new device (or after a BOOT factory reset), OnxDesk starts the open `OnxDesk-Setup` Wi-Fi network and shows the setup guide on the display. Join that network with a phone, then open [http://192.168.4.1](http://192.168.4.1) if the captive-portal browser does not appear automatically. Choose a nearby 2.4 GHz network or enter its SSID, enter its password, and submit the form. The device keeps the setup network available until its next restart and switches to Clock after it receives a station IP address.

The News home page shows the leading item for World, Business and Technology. Enter the category picker, then a category list. Selecting an item shows a QR code for the original article.

## Hardware pin map

| Function | GPIO |
| --- | --- |
| LCD SCLK / MOSI / CS / DC / BL / RST | 5 / 1 / 2 / 3 / 6 / 8 |
| Encoder A / B | 48 / 47 |
| Encoder press | 9 |
| BOOT | 0 |

## Status

The hardware bring-up and the first UI foundation are in place: GC9A01N SPI initialization, 8 MB PSRAM report, persistent settings, input navigation, and the dark circular LVGL 9 layouts for all six channels. The UI currently uses clearly labelled local placeholder data until Wi-Fi provisioning and the data-service clients are added. The next implementation steps are captive-portal setup, SNTP, and the provider clients.

## Data notice

Data sources may delay, change, be unavailable or return incorrect data. This project is for informational use only and is not investment advice. Users must comply with each provider's terms of use.
