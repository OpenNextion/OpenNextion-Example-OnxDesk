#include "settings.h"

#include <stdio.h>
#include <string.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_check.h"
#include "esp_random.h"

#define SETTINGS_NAMESPACE "onxdesk"
#define SETTINGS_KEY "settings"
#define SETUP_SSID_KEY "setup_ssid"

static void settings_defaults(app_settings_t *settings) {
    memset(settings, 0, sizeof(*settings));
    settings->home_page = HOME_CLOCK;
    settings->brightness_percent = 75;
    settings->encoder_step = ENCODER_SENSITIVITY_MEDIUM;
}

esp_err_t settings_init(void) {
    esp_err_t error = nvs_flash_init();
    if (error == ESP_ERR_NVS_NO_FREE_PAGES || error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        error = nvs_flash_init();
    }
    return error;
}

esp_err_t settings_load(app_settings_t *settings) {
    if (settings == NULL) return ESP_ERR_INVALID_ARG;
    settings_defaults(settings);
    nvs_handle_t handle;
    esp_err_t error = nvs_open(SETTINGS_NAMESPACE, NVS_READONLY, &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (error != ESP_OK) return error;
    size_t size = sizeof(*settings);
    error = nvs_get_blob(handle, SETTINGS_KEY, settings, &size);
    nvs_close(handle);
    /* A factory reset leaves the namespace but removes this blob. That is a
     * normal first-boot state, not a startup error. */
    if (error == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (error == ESP_OK && size == sizeof(*settings)) {
        if (settings->encoder_step > ENCODER_SENSITIVITY_HIGH) settings->encoder_step = ENCODER_SENSITIVITY_MEDIUM;
        return ESP_OK;
    }
    settings_defaults(settings);
    return error == ESP_OK ? ESP_OK : error;
}

esp_err_t settings_save(const app_settings_t *settings) {
    if (settings == NULL) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &handle), "settings", "open NVS");
    esp_err_t error = nvs_set_blob(handle, SETTINGS_KEY, settings, sizeof(*settings));
    if (error == ESP_OK) error = nvs_commit(handle);
    nvs_close(handle);
    return error;
}

esp_err_t settings_factory_reset(void) {
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &handle), "settings", "open NVS");
    esp_err_t error = nvs_erase_all(handle);
    if (error == ESP_OK) error = nvs_commit(handle);
    nvs_close(handle);
    return error;
}

esp_err_t settings_get_setup_ssid(char *ssid, size_t ssid_size) {
    if (ssid == NULL || ssid_size < sizeof("OnxDesk-ABCDE")) return ESP_ERR_INVALID_SIZE;
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &handle), "settings", "open NVS");
    size_t saved_size = ssid_size;
    esp_err_t error = nvs_get_str(handle, SETUP_SSID_KEY, ssid, &saved_size);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
        char suffix[6] = {0};
        for (size_t index = 0; index < sizeof(suffix) - 1; index++) suffix[index] = alphabet[esp_random() % (sizeof(alphabet) - 1)];
        snprintf(ssid, ssid_size, "OnxDesk-%s", suffix);
        error = nvs_set_str(handle, SETUP_SSID_KEY, ssid);
        if (error == ESP_OK) error = nvs_commit(handle);
    }
    nvs_close(handle);
    return error;
}

bool settings_has_market_key(const app_settings_t *settings) {
    return settings != NULL && settings->finnhub_api_key[0] != '\0';
}
