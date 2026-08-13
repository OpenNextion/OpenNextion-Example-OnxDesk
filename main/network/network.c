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
#include "lwip/inet.h"
#include "lwip/sockets.h"

#define SETUP_AP_SSID "OnxDesk-Setup"
#define SETUP_PAGE_IP "192.168.4.1"
#define MAX_FORM_BODY 192
#define MAX_CONNECT_RETRIES 5

static const char *TAG = "network";
static volatile bool connected;
static volatile bool connect_requested;
static volatile bool connection_failed;
static volatile bool configure_after_disconnect;
static unsigned int connection_retries;
static wifi_config_t pending_station_config;
static bool pending_station_config_valid;
static char captive_portal_uri[] = "http://" SETUP_PAGE_IP "/";

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
    "<form method=post action=/configure><label>Nearby Wi-Fi networks</label><select id=ssid name=ssid required><option selected disabled value=''>Scanning nearby networks…</option></select><button type=button id=refresh>Refresh network list</button>"
    "<div id=manual hidden><label>Other Wi-Fi name (SSID)</label><input id=manual_ssid name=manual_ssid maxlength=32 autocomplete=off disabled></div>"
    "<label>Password</label><input name=password type=password maxlength=64 autocomplete=current-password>"
    "<button id=connect type=submit>Connect</button></form><p id=status><small>Select a network, then test the connection.</small></p><small>After a successful connection, OnxDesk will open its Clock screen. This setup network remains available until the device is restarted.</small>"
    "<script>let s=document.querySelector('#ssid'),m=document.querySelector('#manual'),i=document.querySelector('#manual_ssid'),f=document.querySelector('form'),b=document.querySelector('#connect'),z=document.querySelector('#status');function manual(){let x=s.value==='__manual__';m.hidden=!x;i.disabled=!x;i.required=x}function add(v,t){let o=document.createElement('option');o.value=v;o.textContent=t;s.append(o)}function scan(){s.textContent='';add('','Scanning nearby networks…');s.options[0].disabled=true;s.value='';fetch('/scan').then(r=>r.text()).then(t=>{s.textContent='';add('','Choose a Wi-Fi network');s.options[0].disabled=true;t.trim().split('\\n').filter(Boolean).forEach(n=>add(n,n));add('__manual__','Other network…')}).catch(()=>{s.textContent='';add('__manual__','Enter Wi-Fi name manually');s.value='__manual__';manual()})}function poll(){fetch('/status').then(r=>r.text()).then(t=>{if(t==='connected'){z.textContent='Connected. OnxDesk is opening its Clock screen.';b.disabled=true}else if(t==='failed'){z.textContent='Connection failed. Check the password and try again.';b.disabled=false}else setTimeout(poll,1000)}).catch(()=>{z.textContent='Could not check connection status.';b.disabled=false})}s.onchange=manual;document.querySelector('#refresh').onclick=scan;f.onsubmit=e=>{e.preventDefault();b.disabled=true;z.textContent='Connecting…';fetch('/configure',{method:'POST',body:new URLSearchParams(new FormData(f))}).then(r=>{if(!r.ok)throw 0;poll()}).catch(()=>{z.textContent='Could not start connection.';b.disabled=false})};scan();</script>"
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
        vTaskDelete(NULL);
        return;
    }
    struct sockaddr_in address = { .sin_family = AF_INET, .sin_port = htons(53), .sin_addr.s_addr = htonl(INADDR_ANY) };
    if (bind(socket_fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        ESP_LOGE(TAG, "cannot bind captive DNS socket");
        close(socket_fd);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "captive DNS redirect listening on UDP 53");
    while (true) {
        uint8_t request[256];
        uint8_t reply[300];
        struct sockaddr_storage client = {0};
        socklen_t client_len = sizeof(client);
        const int request_len = recvfrom(socket_fd, request, sizeof(request), 0, (struct sockaddr *)&client, &client_len);
        if (request_len <= 0) continue;
        const int reply_len = build_dns_reply(request, (size_t)request_len, reply, sizeof(reply));
        if (reply_len > 0) sendto(socket_fd, reply, reply_len, 0, (struct sockaddr *)&client, client_len);
    }
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
    const httpd_uri_t status = { .uri = "/status", .method = HTTP_GET, .handler = status_get_handler };
    const httpd_uri_t configure = { .uri = "/configure", .method = HTTP_POST, .handler = configure_post_handler };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &root));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &scan));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &status));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &configure));
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
        }
    } else if (base == IP_EVENT && event == IP_EVENT_STA_GOT_IP) {
        connected = true;
        connect_requested = false;
        connection_failed = false;
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
    if (xTaskCreate(dns_redirect_task, "captive_dns", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to start captive DNS redirect");
        return ESP_ERR_NO_MEM;
    }

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
bool network_connection_failed(void) { return connection_failed; }
esp_err_t network_factory_reset(void) { return esp_wifi_restore(); }
