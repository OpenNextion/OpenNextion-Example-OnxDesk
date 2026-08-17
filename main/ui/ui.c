#include "ui.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_check.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"
#include "network.h"

#define DISPLAY_WIDTH 240
#define DISPLAY_HEIGHT 240
#define DISPLAY_DRAW_BUFFER_PIXELS (DISPLAY_WIDTH * 20)

#define COLOR_BG       0x0D0D1A
#define COLOR_SURFACE  0x1A1A2E
#define COLOR_PRIMARY  0xFFFFFF
#define COLOR_SECONDARY 0x8A8A9E
#define COLOR_MUTED    0x5A5A6E
#define COLOR_TEAL     0x5DCAA5
#define COLOR_GREEN    0x4ADE80
#define COLOR_RED      0xF87171
#define COLOR_ORANGE   0xF7931A
#define COLOR_SUN      0xF5C453
#define COLOR_CLOUD    0xF7F8FF
#define COLOR_RAIN     0x5F9EFF
#define COLOR_SNOW     0xBDE7FF

static lv_display_t *lvgl_display;

static lv_obj_t *label_new(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color) {
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

static lv_obj_t *panel_new(lv_obj_t *parent, int x, int y, int width, int height, uint32_t color, uint8_t opacity) {
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_remove_style_all(panel);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, width, height);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(panel, opacity, 0);
    return panel;
}

static void add_channel_nav(lv_obj_t *screen, app_page_t page) {
    const int active = page <= PAGE_SETTINGS ? (int)page : page == PAGE_SETTINGS_MENU ? 5 : 4;
    for (int i = 0; i < 6; i++) {
        lv_obj_t *dot = panel_new(screen, 77 + i * 17, 210, 6, 6, i == active ? COLOR_TEAL : 0x3A3A4E, LV_OPA_COVER);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    }
    static const char *names[] = { "CLOCK", "WEATHER", "CRYPTO", "MARKETS", "NEWS", "SETTINGS" };
    lv_obj_t *name = label_new(screen, names[active], &lv_font_montserrat_12, COLOR_TEAL);
    lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, -7);
}

static void add_header(lv_obj_t *screen, const char *title) {
    lv_obj_t *label = label_new(screen, title, &lv_font_montserrat_16, COLOR_TEAL);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 18);
}

static void add_arc_text(lv_obj_t *screen, const char *title, const lv_font_t *font, uint32_t color,
                         int radius, int start_angle, int end_angle) {
    const size_t length = strlen(title);
    if (length == 0) return;
    for (size_t i = 0; i < length; i++) {
        char character[2] = { title[i], '\0' };
        const int angle = length == 1 ? (start_angle + end_angle) / 2 :
                          start_angle + (end_angle - start_angle) * (int)i / (int)(length - 1);
        lv_obj_t *letter = label_new(screen, character, font, color);
        lv_obj_update_layout(letter);
        lv_obj_set_style_transform_pivot_x(letter, lv_obj_get_width(letter) / 2, 0);
        lv_obj_set_style_transform_pivot_y(letter, lv_obj_get_height(letter) / 2, 0);
        lv_obj_set_style_transform_rotation(letter, ((angle + 90) % 360) * 10, 0);
        const int x = DISPLAY_WIDTH / 2 + ((lv_trigo_cos(angle) * radius) >> LV_TRIGO_SHIFT);
        const int y = DISPLAY_HEIGHT / 2 + ((lv_trigo_sin(angle) * radius) >> LV_TRIGO_SHIFT);
        lv_obj_set_pos(letter, x - lv_obj_get_width(letter) / 2, y - lv_obj_get_height(letter) / 2);
    }
}

static void add_wifi_signal(lv_obj_t *screen, int x) {
    const unsigned int level = network_wifi_signal_level();
    static const int heights[] = { 4, 7, 10, 13 };
    for (int i = 0; i < 4; i++) {
        const uint32_t color = level == 0 ? COLOR_MUTED : (unsigned int)i < level ? COLOR_TEAL : COLOR_MUTED;
        panel_new(screen, x + i * 6, 38 - heights[i], 4, heights[i], color, LV_OPA_COVER);
    }
}

