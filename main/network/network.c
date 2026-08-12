#include "network.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#define SETUP_AP_SSID "OnxDesk-Setup"
#define SETUP_PAGE_IP "192.168.4.1"
#define MAX_FORM_BODY 192
#define MAX_CONNECT_RETRIES 5

static const char *TAG = "network";
static bool connected;
static bool connect_requested;
static unsigned int connection_retries;
static char captive_portal_uri[] = "http://" SETUP_PAGE_IP "/";

static const char setup_page[] =
    "<!doctype html><html><head><meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>OnxDesk setup</title><style>body{max-width:34rem;margin:2rem auto;padding:0 1rem;background:#0d0d1a;color:#fff;font:16px system-ui}"
    "h1{color:#5dcaa5}input,select,button{box-sizing:border-box;width:100%;padding:.8rem;margin:.4rem 0;border-radius:.5rem;border:0}"
    "input,select{background:#1a1a2e;color:#fff}button{background:#5dcaa5;color:#0d0d1a;font-weight:bold}small{color:#8a8a9e}</style></head><body>"
    "<h1>OnxDesk Wi-Fi setup</h1><p>Choose your 2.4 GHz Wi-Fi network, then enter its password.</p>"
    "<form method=post action=/configure><label>Nearby networks</label><select id=net><option>Scanning…</option></select>"
    "<label>Wi-Fi name (SSID)</label><input id=ssid name=ssid maxlength=32 required autocomplete=off>"
    "<label>Password</label><input name=password type=password maxlength=64 autocomplete=current-password>"
    "<button type=submit>Connect</button></form><small>After a successful connection, OnxDesk will open its Clock screen. This setup network remains available until the device is restarted.</small>"
    "<script>fetch('/scan').then(r=>r.text()).then(t=>{let s=document.querySelector('#net');s.textContent='';t.trim().split('\\n').filter(Boolean).forEach(n=>{let o=document.createElement('option');o.textContent=o.value=n;s.append(o)});s.onchange=()=>document.querySelector('#ssid').value=s.value}).catch(()=>{document.querySelector('#net').textContent='Scan unavailable'});</script>"
    "</body></html>";

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

static bool form_value(char *body, const char *name, char *output, size_t output_size) {
    const size_t name_len = strlen(name);
    for (char *field = body; field != NULL;) {
        char *next = strchr(field, '&');
        if (next != NULL) *next = '\0';
        if (strncmp(field, name, name_len) == 0 && field[name_len] == '=') {
            strlcpy(output, field + name_len + 1, output_size);
            url_decode(output);
            return true;
        }
        field = next == NULL ? NULL : next + 1;
    }
    return false;
}

static esp_err_t root_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, setup_page, HTTPD_RESP_USE_STRLEN);
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

static esp_err_t configure_post_handler(httpd_req_t *req) {
    if (req->content_len <= 0 || req->content_len >= MAX_FORM_BODY) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid Wi-Fi form");
        return ESP_FAIL;
    }
    char body[MAX_FORM_BODY] = {0};
    int received = httpd_req_recv(req, body, req->content_len);
    if (received <= 0) return ESP_FAIL;
    body[received] = '\0';

    char ssid[sizeof(((wifi_sta_config_t *)0)->ssid)] = {0};
    char password[sizeof(((wifi_sta_config_t *)0)->password)] = {0};
    if (!form_value(body, "ssid", ssid, sizeof(ssid)) || ssid[0] == '\0' ||
        !form_value(body, "password", password, sizeof(password))) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Wi-Fi name is required");
        return ESP_FAIL;
    }

    wifi_config_t station_config = {0};
    strlcpy((char *)station_config.sta.ssid, ssid, sizeof(station_config.sta.ssid));
    strlcpy((char *)station_config.sta.password, password, sizeof(station_config.sta.password));
    station_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    station_config.sta.failure_retry_cnt = MAX_CONNECT_RETRIES;
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_disconnect());
    if (esp_wifi_set_config(WIFI_IF_STA, &station_config) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not save Wi-Fi settings");
        return ESP_FAIL;
    }
    connection_retries = 0;
    connect_requested = true;
    connected = false;
    esp_err_t error = esp_wifi_connect();
    if (error != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not start Wi-Fi connection");
        return error;
    }
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_sendstr(req, "<meta http-equiv=refresh content='4;url=/'><body style='font-family:system-ui;background:#0d0d1a;color:white;padding:2rem'>Connecting OnxDesk to Wi-Fi…</body>");
}

