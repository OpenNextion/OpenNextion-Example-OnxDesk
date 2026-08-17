#include "network.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "cJSON.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "providers.h"
#include "settings.h"

#define SETUP_PAGE_IP "192.168.4.1"
#define MAX_FORM_BODY 256
#define MAX_CONNECT_RETRIES 5

static const char *TAG = "network";
static volatile bool connected;
static volatile bool connect_requested;
static volatile bool connection_failed;
static volatile bool configure_after_disconnect;
static volatile bool initial_sync_started;
static volatile bool initial_sync_completed;
static volatile bool time_synced;
static volatile bool weather_refreshing;
static volatile bool city_saved;
static volatile bool captive_dns_active;
static volatile bool captive_dns_running;
static unsigned int connection_retries;
static wifi_config_t pending_station_config;
static bool pending_station_config_valid;
static bool wifi_started;
static bool saved_wifi_present;
static bool setup_ap_active;
static char captive_portal_uri[] = "http://" SETUP_PAGE_IP "/";
static char setup_ap_ssid[sizeof(((wifi_ap_config_t *)0)->ssid)];
static char station_ip[16];
static app_settings_t *active_settings;
static weather_snapshot_t latest_weather;
static esp_netif_t *setup_ap_netif;

static esp_err_t enable_setup_ap(void);
static void disable_setup_ap(void);
static void advertise_captive_portal(esp_netif_t *netif);

static void apply_utc_offset(int utc_offset_seconds) {
    const int offset_minutes = utc_offset_seconds / 60;
    const int absolute_minutes = offset_minutes < 0 ? -offset_minutes : offset_minutes;
    char timezone[20];
    /* POSIX TZ signs are opposite to UTC offsets: UTC-8 is UTC+08:00. */
    snprintf(timezone, sizeof(timezone), "UTC%c%d:%02d", offset_minutes >= 0 ? '-' : '+',
             absolute_minutes / 60, absolute_minutes % 60);
    setenv("TZ", timezone, 1);
    tzset();
    ESP_LOGI(TAG, "local UTC offset set to %d seconds", utc_offset_seconds);
}

static void weather_refresh_task(void *argument) {
    (void)argument;
    if (active_settings != NULL && active_settings->city[0] != '\0') {
        weather_snapshot_t refreshed = {0};
        const provider_status_t status = open_meteo_refresh_weather(active_settings->latitude, active_settings->longitude, &refreshed);
        if (status == PROVIDER_READY) {
            latest_weather = refreshed;
            apply_utc_offset(refreshed.utc_offset_seconds);
            ESP_LOGI(TAG, "weather refreshed for %s", active_settings->city);
        } else {
            ESP_LOGW(TAG, "weather refresh failed: %d", status);
        }
    }
    weather_refreshing = false;
    vTaskDelete(NULL);
}

void network_request_weather_refresh(void) {
    if (!connected || active_settings == NULL || active_settings->city[0] == '\0' || weather_refreshing) return;
    weather_refreshing = true;
    if (xTaskCreate(weather_refresh_task, "weather_refresh", 7168, NULL, 5, NULL) != pdPASS) {
        weather_refreshing = false;
        ESP_LOGE(TAG, "failed to start weather refresh task");
    }
}

bool network_take_city_saved(void) {
    const bool saved = city_saved;
    city_saved = false;
    return saved;
}

static void initial_sync_task(void *argument) {
    (void)argument;
    const esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("time.cloudflare.com");
    esp_err_t error = esp_netif_sntp_init(&config);
    if (error == ESP_OK) {
        for (int attempt = 0; attempt < 5; attempt++) {
            if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(2000)) == ESP_OK) {
                time_synced = true;
                break;
            }
        }
    }
    if (time_synced) {
        ESP_LOGI(TAG, "initial time synchronization complete");
    } else {
        ESP_LOGW(TAG, "initial time synchronization timed out; SNTP will keep retrying");
    }
    network_request_weather_refresh();
    while (weather_refreshing) vTaskDelay(pdMS_TO_TICKS(100));
    initial_sync_completed = true;
    vTaskDelete(NULL);
}

