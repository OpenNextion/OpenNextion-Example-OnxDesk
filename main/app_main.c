#include "board.h"
#include "input.h"
#include "navigation.h"
#include "network.h"
#include "settings.h"
#include "ui.h"

#include "esp_log.h"
#include "esp_system.h"

static const char *TAG = "onxdesk";

void app_main(void) {
    ESP_ERROR_CHECK(settings_init());

    app_settings_t settings;
    ESP_ERROR_CHECK(settings_load(&settings));
    ESP_ERROR_CHECK(network_init());

    board_display_t display = {0};
    ESP_ERROR_CHECK(board_init(&display));
    ESP_ERROR_CHECK(input_init());
    ESP_ERROR_CHECK(app_ui_init(&display));

    navigation_t navigation;
    navigation_init(&navigation);
    if (!network_is_connected()) navigation.page = PAGE_PROVISIONING;
    app_ui_render(&navigation, &settings);
    ESP_LOGI(TAG, "ONX Desk booted. Default page: %s; city configured: %s; market key: %s",
             navigation_page_name(navigation.page),
             settings.city[0] ? "yes" : "no",
             settings_has_market_key(&settings) ? "yes" : "no");

    input_event_t event;
    while (true) {
        if (xQueueReceive(input_event_queue(), &event, pdMS_TO_TICKS(1000)) != pdTRUE) {
            if (navigation.page == PAGE_PROVISIONING && network_is_connected()) {
                navigation.page = PAGE_CLOCK;
                app_ui_render(&navigation, &settings);
            }
            if (navigation.page == PAGE_CLOCK) app_ui_render(&navigation, &settings);
            continue;
        }
        if (event.type == INPUT_EVENT_ROTATE) {
            navigation_rotate(&navigation, event.value * settings.encoder_step);
        } else if (event.type == INPUT_EVENT_KEY_SHORT_PRESS) {
            navigation_short_press(&navigation);
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
