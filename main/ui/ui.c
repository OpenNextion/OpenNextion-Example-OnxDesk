#include "ui.h"

#include <stdio.h>
#include <time.h>
#include "esp_check.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"
#include "network.h"

#define DISPLAY_WIDTH 240
#define DISPLAY_HEIGHT 240
#define DISPLAY_BUFFER_PIXELS (DISPLAY_WIDTH * DISPLAY_HEIGHT)
#define DISPLAY_TRANSFER_PIXELS (DISPLAY_WIDTH * 20)

#define COLOR_BG       0x0D0D1A
#define COLOR_SURFACE  0x1A1A2E
#define COLOR_PRIMARY  0xFFFFFF
#define COLOR_SECONDARY 0x8A8A9E
#define COLOR_MUTED    0x5A5A6E
#define COLOR_TEAL     0x5DCAA5
#define COLOR_GREEN    0x4ADE80
#define COLOR_RED      0xF87171
#define COLOR_ORANGE   0xF7931A

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
    const int active = page <= PAGE_SETTINGS ? (int)page : 4;
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

static void render_clock(lv_obj_t *screen) {
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

    lv_obj_t *wifi = label_new(screen, "WiFi", &lv_font_montserrat_12, COLOR_TEAL);
    lv_obj_align(wifi, LV_ALIGN_TOP_MID, 0, 25);
    lv_obj_t *time_label = label_new(screen, time_text, &lv_font_montserrat_48, COLOR_PRIMARY);
    lv_obj_align(time_label, LV_ALIGN_CENTER, 0, -10);
    lv_obj_t *date_label = label_new(screen, date_text, &lv_font_montserrat_14, COLOR_SECONDARY);
    lv_obj_align(date_label, LV_ALIGN_CENTER, 0, 32);
    add_channel_nav(screen, PAGE_CLOCK);
}

static void render_weather_forecast(lv_obj_t *screen, const app_settings_t *settings) {
    const char *city = settings != NULL && settings->city[0] ? settings->city : "Choose a city";
    lv_obj_t *city_label = label_new(screen, city, &lv_font_montserrat_16, COLOR_SECONDARY);
    lv_obj_align(city_label, LV_ALIGN_TOP_MID, 0, 25);
    lv_obj_t *heading = label_new(screen, "3-DAY FORECAST", &lv_font_montserrat_12, COLOR_TEAL);
    lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, 52);
    static const char *days[] = { "TODAY", "TOMORROW", "DAY 3" };
    static const char *temperatures[] = { "31° / 25°", "30° / 24°", "29° / 23°" };
    for (int i = 0; i < 3; i++) {
        const int y = 78 + i * 34;
        panel_new(screen, 24, y, 192, 28, i == 0 ? COLOR_TEAL : COLOR_SURFACE, i == 0 ? LV_OPA_20 : LV_OPA_40);
        lv_obj_t *day = label_new(screen, days[i], &lv_font_montserrat_12, i == 0 ? COLOR_PRIMARY : COLOR_SECONDARY);
        lv_obj_set_pos(day, 34, y + 8);
        lv_obj_t *condition = label_new(screen, "SUNNY", &lv_font_montserrat_12, COLOR_TEAL);
        lv_obj_align(condition, LV_ALIGN_TOP_MID, 0, y + 8);
        lv_obj_t *temperature = label_new(screen, settings != NULL && settings->city[0] ? temperatures[i] : "--° / --°", &lv_font_montserrat_12, COLOR_SECONDARY);
        lv_obj_set_pos(temperature, 150, y + 8);
    }
    lv_obj_t *hint = label_new(screen, "Press for current weather", &lv_font_montserrat_12, COLOR_MUTED);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -28);
    add_channel_nav(screen, PAGE_WEATHER);
}

