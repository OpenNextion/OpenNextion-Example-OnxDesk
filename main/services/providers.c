#include "providers.h"
#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"

#define HTTP_RESPONSE_MAX 6144
#define OPEN_METEO_TIMEOUT_MS 8000
#define GOOGLE_NEWS_TIMEOUT_MS 15000
#define RSS_PENDING_MAX 8192

typedef struct {
    char *buffer;
    size_t capacity;
    size_t length;
    bool overflowed;
} http_response_t;

typedef struct {
    news_item_t *items;
    size_t items_capacity;
    size_t item_count;
    char pending[RSS_PENDING_MAX];
    size_t pending_length;
    bool overflowed;
} rss_response_t;

static esp_err_t http_event_handler(esp_http_client_event_t *event) {
    if (event->event_id != HTTP_EVENT_ON_DATA || event->user_data == NULL) return ESP_OK;
    http_response_t *response = event->user_data;
    if (response->length + event->data_len >= response->capacity) {
        response->overflowed = true;
        return ESP_FAIL;
    }
    memcpy(response->buffer + response->length, event->data, event->data_len);
    response->length += event->data_len;
    response->buffer[response->length] = '\0';
    return ESP_OK;
}

static provider_status_t https_get(const char *url, char *response_buffer, size_t response_size) {
    http_response_t response = { .buffer = response_buffer, .capacity = response_size };
    const esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = OPEN_METEO_TIMEOUT_MS,
        .event_handler = http_event_handler,
        .user_data = &response,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) return PROVIDER_NETWORK_ERROR;
    const esp_err_t error = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (error != ESP_OK || response.overflowed) return PROVIDER_NETWORK_ERROR;
    return status == 200 ? PROVIDER_READY : PROVIDER_RESPONSE_ERROR;
}

static bool url_encode(const char *source, char *destination, size_t destination_size) {
    static const char hex[] = "0123456789ABCDEF";
    size_t written = 0;
    for (const unsigned char *cursor = (const unsigned char *)source; *cursor != '\0'; cursor++) {
        const bool plain = (*cursor >= 'a' && *cursor <= 'z') || (*cursor >= 'A' && *cursor <= 'Z') ||
                           (*cursor >= '0' && *cursor <= '9') || *cursor == '-' || *cursor == '_' || *cursor == '.';
        const size_t required = plain ? 1 : 3;
        if (written + required >= destination_size) return false;
        if (plain) destination[written++] = (char)*cursor;
        else {
            destination[written++] = '%';
            destination[written++] = hex[*cursor >> 4];
            destination[written++] = hex[*cursor & 0x0F];
        }
    }
    destination[written] = '\0';
    return true;
}

static bool json_number(const cJSON *object, const char *name, double *value) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsNumber(item)) return false;
    *value = item->valuedouble;
    return true;
}

static bool json_string_number(const cJSON *object, const char *name, float *value) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsString(item) || item->valuestring == NULL) return false;
    char *end = NULL;
    const float parsed = strtof(item->valuestring, &end);
    if (end == item->valuestring || *end != '\0') return false;
    *value = parsed;
    return true;
}

provider_status_t finnhub_validate_key(const char *api_key) {
    return (api_key != NULL && strlen(api_key) > 8) ? PROVIDER_NOT_CONFIGURED : PROVIDER_NOT_CONFIGURED;
}