static void start_initial_sync(void) {
    if (initial_sync_started) return;
    initial_sync_started = true;
    if (xTaskCreate(initial_sync_task, "initial_sync", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to start initial sync task");
        initial_sync_completed = true;
    }
}

typedef struct __attribute__((packed)) {
    uint16_t id;
    uint16_t flags;
    uint16_t question_count;
    uint16_t answer_count;
    uint16_t authority_count;
    uint16_t additional_count;
} dns_header_t;

typedef struct __attribute__((packed)) {
    uint16_t name_pointer;
    uint16_t type;
    uint16_t class;
    uint32_t ttl;
    uint16_t address_length;
    uint32_t address;
} dns_answer_t;

static const char setup_page[] =
    "<!doctype html><html><head><meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>OnxDesk setup</title><style>body{max-width:34rem;margin:2rem auto;padding:0 1rem;background:#0d0d1a;color:#fff;font:16px system-ui}"
    "h1{color:#5dcaa5}input,select,button{box-sizing:border-box;width:100%;padding:.8rem;margin:.4rem 0;border-radius:.5rem;border:0}"
    "input,select{background:#1a1a2e;color:#fff}button{background:#5dcaa5;color:#0d0d1a;font-weight:bold}small{color:#8a8a9e}</style></head><body>"
    "<h1>OnxDesk Wi-Fi setup</h1><p>Choose your 2.4 GHz Wi-Fi network, then enter its password.</p>"
    "<form method=post action=/configure><label>Nearby Wi-Fi networks</label><select id=ssid name=ssid required><option selected disabled value=''>Scanning nearby networks...</option></select><button type=button id=refresh>Refresh network list</button>"
    "<div id=manual hidden><label>Other Wi-Fi name (SSID)</label><input id=manual_ssid name=manual_ssid maxlength=32 autocomplete=off disabled></div>"
    "<label>Password</label><input name=password type=password maxlength=64 autocomplete=current-password>"
    "<button id=connect type=submit>Connect</button></form><p id=status><small>Select a network, then test the connection.</small></p><small>After Wi-Fi connects, OnxDesk opens its Clock screen. Configure city and optional service keys later from Settings.</small>"
    "<script>let s=document.querySelector('#ssid'),m=document.querySelector('#manual'),i=document.querySelector('#manual_ssid'),f=document.querySelector('form'),b=document.querySelector('#connect'),z=document.querySelector('#status');function manual(){let x=s.value==='__manual__';m.hidden=!x;i.disabled=!x;i.required=x}function add(v,t){let o=document.createElement('option');o.value=v;o.textContent=t;s.append(o)}function scan(){s.textContent='';add('','Scanning nearby networks...');s.options[0].disabled=true;s.value='';fetch('/scan').then(r=>r.text()).then(t=>{s.textContent='';add('','Choose a Wi-Fi network');s.options[0].disabled=true;t.trim().split('\\n').filter(Boolean).forEach(n=>add(n,n));add('__manual__','Other network...')}).catch(()=>{s.textContent='';add('__manual__','Enter Wi-Fi name manually');s.value='__manual__';manual()})}function poll(){fetch('/status').then(r=>r.text()).then(t=>{if(t==='connected'){z.textContent='Connected. OnxDesk is opening its Clock screen.';b.disabled=true}else if(t==='failed'){z.textContent='Connection failed. Check the password and try again.';b.disabled=false}else setTimeout(poll,1000)}).catch(()=>{z.textContent='Could not check connection status.';b.disabled=false})}s.onchange=manual;document.querySelector('#refresh').onclick=scan;f.onsubmit=e=>{e.preventDefault();b.disabled=true;z.textContent='Connecting...';fetch('/configure',{method:'POST',body:new URLSearchParams(new FormData(f))}).then(x=>{if(!x.ok)throw 0;poll()}).catch(()=>{z.textContent='Could not start connection.';b.disabled=false})};scan();</script>"
    "</body></html>";

static const char settings_page[] =
    "<!doctype html><html><head><meta name=viewport content='width=device-width,initial-scale=1'><title>OnxDesk settings</title><style>body{max-width:34rem;margin:2rem auto;padding:0 1rem;background:#0d0d1a;color:#fff;font:16px system-ui}h1{color:#5dcaa5}input,select,button{box-sizing:border-box;width:100%;padding:.8rem;margin:.4rem 0;border-radius:.5rem;border:0}input,select{background:#1a1a2e;color:#fff}button{background:#5dcaa5;color:#0d0d1a;font-weight:bold}small{color:#8a8a9e}</style></head><body>"
    "<h1>OnxDesk settings</h1><h2>City, time and weather</h2><p><small>Search in English (for example: London or New York).</small></p><label>City</label><input id=city_query maxlength=63 placeholder='Search city' autocomplete=off><button type=button id=city_search>Search cities</button><select id=city_results hidden></select><button type=button id=city_save hidden>Use this city</button><p id=city_status><small>City not configured.</small></p>"
    "<hr><h2>Market data</h2><p><small>Finnhub key is optional and stays only on this device.</small></p><label>Finnhub API key</label><input id=market_key type=password maxlength=95 autocomplete=off placeholder='Paste your API key'><button type=button id=market_save>Save Finnhub key</button><p id=market_status><small>Get a key at <a href=https://finnhub.io/register>finnhub.io/register</a>.</small></p>"
    "<script>let q=document.querySelector('#city_query'),r=document.querySelector('#city_results'),cs=document.querySelector('#city_search'),sv=document.querySelector('#city_save'),cz=document.querySelector('#city_status'),mk=document.querySelector('#market_key'),ms=document.querySelector('#market_save'),mz=document.querySelector('#market_status');cs.onclick=()=>{let city=q.value.trim();if(!city){cz.textContent='Enter a city name.';return}cs.disabled=true;cz.textContent='Searching...';fetch('/city-search?query='+encodeURIComponent(city)).then(x=>x.ok?x.json():Promise.reject()).then(items=>{r.textContent='';items.forEach(x=>{let o=document.createElement('option');o.textContent=x.name+' - '+x.timezone;o.value=JSON.stringify(x);r.append(o)});r.hidden=!items.length;sv.hidden=!items.length;cz.textContent=items.length?'Choose the matching city.':'No matching city found.'}).catch(()=>cz.textContent='Search unavailable. Check Wi-Fi.').finally(()=>cs.disabled=false)};sv.onclick=()=>{if(!r.value)return;let x=JSON.parse(r.value);sv.disabled=true;cz.textContent='Saving city and refreshing weather...';fetch('/city-save',{method:'POST',body:new URLSearchParams(x)}).then(y=>y.ok?y.text():Promise.reject()).then(t=>cz.textContent=t).catch(()=>cz.textContent='Could not save city.').finally(()=>sv.disabled=false)};ms.onclick=()=>{ms.disabled=true;mz.textContent='Saving key...';fetch('/market-key',{method:'POST',body:new URLSearchParams({api_key:mk.value})}).then(x=>x.ok?x.text():Promise.reject()).then(t=>{mk.value='';mz.textContent=t}).catch(()=>mz.textContent='Could not save key.').finally(()=>ms.disabled=false)}</script></body></html>";

static void url_decode(char *text) {
    char *read = text;
    char *write = text;
    while (*read) {
        if (*read == '+' ) {
            *write++ = ' ';
            read++;
        } else if (*read == '%' && read[1] && read[2]) {
            unsigned int value = 0;
            if (sscanf(read + 1, "%2x", &value) == 1) {
                *write++ = (char)value;
                read += 3;
            } else {
                *write++ = *read++;
            }
        } else {
            *write++ = *read++;
        }
    }
    *write = '\0';
}

static bool form_value(const char *body, const char *name, char *output, size_t output_size) {
    const size_t name_len = strlen(name);
    for (const char *field = body; field != NULL;) {
        const char *next = strchr(field, '&');
        const size_t field_len = next == NULL ? strlen(field) : (size_t)(next - field);
        if (field_len > name_len && strncmp(field, name, name_len) == 0 && field[name_len] == '=') {
            const char *value = field + name_len + 1;
            const size_t value_len = field_len - name_len - 1;
            const size_t copy_len = value_len < output_size - 1 ? value_len : output_size - 1;
            memcpy(output, value, copy_len);
            output[copy_len] = '\0';
            url_decode(output);
            return true;
        }
        field = next == NULL ? NULL : next + 1;
    }
    return false;
}

static int build_dns_reply(const uint8_t *request, size_t request_len, uint8_t *reply, size_t reply_size) {
    if (request_len < sizeof(dns_header_t) + 5 || reply_size < request_len + sizeof(dns_answer_t)) return -1;
    const dns_header_t *request_header = (const dns_header_t *)request;
    if (ntohs(request_header->question_count) != 1) return -1;
    size_t question_len = sizeof(dns_header_t);
    while (question_len < request_len && request[question_len] != 0) {
        const size_t label_len = request[question_len];
        if (label_len == 0 || question_len + label_len >= request_len) return -1;
        question_len += label_len + 1;
    }
    if (question_len + 5 > request_len) return -1;
    question_len += 5; /* root label plus query type and class */
    memcpy(reply, request, question_len);
    dns_header_t *response_header = (dns_header_t *)reply;
    response_header->flags = htons(0x8080 | (ntohs(request_header->flags) & 0x0100));
    response_header->answer_count = htons(1);
    response_header->authority_count = 0;
    response_header->additional_count = 0;
    dns_answer_t *answer = (dns_answer_t *)(reply + question_len);
    *answer = (dns_answer_t){
        .name_pointer = htons(0xC00C), .type = htons(1), .class = htons(1),
        .ttl = htonl(60), .address_length = htons(4), .address = inet_addr(SETUP_PAGE_IP),
    };
    return (int)(question_len + sizeof(*answer));
}

static void dns_redirect_task(void *argument) {
    (void)argument;
    const int socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (socket_fd < 0) {
        ESP_LOGE(TAG, "cannot create captive DNS socket");
        captive_dns_running = false;
        vTaskDelete(NULL);
        return;
    }
    struct sockaddr_in address = { .sin_family = AF_INET, .sin_port = htons(53), .sin_addr.s_addr = htonl(INADDR_ANY) };
    if (bind(socket_fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        ESP_LOGE(TAG, "cannot bind captive DNS socket");
        close(socket_fd);
        captive_dns_running = false;
        vTaskDelete(NULL);
        return;
    }
    struct timeval receive_timeout = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &receive_timeout, sizeof(receive_timeout));
    ESP_LOGI(TAG, "captive DNS redirect listening on UDP 53");
    while (captive_dns_active) {
        uint8_t request[256];
        uint8_t reply[300];
        struct sockaddr_storage client = {0};
        socklen_t client_len = sizeof(client);
        const int request_len = recvfrom(socket_fd, request, sizeof(request), 0, (struct sockaddr *)&client, &client_len);
        if (request_len <= 0) continue;
        const int reply_len = build_dns_reply(request, (size_t)request_len, reply, sizeof(reply));
        if (reply_len > 0) sendto(socket_fd, reply, reply_len, 0, (struct sockaddr *)&client, client_len);
    }
    close(socket_fd);
    captive_dns_running = false;
    ESP_LOGI(TAG, "captive DNS redirect stopped after station connection");
    vTaskDelete(NULL);
}

static esp_err_t root_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, setup_page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t settings_get_handler(httpd_req_t *req) {
    if (!connected) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Connect OnxDesk to Wi-Fi first");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, settings_page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t scan_get_handler(httpd_req_t *req) {
    wifi_scan_config_t config = {0};
    esp_err_t error = esp_wifi_scan_start(&config, true);
    if (error != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Wi-Fi scan unavailable");
        return error;
    }
    uint16_t count = 16;
    wifi_ap_record_t records[16] = {0};
    ESP_RETURN_ON_ERROR(esp_wifi_scan_get_ap_records(&count, records), TAG, "read Wi-Fi scan");
    httpd_resp_set_type(req, "text/plain");
    for (uint16_t i = 0; i < count; i++) {
        if (records[i].ssid[0] != '\0') {
            httpd_resp_send_chunk(req, (const char *)records[i].ssid, HTTPD_RESP_USE_STRLEN);
            httpd_resp_send_chunk(req, "\n", 1);
        }
    }
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t status_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/plain");
    if (connected) return httpd_resp_sendstr(req, "connected");
    if (connection_failed) return httpd_resp_sendstr(req, "failed");
    return httpd_resp_sendstr(req, connect_requested ? "connecting" : "ready");
}

static esp_err_t start_pending_station_connection(void) {
    if (!pending_station_config_valid) return ESP_ERR_INVALID_STATE;
    esp_err_t error = esp_wifi_set_config(WIFI_IF_STA, &pending_station_config);
    if (error == ESP_OK) error = esp_wifi_connect();
    if (error != ESP_OK) {
        connect_requested = false;
        connection_failed = true;
        ESP_LOGE(TAG, "could not start Wi-Fi connection: %s", esp_err_to_name(error));
    }
    return error;
}

static esp_err_t configure_post_handler(httpd_req_t *req) {
    if (req->content_len <= 0 || req->content_len >= MAX_FORM_BODY) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid Wi-Fi form");
        return ESP_FAIL;
    }
    char body[MAX_FORM_BODY] = {0};
    int received = 0;
    while (received < req->content_len) {
        const int chunk = httpd_req_recv(req, body + received, req->content_len - received);
        if (chunk <= 0) return ESP_FAIL;
        received += chunk;
    }
    body[received] = '\0';

    char selected_ssid[sizeof(((wifi_sta_config_t *)0)->ssid)] = {0};
    char ssid[sizeof(((wifi_sta_config_t *)0)->ssid)] = {0};
    char password[sizeof(((wifi_sta_config_t *)0)->password)] = {0};
    if (!form_value(body, "ssid", selected_ssid, sizeof(selected_ssid)) || selected_ssid[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Wi-Fi name is required");
        return ESP_FAIL;
    }
    if (strcmp(selected_ssid, "__manual__") == 0) {
        if (!form_value(body, "manual_ssid", ssid, sizeof(ssid)) || ssid[0] == '\0') {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Wi-Fi name is required");
            return ESP_FAIL;
        }
    } else {
        strlcpy(ssid, selected_ssid, sizeof(ssid));
    }
    if (!form_value(body, "password", password, sizeof(password))) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid Wi-Fi form");
        return ESP_FAIL;
    }

    pending_station_config = (wifi_config_t){0};
    strlcpy((char *)pending_station_config.sta.ssid, ssid, sizeof(pending_station_config.sta.ssid));
    strlcpy((char *)pending_station_config.sta.password, password, sizeof(pending_station_config.sta.password));
    pending_station_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    pending_station_config.sta.failure_retry_cnt = 0;
    pending_station_config_valid = true;
    const bool was_connecting = connected || connect_requested;
    connect_requested = false;
    connection_failed = false;
    configure_after_disconnect = false;
    if (was_connecting) {
        if (esp_wifi_disconnect() == ESP_OK) {
            configure_after_disconnect = true;
        }
    }
    connection_retries = 0;
    connect_requested = true;
    connected = false;
    if (!configure_after_disconnect) {
        esp_err_t error = start_pending_station_connection();
        if (error != ESP_OK) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not start Wi-Fi connection");
            return error;
        }
    }
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, "connecting");
}

