#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "providers.h"
#include "settings.h"

/* Starts the SoftAP captive-portal and connects as a station when credentials exist. */
esp_err_t network_init(app_settings_t *settings);
bool network_is_connected(void);
bool network_is_connecting(void);
bool network_connection_failed(void);
bool network_initial_sync_complete(void);
bool network_time_is_synced(void);
unsigned int network_wifi_signal_level(void);
bool network_get_weather(weather_snapshot_t *weather);
bool network_weather_is_refreshing(void);
void network_request_weather_refresh(void);
bool network_local_url(char *buffer, size_t buffer_size);
bool network_local_wifi_url(char *buffer, size_t buffer_size);
const char *network_setup_ssid(void);
esp_err_t network_factory_reset(void);