provider_status_t open_meteo_search_city(const char *query, city_candidate_t *results,
                                         size_t results_capacity, size_t *result_count) {
    if (result_count != NULL) *result_count = 0;
    if (query == NULL || query[0] == '\0' || results == NULL || results_capacity == 0) return PROVIDER_RESPONSE_ERROR;
    char encoded_query[CITY_NAME_MAX_LEN * 3 + 1];
    char url[320];
    char *response = calloc(1, HTTP_RESPONSE_MAX);
    if (response == NULL) return PROVIDER_NETWORK_ERROR;
    if (!url_encode(query, encoded_query, sizeof(encoded_query)) ||
        snprintf(url, sizeof(url), "https://geocoding-api.open-meteo.com/v1/search?name=%s&count=%u&language=en&format=json",
                 encoded_query, (unsigned)results_capacity) >= (int)sizeof(url)) {
        free(response);
        return PROVIDER_RESPONSE_ERROR;
    }
    const provider_status_t status = https_get(url, response, HTTP_RESPONSE_MAX);
    if (status != PROVIDER_READY) {
        free(response);
        return status;
    }
    cJSON *root = cJSON_Parse(response);
    const cJSON *items = root == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(root, "results");
    if (!cJSON_IsArray(items)) {
        cJSON_Delete(root);
        free(response);
        return PROVIDER_RESPONSE_ERROR;
    }
    size_t count = 0;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, items) {
        if (count == results_capacity) break;
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "name");
        const cJSON *country = cJSON_GetObjectItemCaseSensitive(item, "country");
        const cJSON *timezone = cJSON_GetObjectItemCaseSensitive(item, "timezone");
        double latitude = 0, longitude = 0;
        if (!cJSON_IsString(name) || !cJSON_IsString(timezone) || !json_number(item, "latitude", &latitude) ||
            !json_number(item, "longitude", &longitude)) continue;
        city_candidate_t *candidate = &results[count++];
        if (cJSON_IsString(country) && country->valuestring[0] != '\0') {
            snprintf(candidate->name, sizeof(candidate->name), "%s, %s", name->valuestring, country->valuestring);
        } else {
            strlcpy(candidate->name, name->valuestring, sizeof(candidate->name));
        }
        candidate->latitude = latitude;
        candidate->longitude = longitude;
        strlcpy(candidate->timezone, timezone->valuestring, sizeof(candidate->timezone));
    }
    cJSON_Delete(root);
    free(response);
    if (result_count != NULL) *result_count = count;
    return count > 0 ? PROVIDER_READY : PROVIDER_RESPONSE_ERROR;
}

provider_status_t open_meteo_refresh_weather(double latitude, double longitude, weather_snapshot_t *weather) {
    if (weather == NULL || latitude < -90 || latitude > 90 || longitude < -180 || longitude > 180) return PROVIDER_RESPONSE_ERROR;
    char url[512];
    char *response = calloc(1, HTTP_RESPONSE_MAX);
    if (response == NULL) return PROVIDER_NETWORK_ERROR;
    snprintf(url, sizeof(url), "https://api.open-meteo.com/v1/forecast?latitude=%.6f&longitude=%.6f&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m&daily=weather_code,temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=%d", latitude, longitude, WEATHER_FORECAST_DAYS);
    const provider_status_t status = https_get(url, response, HTTP_RESPONSE_MAX);
    if (status != PROVIDER_READY) {
        free(response);
        return status;
    }
    cJSON *root = cJSON_Parse(response);
    const cJSON *current = root == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(root, "current");
    const cJSON *daily = root == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(root, "daily");
    double temperature = 0, apparent = 0, humidity = 0, weather_code = 0, wind = 0, utc_offset = 0;
    if (!cJSON_IsObject(current) || !cJSON_IsObject(daily) || !json_number(current, "temperature_2m", &temperature) ||
        !json_number(current, "apparent_temperature", &apparent) || !json_number(current, "relative_humidity_2m", &humidity) ||
        !json_number(current, "weather_code", &weather_code) || !json_number(current, "wind_speed_10m", &wind) ||
        !json_number(root, "utc_offset_seconds", &utc_offset)) {
        cJSON_Delete(root);
        free(response);
        return PROVIDER_RESPONSE_ERROR;
    }
    memset(weather, 0, sizeof(*weather));
    weather->temperature_c = (float)temperature;
    weather->apparent_temperature_c = (float)apparent;
    weather->humidity_percent = (int)humidity;
    weather->weather_code = (int)weather_code;
    weather->wind_speed_kmh = (float)wind;
    weather->utc_offset_seconds = (int)utc_offset;
    const cJSON *highs = cJSON_GetObjectItemCaseSensitive(daily, "temperature_2m_max");
    const cJSON *lows = cJSON_GetObjectItemCaseSensitive(daily, "temperature_2m_min");
    const cJSON *codes = cJSON_GetObjectItemCaseSensitive(daily, "weather_code");
    for (int day = 0; day < WEATHER_FORECAST_DAYS; day++) {
        const cJSON *high = cJSON_GetArrayItem(highs, day);
        const cJSON *low = cJSON_GetArrayItem(lows, day);
        const cJSON *code = cJSON_GetArrayItem(codes, day);
        if (cJSON_IsNumber(high)) weather->daily_high_c[day] = (float)high->valuedouble;
        if (cJSON_IsNumber(low)) weather->daily_low_c[day] = (float)low->valuedouble;
        if (cJSON_IsNumber(code)) weather->daily_weather_code[day] = (int)code->valuedouble;
    }
    weather->valid = true;
    cJSON_Delete(root);
    free(response);
    return PROVIDER_READY;
}