static esp_err_t city_search_get_handler(httpd_req_t *req) {
    if (!connected) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Connect Wi-Fi before searching cities");
        return ESP_FAIL;
    }
    const size_t query_length = httpd_req_get_url_query_len(req);
    if (query_length == 0 || query_length > 192) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "City query is required");
        return ESP_FAIL;
    }
    char query[193] = {0};
    char city[CITY_NAME_MAX_LEN] = {0};
    ESP_RETURN_ON_ERROR(httpd_req_get_url_query_str(req, query, sizeof(query)), TAG, "read city query");
    if (httpd_query_key_value(query, "query", city, sizeof(city)) != ESP_OK || city[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "City query is required");
        return ESP_FAIL;
    }
    url_decode(city);
    city_candidate_t candidates[5] = {0};
    size_t count = 0;
    const provider_status_t status = open_meteo_search_city(city, candidates, 5, &count);
    if (status != PROVIDER_READY) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "City search failed");
        return ESP_FAIL;
    }
    cJSON *response = cJSON_CreateArray();
    if (response == NULL) return ESP_ERR_NO_MEM;
    for (size_t index = 0; index < count; index++) {
        cJSON *candidate = cJSON_CreateObject();
        cJSON_AddStringToObject(candidate, "name", candidates[index].name);
        cJSON_AddNumberToObject(candidate, "latitude", candidates[index].latitude);
        cJSON_AddNumberToObject(candidate, "longitude", candidates[index].longitude);
        cJSON_AddStringToObject(candidate, "timezone", candidates[index].timezone);
        cJSON_AddItemToArray(response, candidate);
    }
    char *json = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    if (json == NULL) return ESP_ERR_NO_MEM;
    httpd_resp_set_type(req, "application/json");
    const esp_err_t error = httpd_resp_sendstr(req, json);
    cJSON_free(json);
    return error;
}