static void add_celsius_unit(lv_obj_t *screen, int x, int y, int size, uint32_t color) {
    const int dot_size = size < 9 ? 2 : size / 4;
    lv_obj_t *dot = panel_new(screen, x + size / 5, y, dot_size, dot_size, color, LV_OPA_COVER);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);

    lv_obj_t *letter_c = lv_arc_create(screen);
    lv_obj_set_size(letter_c, size, size);
    lv_obj_set_pos(letter_c, x, y + dot_size + 1);
    lv_arc_set_bg_angles(letter_c, 0, 0);
    lv_arc_set_angles(letter_c, 42, 318);
    lv_obj_remove_style(letter_c, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_width(letter_c, size < 10 ? 1 : 2, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(letter_c, lv_color_hex(color), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(letter_c, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_clear_flag(letter_c, LV_OBJ_FLAG_CLICKABLE);
}

typedef enum {
    WEATHER_ICON_SUN,
    WEATHER_ICON_PARTLY_CLOUDY,
    WEATHER_ICON_CLOUDY,
    WEATHER_ICON_FOG,
    WEATHER_ICON_DRIZZLE,
    WEATHER_ICON_RAIN,
    WEATHER_ICON_SNOW,
    WEATHER_ICON_STORM,
    WEATHER_ICON_HAIL,
} weather_icon_t;

static weather_icon_t weather_icon_for_code(int code) {
    switch (code) {
        case 0: return WEATHER_ICON_SUN;
        case 1:
        case 2: return WEATHER_ICON_PARTLY_CLOUDY;
        case 3: return WEATHER_ICON_CLOUDY;
        case 45:
        case 48: return WEATHER_ICON_FOG;
        case 51:
        case 53:
        case 55:
        case 56:
        case 57: return WEATHER_ICON_DRIZZLE;
        case 61:
        case 63:
        case 65:
        case 66:
        case 67:
        case 80:
        case 81:
        case 82: return WEATHER_ICON_RAIN;
        case 71:
        case 73:
        case 75:
        case 77:
        case 85:
        case 86: return WEATHER_ICON_SNOW;
        case 96:
        case 99: return WEATHER_ICON_HAIL;
        case 95: return WEATHER_ICON_STORM;
        default: return WEATHER_ICON_CLOUDY;
    }
}

static void weather_circle(lv_obj_t *screen, int x, int y, int size, uint32_t color) {
    lv_obj_t *circle = panel_new(screen, x, y, size, size, color, LV_OPA_COVER);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
}

static void add_sun(lv_obj_t *screen, int x, int y, int width, int height) {
    const int size = width < height ? width : height;
    const int offset_x = x + (width - size) / 2;
    const int core = size * 12 / 25;
    const int ray = size < 28 ? 2 : 3;
    const int ray_length = size * 3 / 25;
    const int center = size / 2;
    weather_circle(screen, offset_x + (size - core) / 2, y + (size - core) / 2, core, COLOR_SUN);
    panel_new(screen, offset_x + center - ray / 2, y, ray, ray_length, COLOR_SUN, LV_OPA_COVER);
    panel_new(screen, offset_x + center - ray / 2, y + size - ray_length, ray, ray_length, COLOR_SUN, LV_OPA_COVER);
    panel_new(screen, offset_x, y + center - ray / 2, ray_length, ray, COLOR_SUN, LV_OPA_COVER);
    panel_new(screen, offset_x + size - ray_length, y + center - ray / 2, ray_length, ray, COLOR_SUN, LV_OPA_COVER);
    weather_circle(screen, offset_x + ray_length, y + ray_length, ray, COLOR_SUN);
    weather_circle(screen, offset_x + size - ray_length - ray, y + ray_length, ray, COLOR_SUN);
    weather_circle(screen, offset_x + ray_length, y + size - ray_length - ray, ray, COLOR_SUN);
    weather_circle(screen, offset_x + size - ray_length - ray, y + size - ray_length - ray, ray, COLOR_SUN);
}

static void add_cloud(lv_obj_t *screen, int x, int y, int width, int height) {
    const int small = height * 9 / 25;
    const int large = height * 13 / 25;
    weather_circle(screen, x + width * 8 / 100, y + height * 38 / 100, small, COLOR_CLOUD);
    weather_circle(screen, x + width * 29 / 100, y + height * 7 / 100, large, COLOR_CLOUD);
    weather_circle(screen, x + width * 58 / 100, y + height * 34 / 100, small, COLOR_CLOUD);
    lv_obj_t *base = panel_new(screen, x + width * 8 / 100, y + height * 52 / 100, width * 80 / 100, height * 30 / 100, COLOR_CLOUD, LV_OPA_COVER);
    lv_obj_set_style_radius(base, height / 4, 0);
}

static void add_rain_drops(lv_obj_t *screen, int x, int y, int width, int height, int count, uint32_t color) {
    const int drop_width = height < 28 ? 3 : 4;
    const int drop_height = height < 28 ? 5 : 7;
    const int spacing = count == 2 ? width / 3 : width / 4;
    for (int i = 0; i < count; i++) {
        lv_obj_t *drop = panel_new(screen, x + spacing * (i + 1) - drop_width / 2, y + ((i & 1) ? 2 : 0), drop_width, drop_height, color, LV_OPA_COVER);
        lv_obj_set_style_radius(drop, LV_RADIUS_CIRCLE, 0);
    }
}

static void add_weather_icon(lv_obj_t *screen, int x, int y, int width, int height, int weather_code, bool available) {
    if (!available) {
        lv_obj_t *waiting = label_new(screen, "--", &lv_font_montserrat_16, COLOR_MUTED);
        lv_obj_set_pos(waiting, x + width / 3, y + height / 3);
        return;
    }

    switch (weather_icon_for_code(weather_code)) {
        case WEATHER_ICON_SUN:
            add_sun(screen, x, y, width, height);
            break;
        case WEATHER_ICON_PARTLY_CLOUDY:
            add_sun(screen, x, y, width * 3 / 5, height * 3 / 4);
            add_cloud(screen, x + width * 3 / 10, y + height / 3, width * 7 / 10, height * 2 / 3);
            break;
        case WEATHER_ICON_CLOUDY:
            add_cloud(screen, x, y + height / 8, width, height * 7 / 8);
            break;
        case WEATHER_ICON_FOG:
            add_cloud(screen, x, y, width, height * 3 / 4);
            panel_new(screen, x + width / 8, y + height * 3 / 4, width * 3 / 4, 2, COLOR_SECONDARY, LV_OPA_COVER);
            panel_new(screen, x + width / 5, y + height * 7 / 8, width * 3 / 5, 2, COLOR_SECONDARY, LV_OPA_COVER);
            break;
        case WEATHER_ICON_DRIZZLE:
            add_cloud(screen, x, y, width, height * 3 / 4);
            add_rain_drops(screen, x, y + height * 3 / 4, width, height, 2, COLOR_RAIN);
            break;
        case WEATHER_ICON_RAIN:
            add_cloud(screen, x, y, width, height * 3 / 4);
            add_rain_drops(screen, x, y + height * 3 / 4, width, height, 3, COLOR_RAIN);
            break;
        case WEATHER_ICON_SNOW:
            add_cloud(screen, x, y, width, height * 3 / 4);
            add_rain_drops(screen, x, y + height * 3 / 4, width, height, 3, COLOR_SNOW);
            break;
        case WEATHER_ICON_STORM: {
            add_cloud(screen, x, y, width, height * 3 / 4);
            lv_obj_t *bolt = panel_new(screen, x + width * 11 / 25, y + height * 13 / 25, height / 7, height * 8 / 25, COLOR_SUN, LV_OPA_COVER);
            lv_obj_set_style_transform_pivot_x(bolt, height / 14, 0);
            lv_obj_set_style_transform_pivot_y(bolt, height * 4 / 25, 0);
            lv_obj_set_style_transform_rotation(bolt, 250, 0);
            break;
        }
        case WEATHER_ICON_HAIL:
            add_cloud(screen, x, y, width, height * 3 / 4);
            add_rain_drops(screen, x, y + height * 3 / 4, width, height, 2, COLOR_RAIN);
            weather_circle(screen, x + width * 17 / 25, y + height * 4 / 5, height < 28 ? 3 : 5, COLOR_SNOW);
            break;
    }
}

static void clock_city_name(const app_settings_t *settings, char *name, size_t name_size) {
    const char *source = settings != NULL && settings->city[0] != '\0' ? settings->city : "Set city";
    size_t length = strcspn(source, ",");
    if (length >= name_size) length = name_size - 1;
    memcpy(name, source, length);
    name[length] = '\0';
}

static void render_clock(lv_obj_t *screen, const app_settings_t *settings) {
    const time_t now = time(NULL);
    struct tm local_time = {0};
    localtime_r(&now, &local_time);
    char time_text[8] = "--:--";
    char date_text[28] = "Waiting for time sync";
    if (now > 1700000000) {
        strftime(time_text, sizeof(time_text), "%H:%M", &local_time);
        strftime(date_text, sizeof(date_text), "%a, %b %d", &local_time);
    }

    lv_obj_t *ring = lv_arc_create(screen);
    lv_obj_set_size(ring, 216, 216);
    lv_obj_center(ring);
    lv_arc_set_range(ring, 0, 100);
    lv_arc_set_value(ring, now > 1700000000 ? (local_time.tm_hour * 60 + local_time.tm_min) * 100 / 1440 : 0);
    lv_obj_remove_style(ring, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_width(ring, 2, LV_PART_MAIN);
    lv_obj_set_style_arc_color(ring, lv_color_hex(0x1A2A2A), LV_PART_MAIN);
    lv_obj_set_style_arc_width(ring, 2, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(ring, lv_color_hex(COLOR_TEAL), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(ring, LV_OPA_60, LV_PART_INDICATOR);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE);

    char city[sizeof(((app_settings_t *)0)->city)] = {0};
    clock_city_name(settings, city, sizeof(city));
    lv_obj_t *city_label = label_new(screen, city, &lv_font_montserrat_14, COLOR_SECONDARY);
    lv_obj_set_width(city_label, 180);
    lv_label_set_long_mode(city_label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(city_label, LV_TEXT_ALIGN_CENTER, 0);
    add_wifi_signal(screen, 109);
    lv_obj_t *time_label = label_new(screen, time_text, &lv_font_montserrat_48, COLOR_PRIMARY);
    lv_obj_align(time_label, LV_ALIGN_CENTER, 0, -10);
    lv_obj_t *date_label = label_new(screen, date_text, &lv_font_montserrat_14, COLOR_SECONDARY);
    lv_obj_align(date_label, LV_ALIGN_CENTER, 0, 32);
    lv_obj_align(city_label, LV_ALIGN_CENTER, 0, 54);
    add_channel_nav(screen, PAGE_CLOCK);
}

static void render_weather_forecast(lv_obj_t *screen) {
    weather_snapshot_t weather = {0};
    const bool available = network_get_weather(&weather);
    lv_obj_t *heading = label_new(screen, "3-DAY FORECAST", &lv_font_montserrat_12, COLOR_TEAL);
    lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, 28);
    static const char *days[] = { "TODAY", "TOMORROW", "DAY 3" };
    for (int i = 0; i < 3; i++) {
        const int y = 78 + i * 34;
        panel_new(screen, 24, y, 192, 28, i == 0 ? COLOR_TEAL : COLOR_SURFACE, i == 0 ? LV_OPA_20 : LV_OPA_40);
        lv_obj_t *day = label_new(screen, days[i], &lv_font_montserrat_12, i == 0 ? COLOR_PRIMARY : COLOR_SECONDARY);
        lv_obj_set_pos(day, 34, y + 8);
        add_weather_icon(screen, 100, y + 5, 30, 18, available ? weather.daily_weather_code[i] : 0, available);
        char temperatures[20] = "-- / --";
        if (available) snprintf(temperatures, sizeof(temperatures), "%.0f / %.0f", weather.daily_high_c[i], weather.daily_low_c[i]);
        lv_obj_t *temperature = label_new(screen, temperatures, &lv_font_montserrat_12, COLOR_SECONDARY);
        lv_obj_set_pos(temperature, 150, y + 8);
        lv_obj_update_layout(temperature);
        add_celsius_unit(screen, 153 + lv_obj_get_width(temperature), y + 9, 7, COLOR_SECONDARY);
    }
    add_channel_nav(screen, PAGE_WEATHER);
}

static void render_weather(lv_obj_t *screen, const navigation_t *navigation, const app_settings_t *settings) {
    if (navigation->weather_forecast) {
        render_weather_forecast(screen);
        return;
    }
    weather_snapshot_t weather = {0};
    const bool available = network_get_weather(&weather);
    add_weather_icon(screen, 88, 28, 64, 42, available ? weather.weather_code : 0, available);
    char temperature_text[12] = "--";
    char condition_text[48] = "Set location in local setup";
    if (available) {
        snprintf(temperature_text, sizeof(temperature_text), "%.0f", weather.temperature_c);
        snprintf(condition_text, sizeof(condition_text), "Feels %.0f", weather.apparent_temperature_c);
    } else if (settings != NULL && settings->city[0]) {
        strlcpy(condition_text, network_weather_is_refreshing() ? "Refreshing weather..." : "Weather unavailable; retrying", sizeof(condition_text));
    }
    lv_obj_t *temperature = label_new(screen, temperature_text, &lv_font_montserrat_48, COLOR_PRIMARY);
    lv_obj_align(temperature, LV_ALIGN_CENTER, 0, -8);
    lv_obj_update_layout(temperature);
    add_celsius_unit(screen, lv_obj_get_x(temperature) + lv_obj_get_width(temperature) + 3, lv_obj_get_y(temperature) + 7, 15, COLOR_PRIMARY);
    lv_obj_t *condition = label_new(screen, condition_text, &lv_font_montserrat_14, COLOR_SECONDARY);
    lv_obj_align(condition, LV_ALIGN_CENTER, 0, 36);
    if (available) {
        lv_obj_update_layout(condition);
        add_celsius_unit(screen, lv_obj_get_x(condition) + lv_obj_get_width(condition) + 2, lv_obj_get_y(condition) + 3, 8, COLOR_SECONDARY);
    }
    if (available) {
        lv_obj_t *humidity_label = label_new(screen, "HUM", &lv_font_montserrat_12, COLOR_MUTED);
        lv_obj_align(humidity_label, LV_ALIGN_TOP_MID, -82, 98);
        char humidity_text[8];
        snprintf(humidity_text, sizeof(humidity_text), "%d%%", weather.humidity_percent);
        lv_obj_t *humidity_value = label_new(screen, humidity_text, &lv_font_montserrat_16, COLOR_TEAL);
        lv_obj_align(humidity_value, LV_ALIGN_TOP_MID, -82, 111);
        lv_obj_t *wind_label = label_new(screen, "WIND", &lv_font_montserrat_12, COLOR_MUTED);
        lv_obj_align(wind_label, LV_ALIGN_TOP_MID, 82, 98);
        char wind_text[12];
        snprintf(wind_text, sizeof(wind_text), "%.0f km/h", weather.wind_speed_kmh);
        lv_obj_t *wind_value = label_new(screen, wind_text, &lv_font_montserrat_12, COLOR_TEAL);
        lv_obj_align(wind_value, LV_ALIGN_TOP_MID, 82, 113);
        char range_text[32];
        snprintf(range_text, sizeof(range_text), "HIGH %.0f    LOW %.0f", weather.daily_high_c[0], weather.daily_low_c[0]);
        lv_obj_t *range = label_new(screen, range_text, &lv_font_montserrat_14, COLOR_SECONDARY);
        lv_obj_align(range, LV_ALIGN_CENTER, 0, 65);
        lv_obj_update_layout(range);
        add_celsius_unit(screen, lv_obj_get_x(range) + lv_obj_get_width(range) + 2, lv_obj_get_y(range) + 3, 8, COLOR_SECONDARY);
    }
    add_channel_nav(screen, PAGE_WEATHER);
}

static void render_crypto(lv_obj_t *screen, const navigation_t *navigation) {
    static const char *symbols[] = { "BTC", "ETH", "SOL" };
    const unsigned int index = navigation == NULL ? 0 : navigation->crypto_index % 3;
    crypto_quote_t quote = {0};
    const bool available = network_get_crypto_quote(index, &quote);
    char pair[12];
    snprintf(pair, sizeof(pair), "%s/USDT", symbols[index]);
    lv_obj_t *pair_label = label_new(screen, pair, &lv_font_montserrat_16, COLOR_ORANGE);
    lv_obj_align(pair_label, LV_ALIGN_TOP_MID, 0, 18);
    char price_text[20] = "--";
    if (available) {
        if (quote.last_price >= 1000) snprintf(price_text, sizeof(price_text), "$%.0f", quote.last_price);
        else snprintf(price_text, sizeof(price_text), "$%.2f", quote.last_price);
    }
    lv_obj_t *price = label_new(screen, price_text, &lv_font_montserrat_32, COLOR_PRIMARY);
    lv_obj_align(price, LV_ALIGN_CENTER, 0, -28);
    char change_text[20] = "Waiting for Binance";
    uint32_t change_color = COLOR_MUTED;
    if (available) {
        snprintf(change_text, sizeof(change_text), "24H %+.2f%%", quote.change_percent);
        change_color = quote.change_percent >= 0 ? COLOR_GREEN : COLOR_RED;
    }
    lv_obj_t *change = label_new(screen, change_text, &lv_font_montserrat_16, change_color);
    lv_obj_align(change, LV_ALIGN_CENTER, 0, 2);
    lv_obj_t *range = label_new(screen, available ? "Binance spot - 24H" : network_crypto_is_refreshing() ? "Refreshing Binance..." : "Binance unavailable", &lv_font_montserrat_12, COLOR_MUTED);
    lv_obj_align(range, LV_ALIGN_CENTER, 0, 52);
    for (int i = 0; i < 3; i++) {
        const uint32_t selector_color = (unsigned int)i == index ? (navigation != NULL && navigation->crypto_selecting ? COLOR_PRIMARY : COLOR_TEAL) : COLOR_SECONDARY;
        lv_obj_t *selector = label_new(screen, symbols[i], &lv_font_montserrat_14, selector_color);
        lv_obj_align(selector, LV_ALIGN_CENTER, (i - 1) * 58, 78);
    }
    add_channel_nav(screen, PAGE_CRYPTO);
}

static void render_markets(lv_obj_t *screen, const app_settings_t *settings) {
    if (settings == NULL || !settings_has_market_key(settings)) {
        add_header(screen, "MARKETS");
        lv_obj_t *title = label_new(screen, "Market data setup", &lv_font_montserrat_20, COLOR_PRIMARY);
        lv_obj_align(title, LV_ALIGN_CENTER, 0, -22);
        lv_obj_t *hint = label_new(screen, "Add your Finnhub API key\nin the local setup page", &lv_font_montserrat_14, COLOR_SECONDARY);
        lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(hint, LV_ALIGN_CENTER, 0, 18);
        add_channel_nav(screen, PAGE_MARKETS);
        return;
    }
    market_quote_t quote = {0};
    const bool available = network_get_market_quote(0, &quote);
    add_header(screen, "DOW JONES");
    lv_obj_t *symbol = label_new(screen, "^DJI", &lv_font_montserrat_12, COLOR_MUTED);
    lv_obj_align(symbol, LV_ALIGN_TOP_MID, 0, 42);
    char price_text[20] = "--";
    if (available) snprintf(price_text, sizeof(price_text), "%.0f", quote.value);
    lv_obj_t *price = label_new(screen, price_text, &lv_font_montserrat_32, COLOR_PRIMARY);
    lv_obj_align(price, LV_ALIGN_CENTER, 0, -28);
    char change_text[20] = "Waiting for Finnhub";
    uint32_t change_color = COLOR_MUTED;
    if (available) {
        snprintf(change_text, sizeof(change_text), "24H %+.2f%%", quote.change_percent);
        change_color = quote.change_percent >= 0 ? COLOR_GREEN : COLOR_RED;
    }
    lv_obj_t *change = label_new(screen, change_text, &lv_font_montserrat_16, change_color);
    lv_obj_align(change, LV_ALIGN_CENTER, 0, 2);
    lv_obj_t *updated = label_new(screen, available ? "Finnhub quote - may be delayed" : network_market_is_refreshing() ? "Refreshing Finnhub..." : "Finnhub unavailable", &lv_font_montserrat_12, COLOR_MUTED);
    lv_obj_align(updated, LV_ALIGN_CENTER, 0, 50);
    lv_obj_t *selector = label_new(screen, "DJI     IXIC     GSPC", &lv_font_montserrat_14, COLOR_SECONDARY);
    lv_obj_align(selector, LV_ALIGN_CENTER, 0, 78);
    add_channel_nav(screen, PAGE_MARKETS);
}

static void news_row(lv_obj_t *screen, int y, const char *category, const char *first, const char *second, bool selected) {
    if (selected) {
        panel_new(screen, 18, y - 5, 204, 43, COLOR_TEAL, LV_OPA_20);
        panel_new(screen, 18, y - 5, 3, 43, COLOR_TEAL, LV_OPA_COVER);
    }
    lv_obj_t *category_label = label_new(screen, category, &lv_font_montserrat_12, selected ? COLOR_TEAL : COLOR_SECONDARY);
    lv_obj_set_pos(category_label, 30, y);
    lv_obj_t *headline = label_new(screen, first, &lv_font_montserrat_12, selected ? COLOR_PRIMARY : COLOR_SECONDARY);
    lv_obj_set_pos(headline, 30, y + 14);
    if (second != NULL) {
        lv_obj_t *continued = label_new(screen, second, &lv_font_montserrat_12, selected ? COLOR_PRIMARY : COLOR_SECONDARY);
        lv_obj_set_pos(continued, 30, y + 27);
    }
}

static void render_news_home(lv_obj_t *screen, const navigation_t *navigation) {
    add_header(screen, "NEWS");
    news_row(screen, 51, "WORLD", "Global headlines will", "appear after sync", navigation->news_category == NEWS_WORLD);
    news_row(screen, 105, "BUSINESS", "Business headlines will", "appear after sync", navigation->news_category == NEWS_BUSINESS);
    news_row(screen, 159, "TECHNOLOGY", "Technology headlines will", "appear after sync", navigation->news_category == NEWS_TECHNOLOGY);
    add_channel_nav(screen, PAGE_NEWS_HOME);
}

static void render_news_picker(lv_obj_t *screen, const navigation_t *navigation) {
    static const char *categories[] = { "WORLD", "BUSINESS", "TECHNOLOGY" };
    add_header(screen, "NEWS CATEGORIES");
    for (int i = 0; i < 3; i++) {
        const bool selected = i == navigation->news_category;
        panel_new(screen, 30, 65 + i * 38, 180, 30, selected ? COLOR_TEAL : COLOR_SURFACE, selected ? LV_OPA_30 : LV_OPA_50);
        lv_obj_t *label = label_new(screen, categories[i], &lv_font_montserrat_16, selected ? COLOR_PRIMARY : COLOR_SECONDARY);
        lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 71 + i * 38);
    }
    lv_obj_t *hint = label_new(screen, "Rotate · Press to open", &lv_font_montserrat_12, COLOR_MUTED);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -28);
    add_channel_nav(screen, PAGE_NEWS_HOME);
}

static void render_news_list(lv_obj_t *screen, const navigation_t *navigation) {
    static const char *categories[] = { "WORLD", "BUSINESS", "TECHNOLOGY" };
    char title[32];
    snprintf(title, sizeof(title), "› %s", categories[navigation->news_category]);
    add_header(screen, title);
    for (int i = 0; i < 4; i++) {
        const unsigned item = (navigation->selected_index + (unsigned)i) % 8;
        char row_title[42];
        snprintf(row_title, sizeof(row_title), "News item %u · waiting for sync", item + 1);
        news_row(screen, 53 + i * 38, i == 0 ? "SELECTED" : "", row_title, i == 0 ? "Press to display article QR" : "Source · time", i == 0);
    }
    add_channel_nav(screen, PAGE_NEWS_HOME);
}

static void render_news_qr(lv_obj_t *screen) {
    add_header(screen, "SCAN TO READ");
    lv_obj_t *qr = panel_new(screen, 54, 48, 132, 132, COLOR_PRIMARY, LV_OPA_COVER);
    for (int row = 0; row < 17; row++) {
        for (int column = 0; column < 17; column++) {
            if (((row * 7 + column * 11 + row * column) % 5) < 2) panel_new(qr, 7 + column * 7, 7 + row * 7, 6, 6, COLOR_BG, LV_OPA_COVER);
        }
    }
    lv_obj_t *headline = label_new(screen, "Article QR will use the\noriginal GDELT source URL", &lv_font_montserrat_12, COLOR_SECONDARY);
    lv_obj_set_style_text_align(headline, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(headline, LV_ALIGN_BOTTOM_MID, 0, -27);
}

static const char *encoder_sensitivity_name(const app_settings_t *settings) {
    if (settings == NULL) return "Medium";
    switch (settings->encoder_step) {
        case ENCODER_SENSITIVITY_LOW: return "Low";
        case ENCODER_SENSITIVITY_HIGH: return "High";
        default: return "Medium";
    }
}

static void render_settings(lv_obj_t *screen, const app_settings_t *settings) {
    add_header(screen, "SETTINGS");
    const char *city = settings != NULL && settings->city[0] ? settings->city : "Not configured";
    const char *market = settings != NULL && settings_has_market_key(settings) ? "Configured" : "Not configured";
    const char *labels[] = { "WiFi", "City", "Sensitivity", "Finnhub Key", "About" };
    const char *details[] = { "Local setup page", city, encoder_sensitivity_name(settings), market, "OnxDesk · ONX2424G013" };
    for (int i = 0; i < 5; i++) {
        panel_new(screen, 20, 50 + i * 29, 200, 24, i == 0 ? COLOR_TEAL : COLOR_SURFACE, i == 0 ? LV_OPA_20 : LV_OPA_40);
        lv_obj_t *label = label_new(screen, labels[i], &lv_font_montserrat_14, i == 0 ? COLOR_PRIMARY : COLOR_SECONDARY);
        lv_obj_set_pos(label, 30, 54 + i * 29);
        lv_obj_t *detail = label_new(screen, details[i], &lv_font_montserrat_12, COLOR_MUTED);
        lv_obj_set_pos(detail, 112, 56 + i * 29);
    }
    add_channel_nav(screen, PAGE_SETTINGS);
}

static void render_settings_menu(lv_obj_t *screen, const navigation_t *navigation, const app_settings_t *settings) {
    static const char *labels[] = { "WiFi setup", "City", "Sensitivity", "Finnhub Key", "About", "Display test" };
    add_header(screen, "SETTINGS MENU");
    for (int i = 0; i < SETTINGS_MENU_COUNT; i++) {
        const bool selected = i == navigation->settings_item;
        const int y = 48 + i * 23;
        panel_new(screen, 20, y, 200, 20, selected ? COLOR_TEAL : COLOR_SURFACE, selected ? LV_OPA_30 : LV_OPA_40);
        lv_obj_t *label = label_new(screen, labels[i], &lv_font_montserrat_14, selected ? COLOR_PRIMARY : COLOR_SECONDARY);
        lv_obj_set_pos(label, 30, y + 3);
        if (i == SETTINGS_SENSITIVITY) {
            lv_obj_t *value = label_new(screen, encoder_sensitivity_name(settings), &lv_font_montserrat_12, selected ? COLOR_TEAL : COLOR_MUTED);
            lv_obj_set_pos(value, 157, y + 4);
        }
    }
    add_channel_nav(screen, PAGE_SETTINGS);
}

static void render_config_url(lv_obj_t *screen, const navigation_t *navigation) {
    char url[40] = "Waiting for Wi-Fi";
    const bool wifi_setup = navigation != NULL && navigation->settings_item == SETTINGS_WIFI;
    const bool available = wifi_setup ? network_local_wifi_url(url, sizeof(url)) : network_local_url(url, sizeof(url));
    add_header(screen, "LOCAL SETUP");
    lv_obj_t *title = label_new(screen, available ? "Open this address" : "Connect Wi-Fi first", &lv_font_montserrat_20, COLOR_PRIMARY);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -46);
    lv_obj_t *address = label_new(screen, url, &lv_font_montserrat_16, COLOR_TEAL);
    lv_obj_set_style_text_align(address, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(address, LV_ALIGN_CENTER, 0, -10);
    lv_obj_t *detail = label_new(screen, available ? wifi_setup ? "Same Wi-Fi network\nChange Wi-Fi connection" : "Same Wi-Fi network\nCity · Finnhub Key" : "Then return to this page", &lv_font_montserrat_14, COLOR_SECONDARY);
    lv_obj_set_style_text_align(detail, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(detail, LV_ALIGN_CENTER, 0, 34);
    lv_obj_t *hint = label_new(screen, "Long press to return", &lv_font_montserrat_12, COLOR_MUTED);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -28);
}

static void render_city_setup(lv_obj_t *screen, const navigation_t *navigation) {
    char url[40] = {0};
    const bool available = network_local_url(url, sizeof(url));
    add_header(screen, "CITY SETUP");
    const bool show_qr = navigation != NULL && navigation->city_setup_show_qr;
    if (!show_qr) {
        lv_obj_t *title = label_new(screen, "Connect your phone", &lv_font_montserrat_20, COLOR_PRIMARY);
        lv_obj_align(title, LV_ALIGN_CENTER, 0, -34);
        lv_obj_t *instruction = label_new(screen, "Connect to the same\nWi-Fi router as OnxDesk", &lv_font_montserrat_16, COLOR_SECONDARY);
        lv_obj_set_style_text_align(instruction, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(instruction, LV_ALIGN_CENTER, 0, 4);
        lv_obj_t *hint = label_new(screen, "Press to show setup QR", &lv_font_montserrat_14, COLOR_TEAL);
        lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -30);
        return;
    }
    if (available) {
        lv_obj_t *qr = lv_qrcode_create(screen);
        lv_qrcode_set_size(qr, 154);
        lv_qrcode_set_dark_color(qr, lv_color_hex(COLOR_BG));
        lv_qrcode_set_light_color(qr, lv_color_hex(COLOR_PRIMARY));
        lv_qrcode_set_quiet_zone(qr, true);
        if (lv_qrcode_update(qr, url, strlen(url)) != LV_RESULT_OK) {
            lv_obj_del(qr);
        } else {
            lv_obj_align(qr, LV_ALIGN_CENTER, 0, 2);
        }
    }
    lv_obj_t *hint = label_new(screen, available ? "Phone: same Wi-Fi router\nCity saves automatically" : "Waiting for Wi-Fi address", &lv_font_montserrat_12, COLOR_SECONDARY);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -18);
}

static void render_provisioning(lv_obj_t *screen) {
    add_header(screen, "WI-FI SETUP");
    const bool failed = network_connection_failed();
    lv_obj_t *title = label_new(screen, failed ? "Connection failed" : network_is_connecting() ? "Connecting to Wi-Fi" : "Connect your phone", &lv_font_montserrat_20, failed ? COLOR_RED : COLOR_PRIMARY);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -44);
    lv_obj_t *network = label_new(screen, network_setup_ssid(), &lv_font_montserrat_20, COLOR_TEAL);
    lv_obj_align(network, LV_ALIGN_CENTER, 0, -13);
    lv_obj_t *hint = label_new(screen, "Join this open network\nthen open", &lv_font_montserrat_14, COLOR_SECONDARY);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 20);
    lv_obj_t *address = label_new(screen, "192.168.4.1", &lv_font_montserrat_20, COLOR_PRIMARY);
    lv_obj_align(address, LV_ALIGN_CENTER, 0, 55);
    lv_obj_t *footer = label_new(screen, failed ? "Check password and try again" : network_is_connecting() ? "Testing Wi-Fi connection…" : "A setup browser may open automatically", &lv_font_montserrat_12, failed ? COLOR_RED : COLOR_MUTED);
    lv_obj_set_style_text_align(footer, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, -22);
}

static void render_wifi_test(lv_obj_t *screen) {
    const bool failed = network_connection_failed();
    const uint32_t color = failed ? COLOR_RED : COLOR_TEAL;
    lv_obj_t *spinner = lv_spinner_create(screen);
    lv_obj_set_size(spinner, 42, 42);
    lv_obj_align(spinner, LV_ALIGN_CENTER, 0, -20);
    lv_spinner_set_anim_params(spinner, 850, 250);
    lv_obj_set_style_arc_width(spinner, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(COLOR_SURFACE), LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner, 4, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(color), LV_PART_INDICATOR);
    lv_obj_t *status = label_new(screen, failed ? "Wi-Fi connection failed" : "Connecting to Wi-Fi…", &lv_font_montserrat_16, color);
    lv_obj_align(status, LV_ALIGN_CENTER, 0, 28);
}

static void loading_row(lv_obj_t *screen, int y, const char *label, const char *status, uint32_t color) {
    lv_obj_t *label_widget = label_new(screen, label, &lv_font_montserrat_14, COLOR_SECONDARY);
    lv_obj_set_pos(label_widget, 32, y);
    lv_obj_t *status_widget = label_new(screen, status, &lv_font_montserrat_12, color);
    lv_obj_set_pos(status_widget, 132, y + 2);
}

static void render_loading(lv_obj_t *screen, const app_settings_t *settings) {
    const bool time_ready = network_time_is_synced();
    const bool sync_finished = network_initial_sync_complete();
    add_arc_text(screen, "STARTING ONXDESK", &lv_font_montserrat_14, COLOR_TEAL, 100, 205, 335);
    loading_row(screen, 96, "Wi-Fi", "Connected", COLOR_GREEN);
    loading_row(screen, 124, "Time", time_ready ? "Synchronized" : sync_finished ? "Will retry" : "Syncing…", time_ready ? COLOR_GREEN : sync_finished ? COLOR_MUTED : COLOR_TEAL);
    weather_snapshot_t weather = {0};
    const bool weather_ready = network_get_weather(&weather);
    loading_row(screen, 152, "Weather", weather_ready ? "Loaded" : settings != NULL && settings->city[0] ? "Loading…" : "City not set", weather_ready ? COLOR_GREEN : settings != NULL && settings->city[0] ? COLOR_TEAL : COLOR_MUTED);
    loading_row(screen, 180, "Markets", settings != NULL && settings_has_market_key(settings) ? "Key ready" : "Key optional", settings != NULL && settings_has_market_key(settings) ? COLOR_GREEN : COLOR_MUTED);
}

static void add_color_swatch(lv_obj_t *parent, lv_color_t color, int x, int y) {
    lv_obj_t *swatch = panel_new(parent, x, y, 48, 48, 0, LV_OPA_COVER);
    lv_obj_set_style_radius(swatch, 8, 0);
    lv_obj_set_style_bg_color(swatch, color, 0);
}

static void render_display_test(lv_obj_t *screen) {
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    const lv_color_t colors[] = { lv_color_hex(0xFF0000), lv_color_hex(0x00FF00), lv_color_hex(0x0000FF), lv_color_hex(0xFFFFFF) };
    for (int i = 0; i < 4; i++) add_color_swatch(screen, colors[i], 20 + (i % 2) * 152, 20 + (i / 2) * 152);
    lv_obj_t *label = label_new(screen, "R       G\n\nB       W\n\nDISPLAY TEST", &lv_font_montserrat_14, COLOR_PRIMARY);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
}

esp_err_t app_ui_init(const board_display_t *display) {
    if (display == NULL || display->panel == NULL || display->io == NULL) return ESP_ERR_INVALID_ARG;
    const lvgl_port_cfg_t port_config = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port_config), "ui", "initialize LVGL port");
    const lvgl_port_display_cfg_t display_config = {
        .io_handle = display->io, .panel_handle = display->panel,
        /*
         * The SPI LCD driver cannot directly transmit our PSRAM canvas unless
         * it allocates a large internal-RAM bounce buffer for every flush.
         * Under Wi-Fi and TLS pressure that allocation can fail, leaving LVGL
         * waiting for a completion callback that will never arrive.  A 20-line
         * DMA-capable draw buffer sends directly and is ample for this 240px UI.
         */
        .buffer_size = DISPLAY_DRAW_BUFFER_PIXELS,
        .hres = DISPLAY_WIDTH, .vres = DISPLAY_HEIGHT, .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = { .swap_xy = true, .mirror_x = false, .mirror_y = false },
        .flags = { .buff_dma = true, .swap_bytes = true },
    };
    lvgl_display = lvgl_port_add_disp(&display_config);
    return lvgl_display == NULL ? ESP_ERR_NO_MEM : ESP_OK;
}