provider_status_t binance_refresh_quote(const char *symbol, crypto_quote_t *quote) {
    if (symbol == NULL || symbol[0] == '\0' || quote == NULL) return PROVIDER_RESPONSE_ERROR;
    char url[128];
    if (snprintf(url, sizeof(url), "https://api.binance.com/api/v3/ticker/24hr?symbol=%s", symbol) >= (int)sizeof(url)) {
        return PROVIDER_RESPONSE_ERROR;
    }
    char *response = calloc(1, HTTP_RESPONSE_MAX);
    if (response == NULL) return PROVIDER_NETWORK_ERROR;
    const provider_status_t status = https_get(url, response, HTTP_RESPONSE_MAX);
    if (status != PROVIDER_READY) {
        free(response);
        return status;
    }
    cJSON *root = cJSON_Parse(response);
    float price = 0, change = 0;
    const bool valid = cJSON_IsObject(root) && json_string_number(root, "lastPrice", &price) &&
                       json_string_number(root, "priceChangePercent", &change);
    cJSON_Delete(root);
    free(response);
    if (!valid || price <= 0) return PROVIDER_RESPONSE_ERROR;
    *quote = (crypto_quote_t){ .valid = true, .last_price = price, .change_percent = change };
    return PROVIDER_READY;
}

provider_status_t finnhub_refresh_quote(const char *symbol, const char *api_key, market_quote_t *quote) {
    if (symbol == NULL || symbol[0] == '\0' || api_key == NULL || api_key[0] == '\0' || quote == NULL) return PROVIDER_NOT_CONFIGURED;
    char encoded_symbol[48];
    char encoded_key[sizeof(((app_settings_t *)0)->finnhub_api_key) * 3 + 1];
    char url[512];
    if (!url_encode(symbol, encoded_symbol, sizeof(encoded_symbol)) || !url_encode(api_key, encoded_key, sizeof(encoded_key)) ||
        snprintf(url, sizeof(url), "https://finnhub.io/api/v1/quote?symbol=%s&token=%s", encoded_symbol, encoded_key) >= (int)sizeof(url)) {
        return PROVIDER_RESPONSE_ERROR;
    }
    char *response = calloc(1, HTTP_RESPONSE_MAX);
    if (response == NULL) return PROVIDER_NETWORK_ERROR;
    const provider_status_t status = https_get(url, response, HTTP_RESPONSE_MAX);
    if (status != PROVIDER_READY) {
        free(response);
        return status;
    }
    cJSON *root = cJSON_Parse(response);
    double value = 0, change = 0, updated_at = 0;
    const bool valid = cJSON_IsObject(root) && json_number(root, "c", &value) && json_number(root, "dp", &change) &&
                       json_number(root, "t", &updated_at);
    cJSON_Delete(root);
    free(response);
    if (!valid || value <= 0) return PROVIDER_RESPONSE_ERROR;
    quote->valid = true;
    quote->value = (float)value;
    quote->change_percent = (float)change;
    quote->updated_at = (int64_t)updated_at;
    return PROVIDER_READY;
}

static void xml_copy_text(char *destination, size_t destination_size, const char *source, const char *end) {
    if (destination_size == 0) return;
    size_t written = 0;
    while (source < end && written + 1 < destination_size) {
        if (*source == '&') {
            if ((size_t)(end - source) >= 5 && strncmp(source, "&amp;", 5) == 0) {
                destination[written++] = '&'; source += 5; continue;
            }
            if ((size_t)(end - source) >= 6 && strncmp(source, "&quot;", 6) == 0) {
                destination[written++] = '\"'; source += 6; continue;
            }
            if ((size_t)(end - source) >= 5 && strncmp(source, "&#39;", 5) == 0) {
                destination[written++] = '\''; source += 5; continue;
            }
            if ((size_t)(end - source) >= 4 && strncmp(source, "&lt;", 4) == 0) {
                destination[written++] = '<'; source += 4; continue;
            }
            if ((size_t)(end - source) >= 4 && strncmp(source, "&gt;", 4) == 0) {
                destination[written++] = '>'; source += 4; continue;
            }
        }
        destination[written++] = *source++;
    }
    destination[written] = '\0';
}

