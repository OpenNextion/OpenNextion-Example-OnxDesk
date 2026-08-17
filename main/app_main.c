#include "board.h"
#include "input.h"
#include "navigation.h"
#include "network.h"
#include "settings.h"
#include "ui.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"

static const char *TAG = "onxdesk";

static int apply_encoder_sensitivity(int raw_steps, uint8_t sensitivity) {
    static int low_sensitivity_accumulator;
    if (sensitivity == ENCODER_SENSITIVITY_LOW) {
        low_sensitivity_accumulator += raw_steps;
        if (low_sensitivity_accumulator > -2 && low_sensitivity_accumulator < 2) return 0;
        const int adjusted = low_sensitivity_accumulator > 0 ? 1 : -1;
        low_sensitivity_accumulator = 0;
        return adjusted;
    }
    low_sensitivity_accumulator = 0;
    return raw_steps * (sensitivity == ENCODER_SENSITIVITY_HIGH ? 2 : 1);
}

static void advance_encoder_sensitivity(app_settings_t *settings) {
    settings->encoder_step = (uint8_t)((settings->encoder_step + 1) % 3);
    ESP_ERROR_CHECK(settings_save(settings));
    ESP_LOGI(TAG, "encoder sensitivity changed to %u", settings->encoder_step);
}

static bool market_configuration_needed(const app_settings_t *settings, const navigation_t *navigation) {
    if (!settings_has_market_key(settings)) return true;
    market_quote_t quote = {0};
    const unsigned int index = navigation == NULL ? 0 : navigation->market_index % 3;
    return !network_market_is_refreshing() && !network_get_market_quote(index, &quote);
}

static void focus_toggle(navigation_t *navigation) {
    const int64_t now = esp_timer_get_time();
    if (navigation->focus_running) {
        const int64_t remaining = navigation->focus_deadline_us - now;
        navigation->focus_remaining_seconds = remaining > 0 ? (uint32_t)((remaining + 999999) / 1000000) : 0;
        navigation->focus_running = false;
        return;
    }
    if (navigation->focus_remaining_seconds == 0) navigation->focus_remaining_seconds = navigation->focus_duration_seconds;
    navigation->focus_deadline_us = now + (int64_t)navigation->focus_remaining_seconds * 1000000;
    navigation->focus_running = true;
}

static bool focus_update(navigation_t *navigation) {
    if (!navigation->focus_running) return false;
    const int64_t remaining = navigation->focus_deadline_us - esp_timer_get_time();
    const uint32_t seconds = remaining > 0 ? (uint32_t)((remaining + 999999) / 1000000) : 0;
    if (seconds == navigation->focus_remaining_seconds) return false;
    navigation->focus_remaining_seconds = seconds;
    if (seconds == 0) {
        navigation->focus_running = false;
        navigation->focus_alert_until_us = esp_timer_get_time() + 6LL * 1000 * 1000;
        navigation->focus_alert_flash_on = true;
        navigation->page = PAGE_FOCUS;
    }
    return true;
}

static bool focus_alert_update(navigation_t *navigation) {
    if (navigation->focus_alert_until_us == 0) return false;
    const int64_t now = esp_timer_get_time();
    if (now >= navigation->focus_alert_until_us) {
        navigation->focus_alert_until_us = 0;
        navigation->focus_alert_flash_on = false;
        return true;
    }
    const bool flash_on = ((now / 250000) & 1) == 0;
    if (flash_on == navigation->focus_alert_flash_on) return false;
    navigation->focus_alert_flash_on = flash_on;
    return true;
}

static void focus_dismiss_alert(navigation_t *navigation) {
    navigation->focus_alert_until_us = 0;
    navigation->focus_alert_flash_on = false;
}

static home_page_t selected_optional_home_page(const navigation_t *navigation) {
    static const home_page_t optional_pages[] = { HOME_WEATHER, HOME_CRYPTO, HOME_MARKETS, HOME_FOCUS };
    return optional_pages[navigation->home_pages_item % (sizeof(optional_pages) / sizeof(optional_pages[0]))];
}