void app_ui_render(const navigation_t *navigation, const app_settings_t *settings) {
    if (navigation == NULL || !lvgl_port_lock(0)) return;
    lv_obj_t *screen = lv_screen_active();
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    switch (navigation->page) {
        case PAGE_CLOCK: render_clock(screen, settings); break;
        case PAGE_WEATHER: render_weather(screen, navigation, settings); break;
        case PAGE_CRYPTO: render_crypto(screen, navigation); break;
        case PAGE_MARKETS: render_markets(screen, settings); break;
        case PAGE_NEWS_HOME: render_news_home(screen, navigation); break;
        case PAGE_SETTINGS: render_settings(screen, settings); break;
        case PAGE_NEWS_CATEGORY_PICKER: render_news_picker(screen, navigation); break;
        case PAGE_NEWS_LIST: render_news_list(screen, navigation); break;
        case PAGE_NEWS_QR: render_news_qr(screen); break;
        case PAGE_DISPLAY_TEST: render_display_test(screen); break;
        case PAGE_PROVISIONING: render_provisioning(screen); break;
        case PAGE_LOADING: render_loading(screen, settings); break;
        case PAGE_SETTINGS_MENU: render_settings_menu(screen, navigation, settings); break;
        case PAGE_CONFIG_URL: render_config_url(screen, navigation); break;
        case PAGE_CITY_SETUP: render_city_setup(screen, navigation); break;
        case PAGE_WIFI_TEST: render_wifi_test(screen); break;
        default: break;
    }
    lvgl_port_unlock();
}