static void render_weather(lv_obj_t *screen, const navigation_t *navigation, const app_settings_t *settings) {
    if (navigation->weather_forecast) {
        render_weather_forecast(screen, settings);
        return;
    }
    const char *city = settings != NULL && settings->city[0] ? settings->city : "Choose a city";
    lv_obj_t *city_label = label_new(screen, city, &lv_font_montserrat_16, COLOR_SECONDARY);
    lv_obj_align(city_label, LV_ALIGN_TOP_MID, 0, 25);
    lv_obj_t *icon = label_new(screen, "SUN", &lv_font_montserrat_16, COLOR_TEAL);
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 55);
    lv_obj_t *temperature = label_new(screen, settings != NULL && settings->city[0] ? "28°" : "--°", &lv_font_montserrat_48, COLOR_PRIMARY);
    lv_obj_align(temperature, LV_ALIGN_CENTER, 0, -8);
    lv_obj_t *condition = label_new(screen, settings != NULL && settings->city[0] ? "Feels 31° · Sunny" : "Set location in local setup", &lv_font_montserrat_14, COLOR_SECONDARY);
    lv_obj_align(condition, LV_ALIGN_CENTER, 0, 30);
    lv_obj_t *details = label_new(screen, settings != NULL && settings->city[0] ? "Humidity 72% · Wind 8 km/h\nH 31° · L 25°" : "Open Settings for city setup", &lv_font_montserrat_12, COLOR_SECONDARY);
    lv_obj_set_style_text_align(details, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(details, LV_ALIGN_CENTER, 0, 58);
    lv_obj_t *hint = label_new(screen, "Press for 3-day forecast", &lv_font_montserrat_12, COLOR_MUTED);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -28);
    add_channel_nav(screen, PAGE_WEATHER);
}

static void render_crypto(lv_obj_t *screen) {
    add_header(screen, "BTC/USDT");
    lv_obj_t *coin = label_new(screen, "BTC", &lv_font_montserrat_16, COLOR_ORANGE);
    lv_obj_align(coin, LV_ALIGN_TOP_LEFT, 30, 54);
    lv_obj_t *price = label_new(screen, "$67,432", &lv_font_montserrat_32, COLOR_PRIMARY);
    lv_obj_align(price, LV_ALIGN_CENTER, 0, -28);
    lv_obj_t *change = label_new(screen, "▲ +2.34%   +$1,542", &lv_font_montserrat_16, COLOR_GREEN);
    lv_obj_align(change, LV_ALIGN_CENTER, 0, 2);
    lv_obj_t *chart = panel_new(screen, 48, 130, 144, 2, COLOR_GREEN, LV_OPA_COVER);
    lv_obj_set_style_transform_rotation(chart, 335, 0);
    lv_obj_t *range = label_new(screen, "24h", &lv_font_montserrat_12, COLOR_MUTED);
    lv_obj_align(range, LV_ALIGN_CENTER, 0, 52);
    lv_obj_t *selector = label_new(screen, "BTC     ETH     SOL", &lv_font_montserrat_14, COLOR_SECONDARY);
    lv_obj_align(selector, LV_ALIGN_CENTER, 0, 78);
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
    add_header(screen, "Dow Jones");
    lv_obj_t *symbol = label_new(screen, "^DJI", &lv_font_montserrat_12, COLOR_MUTED);
    lv_obj_align(symbol, LV_ALIGN_TOP_MID, 0, 42);
    lv_obj_t *price = label_new(screen, "39,847", &lv_font_montserrat_32, COLOR_PRIMARY);
    lv_obj_align(price, LV_ALIGN_CENTER, 0, -28);
    lv_obj_t *change = label_new(screen, "▼ -0.52%   -208", &lv_font_montserrat_16, COLOR_RED);
    lv_obj_align(change, LV_ALIGN_CENTER, 0, 2);
    lv_obj_t *updated = label_new(screen, "Awaiting Finnhub refresh", &lv_font_montserrat_12, COLOR_MUTED);
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

static void render_settings(lv_obj_t *screen, const app_settings_t *settings) {
    add_header(screen, "SETTINGS");
    const char *city = settings != NULL && settings->city[0] ? settings->city : "Not configured";
    const char *market = settings != NULL && settings_has_market_key(settings) ? "Configured" : "Not configured";
    const char *labels[] = { "WiFi", "City", "Finnhub Key", "About" };
    const char *details[] = { "Local setup page", city, market, "OnxDesk · ONX2424G013" };
    for (int i = 0; i < 4; i++) {
        panel_new(screen, 20, 50 + i * 36, 200, 30, i == 0 ? COLOR_TEAL : COLOR_SURFACE, i == 0 ? LV_OPA_20 : LV_OPA_40);
        lv_obj_t *label = label_new(screen, labels[i], &lv_font_montserrat_14, i == 0 ? COLOR_PRIMARY : COLOR_SECONDARY);
        lv_obj_set_pos(label, 30, 55 + i * 36);
        lv_obj_t *detail = label_new(screen, details[i], &lv_font_montserrat_12, COLOR_MUTED);
        lv_obj_set_pos(detail, 104, 57 + i * 36);
    }
    lv_obj_t *hint = label_new(screen, "Press: display test", &lv_font_montserrat_12, COLOR_RED);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -28);
    add_channel_nav(screen, PAGE_SETTINGS);
}

