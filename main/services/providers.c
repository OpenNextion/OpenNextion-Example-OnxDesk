#include "providers.h"

#include <string.h>

/* Transport and JSON parsing will be enabled after Wi-Fi provisioning is verified on hardware. */
provider_status_t finnhub_validate_key(const char *api_key) {
    return (api_key != NULL && strlen(api_key) > 8) ? PROVIDER_NOT_CONFIGURED : PROVIDER_NOT_CONFIGURED;
}

provider_status_t open_meteo_search_city(const char *query) {
    return (query != NULL && query[0] != '\0') ? PROVIDER_NOT_CONFIGURED : PROVIDER_RESPONSE_ERROR;
}

provider_status_t gdelt_refresh_category(const char *category) {
    return (category != NULL && category[0] != '\0') ? PROVIDER_NOT_CONFIGURED : PROVIDER_RESPONSE_ERROR;
}