static esp_err_t city_save_post_handler(httpd_req_t *req) {
    if (active_settings == NULL || req->content_len <= 0 || req->content_len >= MAX_FORM_BODY) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid city form");
        return ESP_FAIL;
    }
    char body[MAX_FORM_BODY] = {0};
    int received = 0;
    while (received < req->content_len) {
        const int chunk = httpd_req_recv(req, body + received, req->content_len - received);
        if (chunk <= 0) return ESP_FAIL;
        received += chunk;
    }
    body[received] = '\0';
    char name[CITY_NAME_MAX_LEN] = {0};
    char latitude_text[32] = {0};
    char longitude_text[32] = {0};
    char timezone[CITY_TIMEZONE_MAX_LEN] = {0};
    if (!form_value(body, "name", name, sizeof(name)) || !form_value(body, "latitude", latitude_text, sizeof(latitude_text)) ||
        !form_value(body, "longitude", longitude_text, sizeof(longitude_text)) || !form_value(body, "timezone", timezone, sizeof(timezone))) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "City details are required");
        return ESP_FAIL;
    }
    char *latitude_end = NULL;
    char *longitude_end = NULL;
    const double latitude = strtod(latitude_text, &latitude_end);
    const double longitude = strtod(longitude_text, &longitude_end);
    if (name[0] == '\0' || timezone[0] == '\0' || latitude_end == latitude_text || *latitude_end != '\0' ||
        longitude_end == longitude_text || *longitude_end != '\0' || latitude < -90 || latitude > 90 || longitude < -180 || longitude > 180) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid city details");
        return ESP_FAIL;
    }
    strlcpy(active_settings->city, name, sizeof(active_settings->city));
    active_settings->latitude = latitude;
    active_settings->longitude = longitude;
    strlcpy(active_settings->timezone, timezone, sizeof(active_settings->timezone));
    const esp_err_t error = settings_save(active_settings);
    if (error != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not save city");
        return error;
    }
    latest_weather.valid = false;
    city_saved = true;
    network_request_weather_refresh();
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, "City saved. Local time and weather are refreshing.");
}