static bool rss_element_text(const char *item, const char *opening, const char *closing,
                             char *destination, size_t destination_size) {
    const char *value = strstr(item, opening);
    if (value == NULL) return false;
    value += strlen(opening);
    const char *end = strstr(value, closing);
    if (end == NULL) return false;
    xml_copy_text(destination, destination_size, value, end);
    return destination[0] != '\0';
}

static bool rss_source_text(const char *item, char *destination, size_t destination_size) {
    const char *value = strstr(item, "<source");
    if (value == NULL || (value = strchr(value, '>')) == NULL) return false;
    value++;
    const char *end = strstr(value, "</source>");
    if (end == NULL) return false;
    xml_copy_text(destination, destination_size, value, end);
    return destination[0] != '\0';
}

static void rss_consume_items(rss_response_t *response) {
    while (response->pending_length > 0) {
        char *start = strstr(response->pending, "<item>");
        if (start == NULL) {
            const size_t preserved = response->pending_length < 5 ? response->pending_length : 5;
            memmove(response->pending, response->pending + response->pending_length - preserved, preserved);
            response->pending_length = preserved;
            response->pending[preserved] = '\0';
            return;
        }
        if (start != response->pending) {
            const size_t remaining = response->pending_length - (size_t)(start - response->pending);
            memmove(response->pending, start, remaining);
            response->pending_length = remaining;
            response->pending[remaining] = '\0';
        }
        char *end = strstr(response->pending, "</item>");
        if (end == NULL) return;
        const size_t item_length = (size_t)(end - response->pending) + strlen("</item>");
        const char saved = response->pending[item_length];
        response->pending[item_length] = '\0';
        if (response->item_count < response->items_capacity) {
            news_item_t *item = &response->items[response->item_count];
            memset(item, 0, sizeof(*item));
            if (rss_element_text(response->pending, "<title>", "</title>", item->title, sizeof(item->title)) &&
                rss_element_text(response->pending, "<link>", "</link>", item->url, sizeof(item->url))) {
                rss_source_text(response->pending, item->source_domain, sizeof(item->source_domain));
                item->valid = true;
                response->item_count++;
            }
        }
        response->pending[item_length] = saved;
        const size_t remaining = response->pending_length - item_length;
        memmove(response->pending, response->pending + item_length, remaining);
        response->pending_length = remaining;
        response->pending[remaining] = '\0';
    }
}

static esp_err_t rss_http_event_handler(esp_http_client_event_t *event) {
    if (event->event_id != HTTP_EVENT_ON_DATA || event->user_data == NULL) return ESP_OK;
    rss_response_t *response = event->user_data;
    if (response->pending_length + event->data_len >= sizeof(response->pending)) {
        response->overflowed = true;
        return ESP_FAIL;
    }
    memcpy(response->pending + response->pending_length, event->data, event->data_len);
    response->pending_length += event->data_len;
    response->pending[response->pending_length] = '\0';
    rss_consume_items(response);
    return ESP_OK;
}

provider_status_t google_news_refresh_category(const char *feed_url, news_item_t *items,
                                               size_t items_capacity, size_t *item_count) {
    if (item_count != NULL) *item_count = 0;
    memset(items, 0, sizeof(*items) * items_capacity);
    if (feed_url == NULL || feed_url[0] == '\0' || items == NULL || items_capacity == 0) return PROVIDER_RESPONSE_ERROR;
    rss_response_t response = { .items = items, .items_capacity = items_capacity };
    const esp_http_client_config_t config = {
        .url = feed_url,
        .timeout_ms = GOOGLE_NEWS_TIMEOUT_MS,
        .event_handler = rss_http_event_handler,
        .user_data = &response,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .disable_auto_redirect = false,
        .user_agent = "OnxDesk/1.0 personal news reader",
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) return PROVIDER_NETWORK_ERROR;
    const esp_err_t error = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (error != ESP_OK || response.overflowed) return PROVIDER_NETWORK_ERROR;
    if (item_count != NULL) *item_count = response.item_count;
    return status == 200 && response.item_count > 0 ? PROVIDER_READY : PROVIDER_RESPONSE_ERROR;
}