static esp_err_t redirect_handler(httpd_req_t *req, httpd_err_code_t error) {
    (void)error;
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://" SETUP_PAGE_IP "/");
    return httpd_resp_sendstr(req, "Open OnxDesk Wi-Fi setup");
}

static void start_setup_server(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(httpd_start(&server, &config));
    const httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = root_get_handler };
    const httpd_uri_t scan = { .uri = "/scan", .method = HTTP_GET, .handler = scan_get_handler };
    const httpd_uri_t configure = { .uri = "/configure", .method = HTTP_POST, .handler = configure_post_handler };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &root));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &scan));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &configure));
    ESP_ERROR_CHECK(httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, redirect_handler));
}

static void network_event_handler(void *arg, esp_event_base_t base, int32_t event, void *data) {
    (void)arg;
    (void)data;
    if (base == WIFI_EVENT && event == WIFI_EVENT_STA_DISCONNECTED) {
        connected = false;
        if (connect_requested && connection_retries++ < MAX_CONNECT_RETRIES) {
            ESP_LOGW(TAG, "Wi-Fi connection retry %u/%u", connection_retries, MAX_CONNECT_RETRIES);
            esp_wifi_connect();
        }
    } else if (base == IP_EVENT && event == IP_EVENT_STA_GOT_IP) {
        connected = true;
        connection_retries = 0;
        ESP_LOGI(TAG, "Wi-Fi connected; OnxDesk data services may start");
    }
}

static void start_softap(void) {
    wifi_config_t ap_config = {0};
    strlcpy((char *)ap_config.ap.ssid, SETUP_AP_SSID, sizeof(ap_config.ap.ssid));
    ap_config.ap.ssid_len = strlen(SETUP_AP_SSID);
    ap_config.ap.channel = 1;
    ap_config.ap.max_connection = 4;
    ap_config.ap.authmode = WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static void advertise_captive_portal(esp_netif_t *netif) {
    /* DHCP option 114 tells current iOS, Android, and desktop clients where
     * the local setup page lives. The URI must remain valid while DHCP runs. */
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_stop(netif));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_option(netif, ESP_NETIF_OP_SET,
        ESP_NETIF_CAPTIVEPORTAL_URI, captive_portal_uri, strlen(captive_portal_uri)));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_start(netif));
}

esp_err_t network_init(void) {
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "initialize network interface");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "create event loop");
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    esp_netif_t *station_netif = esp_netif_create_default_wifi_sta();
    if (ap_netif == NULL || station_netif == NULL) return ESP_ERR_NO_MEM;

    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&config), TAG, "initialize Wi-Fi");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, network_event_handler, NULL), TAG, "register Wi-Fi events");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, network_event_handler, NULL), TAG, "register IP event");
    start_softap();
    advertise_captive_portal(ap_netif);
    start_setup_server();

    wifi_config_t saved_station = {0};
    ESP_RETURN_ON_ERROR(esp_wifi_get_config(WIFI_IF_STA, &saved_station), TAG, "read saved Wi-Fi settings");
    if (saved_station.sta.ssid[0] != '\0') {
        connect_requested = true;
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_connect());
    }
    ESP_LOGI(TAG, "Wi-Fi setup ready: connect to %s and open http://%s", SETUP_AP_SSID, SETUP_PAGE_IP);
    return ESP_OK;
}

bool network_is_connected(void) { return connected; }
bool network_is_connecting(void) { return connect_requested && !connected; }
esp_err_t network_factory_reset(void) { return esp_wifi_restore(); }