static esp_err_t market_key_post_handler(httpd_req_t *req) {
    if (active_settings == NULL || req->content_len <= 0 || req->content_len >= MAX_FORM_BODY) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid market key form");
        return ESP_FAIL;
    }
    char body[MAX_FORM_BODY] = {0};
    int received = 0;
    while (received < req->content_len) {
        const int chunk = httpd_req_recv(req, body + received, req->content_len - received);
        if (chunk <= 0) return ESP_FAIL;
        received += chunk;
    }
    body[received] = '\0';
    char key[sizeof(active_settings->finnhub_api_key)] = {0};
    if (!form_value(body, "api_key", key, sizeof(key))) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Market key is required");
        return ESP_FAIL;
    }
    strlcpy(active_settings->finnhub_api_key, key, sizeof(active_settings->finnhub_api_key));
    const esp_err_t error = settings_save(active_settings);
    if (error != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not save market key");
        return error;
    }
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, key[0] == '\0' ? "Finnhub key cleared." : "Finnhub key saved on this device.");
}

static esp_err_t redirect_handler(httpd_req_t *req, httpd_err_code_t error) {
    (void)error;
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://" SETUP_PAGE_IP "/");
    return httpd_resp_sendstr(req, "Open OnxDesk Wi-Fi setup");
}

