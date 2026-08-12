#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    const char *symbol;
    const char *name;
    float value;
    float change_percent;
    int64_t updated_at;
} market_quote_t;

typedef struct {
    const char *category;
    const char *title;
    const char *source_domain;
    const char *url;
    int64_t published_at;
} news_item_t;

typedef enum {
    PROVIDER_NOT_CONFIGURED,
    PROVIDER_READY,
    PROVIDER_NETWORK_ERROR,
    PROVIDER_AUTH_ERROR,
    PROVIDER_RESPONSE_ERROR,
} provider_status_t;

/* Network clients are intentionally isolated from UI. Implement after Wi-Fi provisioning bring-up. */
provider_status_t finnhub_validate_key(const char *api_key);
provider_status_t open_meteo_search_city(const char *query);
provider_status_t gdelt_refresh_category(const char *category);
