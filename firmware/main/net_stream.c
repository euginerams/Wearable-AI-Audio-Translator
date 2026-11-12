#include "net_stream.h"
#include "config.h"
#include <string.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "lwip/sockets.h"

static const char *TAG = "net";

static EventGroupHandle_t s_wifi_event_group;
static const int WIFI_CONNECTED_BIT = BIT0;
static volatile int s_sock = -1;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Wi-Fi lost, reconnecting");
        esp_wifi_connect();   // automatic Wi-Fi recovery
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void net_stream_wifi_init(void)
{
    nvs_flash_init();
    s_wifi_event_group = xEventGroupCreate();

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);

    wifi_config_t wifi_config = { 0 };
    strncpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password) - 1);

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
    esp_wifi_set_ps(WIFI_PS_NONE);   // no Wi-Fi power saving: steadier streaming

    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
}

bool net_stream_connect(void)
{
    net_stream_disconnect();

    struct sockaddr_in dest = { 0 };
    dest.sin_family = AF_INET;
    dest.sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, SERVER_IP, &dest.sin_addr) != 1) {
        ESP_LOGE(TAG, "SERVER_IP in wifi_secrets.h is not a valid IP address: %s", SERVER_IP);
        return false;
    }

    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) return false;

    if (connect(fd, (struct sockaddr *)&dest, sizeof(dest)) != 0) {
        close(fd);
        return false;
    }
#ifdef TCP_NODELAY
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));   // send small frames immediately
#endif
    s_sock = fd;
    return true;
}

void net_stream_disconnect(void)
{
    int fd = s_sock;
    s_sock = -1;
    if (fd >= 0) {
        close(fd);
    }
}

bool net_stream_is_connected(void)
{
    return s_sock >= 0;
}

// Header and payload are sent in ONE write. Two small writes in a row can make
// TCP wait for the other side's delayed acknowledgement and slow the stream down.
static bool send_framed(uint8_t type, const uint8_t *data, size_t len)
{
    int fd = s_sock;
    if (fd < 0) return false;

    uint8_t buf[4 + 2 * FRAME_SAMPLES * sizeof(int16_t)];
    if (len > sizeof(buf) - 4) return false;          // never happens: frames are one 20 ms frame

    uint32_t header = ((uint32_t)type << 24) | ((uint32_t)len & 0x00FFFFFF);
    memcpy(buf, &header, 4);
    if (len > 0) memcpy(buf + 4, data, len);

    size_t total = 4 + len;
    size_t sent = 0;
    while (sent < total) {
        int n = send(fd, buf + sent, total - sent, 0);
        if (n <= 0) {
            net_stream_disconnect();
            return false;
        }
        sent += (size_t)n;
    }
    return true;
}

bool net_stream_send_frame(const int16_t *samples, size_t num_samples)
{
    return send_framed(NET_FRAME_AUDIO, (const uint8_t *)samples, num_samples * sizeof(int16_t));
}

bool net_stream_send_control(uint8_t type, const uint8_t *payload, size_t len)
{
    return send_framed(type, payload, len);
}

static bool recv_exact(int fd, uint8_t *buf, size_t len)
{
    size_t got = 0;
    while (got < len) {
        int n = recv(fd, buf + got, len - got, 0);
        if (n <= 0) return false;   // 0 = server closed, <0 = error
        got += (size_t)n;
    }
    return true;
}

size_t net_stream_recv_frame(int16_t *out, size_t max_samples)
{
    int fd = s_sock;
    if (fd < 0) return 0;

    uint32_t len_bytes = 0;
    if (!recv_exact(fd, (uint8_t *)&len_bytes, sizeof(len_bytes))) {
        net_stream_disconnect();
        return 0;
    }

    size_t num_samples = len_bytes / sizeof(int16_t);
    if (num_samples > max_samples) num_samples = max_samples;   // should not happen

    if (!recv_exact(fd, (uint8_t *)out, num_samples * sizeof(int16_t))) {
        net_stream_disconnect();
        return 0;
    }
    return num_samples;
}