static void start_setup_server(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    /*
     * City search performs an HTTPS request to Open-Meteo in this server's
     * handler.  The ESP-IDF HTTP server default (4 KiB) is not enough for the
     * TLS/mbedTLS call stack and can corrupt the task stack before the request
     * returns.  Keep the portal responsive while allowing that request to run.
     */
    config.stack_size = 12288;
    config.lru_purge_enable = true;
    httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(httpd_start(&server, &config));
    const httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = root_get_handler };
    const httpd_uri_t settings = { .uri = "/settings", .method = HTTP_GET, .handler = settings_get_handler };
    const httpd_uri_t scan = { .uri = "/scan", .method = HTTP_GET, .handler = scan_get_handler };
    const httpd_uri_t status = { .uri = "/status", .method = HTTP_GET, .handler = status_get_handler };
    const httpd_uri_t configure = { .uri = "/configure", .method = HTTP_POST, .handler = configure_post_handler };
    const httpd_uri_t city_search = { .uri = "/city-search", .method = HTTP_GET, .handler = city_search_get_handler };
    const httpd_uri_t city_save = { .uri = "/city-save", .method = HTTP_POST, .handler = city_save_post_handler };
    const httpd_uri_t market_key = { .uri = "/market-key", .method = HTTP_POST, .handler = market_key_post_handler };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &root));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &settings));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &scan));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &status));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &configure));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &city_search));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &city_save));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &market_key));
    ESP_ERROR_CHECK(httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, redirect_handler));
}