void app_main(void) {
    ESP_ERROR_CHECK(settings_init());

    app_settings_t settings;
    ESP_ERROR_CHECK(settings_load(&settings));
    ESP_ERROR_CHECK(network_init(&settings));

    board_display_t display = {0};
    ESP_ERROR_CHECK(board_init(&display));
    ESP_ERROR_CHECK(input_init());
    ESP_ERROR_CHECK(app_ui_init(&display));

    navigation_t navigation;
    navigation_init(&navigation);
    if (network_has_saved_wifi()) {
        navigation.page = PAGE_WIFI_TEST;
    } else if (!network_is_connected()) {
        navigation.page = PAGE_PROVISIONING;
    }
    app_ui_render(&navigation, &settings);
    ESP_LOGI(TAG, "ONX Desk booted. Default page: %s; city configured: %s; market key: %s",
             navigation_page_name(navigation.page),
             settings.city[0] ? "yes" : "no",
             settings_has_market_key(&settings) ? "yes" : "no");

    input_event_t event;
    while (true) {
        if (xQueueReceive(input_event_queue(), &event, pdMS_TO_TICKS(250)) != pdTRUE) {
            const bool city_was_saved = network_take_city_saved();
            if ((navigation.page == PAGE_PROVISIONING || navigation.page == PAGE_WIFI_TEST) && network_is_connected()) {
                navigation.page = settings.city[0] == '\0' ? PAGE_CITY_SETUP : PAGE_LOADING;
                app_ui_render(&navigation, &settings);
            } else if (navigation.page == PAGE_WIFI_TEST && network_connection_failed()) {
                navigation.page = PAGE_PROVISIONING;
                app_ui_render(&navigation, &settings);
            } else if (navigation.page == PAGE_CITY_SETUP && navigation.city_setup_from_settings && city_was_saved) {
                navigation.city_setup_from_settings = false;
                navigation.city_setup_show_qr = false;
                navigation.page = PAGE_SETTINGS;
                app_ui_render(&navigation, &settings);
            } else if (navigation.page == PAGE_CITY_SETUP && !navigation.city_setup_from_settings && settings.city[0] != '\0') {
                weather_snapshot_t weather = {0};
                if (network_get_weather(&weather)) navigation.page = PAGE_CLOCK;
                app_ui_render(&navigation, &settings);
            } else if (navigation.page == PAGE_LOADING && network_initial_sync_complete()) {
                navigation.page = PAGE_CLOCK;
                app_ui_render(&navigation, &settings);
            } else if (navigation.page == PAGE_PROVISIONING || navigation.page == PAGE_WIFI_TEST || navigation.page == PAGE_LOADING) {
                app_ui_render(&navigation, &settings);
            }
            const bool focus_changed = focus_update(&navigation);
            const bool alert_changed = focus_alert_update(&navigation);
            if (navigation.page == PAGE_CRYPTO) network_request_crypto_refresh();
            if (navigation.page == PAGE_MARKETS) network_request_market_refresh();
            if (navigation.page == PAGE_CLOCK || navigation.page == PAGE_CRYPTO || navigation.page == PAGE_MARKETS ||
                focus_changed || alert_changed) app_ui_render(&navigation, &settings);
            continue;
        }
        if (event.type == INPUT_EVENT_ROTATE) {
            navigation_rotate(&navigation, apply_encoder_sensitivity(event.value, settings.encoder_step), &settings);
        } else if (event.type == INPUT_EVENT_KEY_SHORT_PRESS) {
            if (navigation.focus_alert_until_us != 0) {
                focus_dismiss_alert(&navigation);
            } else if (navigation.page == PAGE_SETTINGS_MENU && navigation.settings_item == SETTINGS_SENSITIVITY) {
                advance_encoder_sensitivity(&settings);
            } else if (navigation.page == PAGE_HOME_PAGES) {
                if (settings_toggle_optional_home_page(&settings, selected_optional_home_page(&navigation))) {
                    ESP_ERROR_CHECK(settings_save(&settings));
                }
            } else if (navigation.page == PAGE_FOCUS) {
                focus_toggle(&navigation);
            } else if (navigation.page == PAGE_MARKETS && market_configuration_needed(&settings, &navigation)) {
                navigation.parent_page = PAGE_MARKETS;
                navigation.settings_item = SETTINGS_FINNHUB;
                navigation.page = PAGE_CONFIG_URL;
            } else {
                navigation_short_press(&navigation);
            }
        } else if (event.type == INPUT_EVENT_KEY_LONG_PRESS) {
            navigation_long_press(&navigation);
        } else if (event.type == INPUT_EVENT_BOOT_FACTORY_RESET) {
            ESP_LOGW(TAG, "BOOT held for 3 seconds; clearing all local settings");
            ESP_ERROR_CHECK(settings_factory_reset());
            ESP_ERROR_CHECK(network_factory_reset());
            esp_restart();
        }
        app_ui_render(&navigation, &settings);
        ESP_LOGI(TAG, "current page: %s", navigation_page_name(navigation.page));
    }
}
