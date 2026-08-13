#pragma once

#include <stdbool.h>
#include "esp_err.h"

/* Starts the SoftAP captive-portal and connects as a station when credentials exist. */
esp_err_t network_init(void);
bool network_is_connected(void);
bool network_is_connecting(void);
bool network_connection_failed(void);
esp_err_t network_factory_reset(void);
