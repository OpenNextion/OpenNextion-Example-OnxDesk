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

The News home page shows the leading item for World, Business and Technology. Enter the category picker, then a category list. Selecting an item shows a QR code for the original article.

## Hardware pin map

| Function | GPIO |
| --- | --- |
| LCD SCLK / MOSI / CS / DC / BL / RST | 5 / 1 / 2 / 3 / 6 / 8 |
| Encoder A / B | 48 / 47 |
| Encoder press | 9 |
| BOOT | 0 |

## Status

The initial hardware bring-up scaffold is in place: GC9A01N SPI initialization, 8 MB PSRAM report, persistent settings, navigation state transitions, and GPIO input events. The next implementation step is LVGL 9 rendering, followed by Wi-Fi provisioning and the data-service clients.

## Data notice

Data sources may delay, change, be unavailable or return incorrect data. This project is for informational use only and is not investment advice. Users must comply with each provider's terms of use.