static void render_provisioning(lv_obj_t *screen) {
    add_header(screen, "WI-FI SETUP");
    const bool failed = network_connection_failed();
    lv_obj_t *title = label_new(screen, failed ? "Connection failed" : network_is_connecting() ? "Connecting to Wi-Fi" : "Connect your phone", &lv_font_montserrat_20, failed ? COLOR_RED : COLOR_PRIMARY);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -44);
    lv_obj_t *network = label_new(screen, "OnxDesk-Setup", &lv_font_montserrat_20, COLOR_TEAL);
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

static void loading_row(lv_obj_t *screen, int y, const char *label, const char *status, uint32_t color) {
    lv_obj_t *label_widget = label_new(screen, label, &lv_font_montserrat_14, COLOR_SECONDARY);
    lv_obj_set_pos(label_widget, 32, y);
    lv_obj_t *status_widget = label_new(screen, status, &lv_font_montserrat_12, color);
    lv_obj_set_pos(status_widget, 132, y + 2);
}

static void render_loading(lv_obj_t *screen, const app_settings_t *settings) {
    const bool time_ready = network_time_is_synced();
    const bool sync_finished = network_initial_sync_complete();
    add_header(screen, "STARTING ONXDESK");
    lv_obj_t *title = label_new(screen, "Preparing your desk", &lv_font_montserrat_20, COLOR_PRIMARY);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 52);
    loading_row(screen, 91, "Wi-Fi", "Connected", COLOR_GREEN);
    loading_row(screen, 120, "Time", time_ready ? "Synchronized" : sync_finished ? "Will retry" : "Syncing…", time_ready ? COLOR_GREEN : sync_finished ? COLOR_MUTED : COLOR_TEAL);
    loading_row(screen, 149, "Weather", settings != NULL && settings->city[0] ? "Location ready" : "City not set", settings != NULL && settings->city[0] ? COLOR_GREEN : COLOR_MUTED);
    loading_row(screen, 178, "Markets", settings != NULL && settings_has_market_key(settings) ? "Key ready" : "Key optional", settings != NULL && settings_has_market_key(settings) ? COLOR_GREEN : COLOR_MUTED);
    lv_obj_t *footer = label_new(screen, sync_finished ? "Opening Clock" : "Opening Clock after time setup", &lv_font_montserrat_12, COLOR_MUTED);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, -24);
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
        .buffer_size = DISPLAY_BUFFER_PIXELS, .trans_size = DISPLAY_TRANSFER_PIXELS,
        .hres = DISPLAY_WIDTH, .vres = DISPLAY_HEIGHT, .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = { .swap_xy = true, .mirror_x = false, .mirror_y = false },
        .flags = { .buff_spiram = true, .swap_bytes = true },
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
        case PAGE_CLOCK: render_clock(screen); break;
        case PAGE_WEATHER: render_weather(screen, navigation, settings); break;
        case PAGE_CRYPTO: render_crypto(screen); break;
        case PAGE_MARKETS: render_markets(screen, settings); break;
        case PAGE_NEWS_HOME: render_news_home(screen, navigation); break;
        case PAGE_SETTINGS: render_settings(screen, settings); break;
        case PAGE_NEWS_CATEGORY_PICKER: render_news_picker(screen, navigation); break;
        case PAGE_NEWS_LIST: render_news_list(screen, navigation); break;
        case PAGE_NEWS_QR: render_news_qr(screen); break;
        case PAGE_DISPLAY_TEST: render_display_test(screen); break;
        case PAGE_PROVISIONING: render_provisioning(screen); break;
        case PAGE_LOADING: render_loading(screen, settings); break;
        default: break;
    }
    lvgl_port_unlock();
}