static void network_event_handler(void *arg, esp_event_base_t base, int32_t event, void *data) {
    (void)arg;
    (void)data;
    if (base == WIFI_EVENT && event == WIFI_EVENT_STA_DISCONNECTED) {
        connected = false;
        if (configure_after_disconnect) {
            configure_after_disconnect = false;
            ESP_ERROR_CHECK_WITHOUT_ABORT(start_pending_station_connection());
            return;
        }
        if (connect_requested && connection_retries++ < MAX_CONNECT_RETRIES) {
            ESP_LOGW(TAG, "Wi-Fi connection retry %u/%u", connection_retries, MAX_CONNECT_RETRIES);
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_connect());
        } else if (connect_requested) {
            connect_requested = false;
            connection_failed = true;
            ESP_LOGE(TAG, "Wi-Fi connection failed after %u retries", MAX_CONNECT_RETRIES);
            if (enable_setup_ap() == ESP_OK) {
                ESP_LOGI(TAG, "Wi-Fi setup ready: connect to %s and open http://%s", setup_ap_ssid, SETUP_PAGE_IP);
            }
        }
    } else if (base == IP_EVENT && event == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *got_ip = data;
        if (got_ip != NULL) ip4addr_ntoa_r((const ip4_addr_t *)&got_ip->ip_info.ip, station_ip, sizeof(station_ip));
        connected = true;
        captive_dns_active = false;
        disable_setup_ap();
        connect_requested = false;
        connection_failed = false;
        connection_retries = 0;
        start_initial_sync();
        ESP_LOGI(TAG, "Wi-Fi connected; local settings URL: http://%s/settings", station_ip);
    }
}

static esp_err_t start_captive_dns(void) {
    if (captive_dns_running) return ESP_OK;
    captive_dns_active = true;
    captive_dns_running = true;
    if (xTaskCreate(dns_redirect_task, "captive_dns", 4096, NULL, 5, NULL) != pdPASS) {
        captive_dns_running = false;
        captive_dns_active = false;
        ESP_LOGE(TAG, "failed to start captive DNS redirect");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static esp_err_t enable_setup_ap(void) {
    if (setup_ap_netif == NULL) return ESP_ERR_INVALID_STATE;
    wifi_config_t ap_config = {0};
    strlcpy((char *)ap_config.ap.ssid, setup_ap_ssid, sizeof(ap_config.ap.ssid));
    ap_config.ap.ssid_len = strlen(setup_ap_ssid);
    ap_config.ap.channel = 1;
    ap_config.ap.max_connection = 4;
    ap_config.ap.authmode = WIFI_AUTH_OPEN;
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), TAG, "enable setup access point");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap_config), TAG, "configure setup access point");
    if (!wifi_started) {
        ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start Wi-Fi");
        wifi_started = true;
    }
    advertise_captive_portal(setup_ap_netif);
    setup_ap_active = true;
    return start_captive_dns();
}

