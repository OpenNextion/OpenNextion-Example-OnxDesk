#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CITY_NAME_MAX_LEN 64
#define CITY_TIMEZONE_MAX_LEN 48
#define WEATHER_FORECAST_DAYS 3

typedef struct {
    char name[CITY_NAME_MAX_LEN];
    double latitude;
    double longitude;
    char timezone[CITY_TIMEZONE_MAX_LEN];
} city_candidate_t;

typedef struct {
    bool valid;
    float temperature_c;
    float apparent_temperature_c;
    float wind_speed_kmh;
    int humidity_percent;
    int weather_code;
    int utc_offset_seconds;
    float daily_high_c[WEATHER_FORECAST_DAYS];
    float daily_low_c[WEATHER_FORECAST_DAYS];
    int daily_weather_code[WEATHER_FORECAST_DAYS];
} weather_snapshot_t;

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
provider_status_t open_meteo_search_city(const char *query, city_candidate_t *results,
                                         size_t results_capacity, size_t *result_count);
provider_status_t open_meteo_refresh_weather(double latitude, double longitude,
                                              weather_snapshot_t *weather);
provider_status_t gdelt_refresh_category(const char *category);