static void disable_setup_ap(void) {
    if (!setup_ap_active) return;
    const esp_err_t error = esp_wifi_set_mode(WIFI_MODE_STA);
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "could not disable setup access point: %s", esp_err_to_name(error));
        return;
    }
    setup_ap_active = false;
    ESP_LOGI(TAG, "setup access point disabled after station connection");
}

static void advertise_captive_portal(esp_netif_t *netif) {
    /* DHCP option 114 tells current iOS, Android, and desktop clients where
     * the local setup page lives. The URI must remain valid while DHCP runs. */
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_stop(netif));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_option(netif, ESP_NETIF_OP_SET,
        ESP_NETIF_CAPTIVEPORTAL_URI, captive_portal_uri, strlen(captive_portal_uri)));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_start(netif));
}

esp_err_t network_init(app_settings_t *settings) {
    if (settings == NULL) return ESP_ERR_INVALID_ARG;
    active_settings = settings;
    ESP_RETURN_ON_ERROR(settings_get_setup_ssid(setup_ap_ssid, sizeof(setup_ap_ssid)), TAG, "create setup Wi-Fi name");
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "initialize network interface");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "create event loop");
    setup_ap_netif = esp_netif_create_default_wifi_ap();
    esp_netif_t *station_netif = esp_netif_create_default_wifi_sta();
    if (setup_ap_netif == NULL || station_netif == NULL) return ESP_ERR_NO_MEM;

    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&config), TAG, "initialize Wi-Fi");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, network_event_handler, NULL), TAG, "register Wi-Fi events");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, network_event_handler, NULL), TAG, "register IP event");
    start_setup_server();

    wifi_config_t saved_station = {0};
    ESP_RETURN_ON_ERROR(esp_wifi_get_config(WIFI_IF_STA, &saved_station), TAG, "read saved Wi-Fi settings");
    if (saved_station.sta.ssid[0] != '\0') {
        saved_wifi_present = true;
        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "enable station Wi-Fi");
        ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start station Wi-Fi");
        wifi_started = true;
        connect_requested = true;
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_connect());
        ESP_LOGI(TAG, "saved Wi-Fi found; reconnecting without setup access point");
    } else {
        ESP_RETURN_ON_ERROR(enable_setup_ap(), TAG, "start Wi-Fi setup access point");
        ESP_LOGI(TAG, "Wi-Fi setup ready: connect to %s and open http://%s", setup_ap_ssid, SETUP_PAGE_IP);
    }
    return ESP_OK;
}

bool network_is_connected(void) { return connected; }
bool network_is_connecting(void) { return connect_requested && !connected; }
bool network_has_saved_wifi(void) { return saved_wifi_present; }
bool network_connection_failed(void) { return connection_failed; }
bool network_initial_sync_complete(void) { return initial_sync_completed; }
bool network_time_is_synced(void) { return time_synced; }
bool network_get_weather(weather_snapshot_t *weather) {
    if (weather == NULL || !latest_weather.valid) return false;
    *weather = latest_weather;
    return true;
}
bool network_weather_is_refreshing(void) { return weather_refreshing; }
const char *network_setup_ssid(void) { return setup_ap_ssid; }
bool network_local_url(char *buffer, size_t buffer_size) {
    if (buffer == NULL || buffer_size == 0 || station_ip[0] == '\0') return false;
    return snprintf(buffer, buffer_size, "http://%s/settings", station_ip) < (int)buffer_size;
}
bool network_local_wifi_url(char *buffer, size_t buffer_size) {
    if (buffer == NULL || buffer_size == 0 || station_ip[0] == '\0') return false;
    return snprintf(buffer, buffer_size, "http://%s/", station_ip) < (int)buffer_size;
}
unsigned int network_wifi_signal_level(void) {
    if (!connected) return 0;
    wifi_ap_record_t access_point = {0};
    if (esp_wifi_sta_get_ap_info(&access_point) != ESP_OK) return 0;
    if (access_point.rssi >= -55) return 4;
    if (access_point.rssi >= -67) return 3;
    if (access_point.rssi >= -75) return 2;
    return 1;
}
esp_err_t network_factory_reset(void) { return esp_wifi_restore(); }
