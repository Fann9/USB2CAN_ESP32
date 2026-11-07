#include "wifi_console.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "lwip/tcp.h"
#include "nvs_flash.h"

#define WIFI_SSID "AL1S-CAN"
#define WIFI_PASSWORD "wuhuang1313113"
#define CONSOLE_PORT 2323
static const char *TAG="wifi_console";
static QueueHandle_t events;
static SemaphoreHandle_t socket_lock;
static esp_netif_t *ap_netif;
static int client_fd=-1; /* Protected by socket_lock. */
static volatile bool disconnect_requested;
static volatile bool server_ready;

static bool send_event(wifi_console_event_kind_t kind,uint8_t byte)
{
    wifi_console_event_t event={.kind=kind,.byte=byte};
    return xQueueSend(events,&event,pdMS_TO_TICKS(100))==pdTRUE;
}

static void close_client(void)
{
    xSemaphoreTake(socket_lock,portMAX_DELAY);
    if (client_fd>=0) {
        shutdown(client_fd,SHUT_RDWR);
        close(client_fd);
        client_fd=-1;
    }
    xSemaphoreGive(socket_lock);
    disconnect_requested=false;
    (void)send_event(WIFI_CONSOLE_DISCONNECTED,0);
}

static void server_task(void *arg)
{
    (void)arg;
    int listener=socket(AF_INET,SOCK_STREAM,IPPROTO_IP);
    if (listener<0) { ESP_LOGE(TAG,"socket: %d",errno); vTaskDelete(NULL); }
    int reuse=1;
    setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
    struct sockaddr_in address={.sin_family=AF_INET,.sin_port=htons(CONSOLE_PORT),.sin_addr.s_addr=htonl(INADDR_ANY)};
    if (bind(listener,(struct sockaddr *)&address,sizeof(address))<0 || listen(listener,1)<0) {
        ESP_LOGE(TAG,"bind/listen failed: %d",errno);
        close(listener); vTaskDelete(NULL);
    }
    server_ready=true;
    ESP_LOGI(TAG,"TCP console listening on port %d",CONSOLE_PORT);
    int active=-1;
    for (;;) {
        if (active>=0 && disconnect_requested) {
            close_client(); active=-1;
        }
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(listener,&readfds);
        if (active>=0) FD_SET(active,&readfds);
        struct timeval timeout={.tv_sec=0,.tv_usec=20000};
        int ready=select((active>listener?active:listener)+1,&readfds,NULL,NULL,&timeout);
        if (ready<0) {
            if (errno!=EINTR) ESP_LOGW(TAG,"select: %d",errno);
            continue;
        }
        if (FD_ISSET(listener,&readfds)) {
            int incoming=accept(listener,NULL,NULL);
            if (incoming>=0) {
                if (active>=0) {
                    static const char busy[]="Console busy\r\n";
                    send(incoming,busy,sizeof(busy)-1,0);
                    close(incoming);
                } else {
                    int nodelay=1;
                    setsockopt(incoming,IPPROTO_TCP,TCP_NODELAY,&nodelay,sizeof(nodelay));
                    xSemaphoreTake(socket_lock,portMAX_DELAY);
                    client_fd=incoming;
                    xSemaphoreGive(socket_lock);
                    active=incoming;
                    if (!send_event(WIFI_CONSOLE_CONNECTED,0)) {
                        close_client(); active=-1;
                    }
                }
            }
        }
        if (active>=0 && FD_ISSET(active,&readfds)) {
            uint8_t bytes[64];
            int count=recv(active,bytes,sizeof(bytes),0);
            if (count<=0) { close_client(); active=-1; }
            else {
                for (int i=0;i<count;++i) {
                    if (!send_event(WIFI_CONSOLE_BYTE,bytes[i])) {
                        close_client(); active=-1; break;
                    }
                }
            }
        }
    }
}

bool wifi_console_start(void)
{
    events=xQueueCreate(512,sizeof(wifi_console_event_t));
    socket_lock=xSemaphoreCreateMutex();
    if (!events || !socket_lock) return false;
    esp_err_t err=nvs_flash_init();
    if (err!=ESP_OK) { ESP_LOGE(TAG,"NVS init: %s",esp_err_to_name(err)); return false; }
    err=esp_netif_init();
    if (err==ESP_OK) err=esp_event_loop_create_default();
    if (err!=ESP_OK) { ESP_LOGE(TAG,"network init: %s",esp_err_to_name(err)); return false; }
    ap_netif=esp_netif_create_default_wifi_ap();
    if (!ap_netif) return false;
    wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();
    err=esp_wifi_init(&init);
    if (err==ESP_OK) err=esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err==ESP_OK) err=esp_wifi_set_mode(WIFI_MODE_AP);
    wifi_config_t config={.ap={.ssid=WIFI_SSID,.ssid_len=sizeof(WIFI_SSID)-1,
        .password=WIFI_PASSWORD,.channel=1,.max_connection=1,.authmode=WIFI_AUTH_WPA2_PSK}};
    if (err==ESP_OK) err=esp_wifi_set_config(WIFI_IF_AP,&config);
    if (err==ESP_OK) err=esp_wifi_start();
    if (err!=ESP_OK) { ESP_LOGE(TAG,"AP start: %s",esp_err_to_name(err)); return false; }
    ESP_LOGI(TAG,"SoftAP SSID %s; TCP port %d",WIFI_SSID,CONSOLE_PORT);
    return xTaskCreatePinnedToCore(server_task,"wifi_console",4096,NULL,3,NULL,0)==pdPASS;
}

bool wifi_console_pop_event(wifi_console_event_t *event)
{
    return events && xQueueReceive(events,event,0)==pdTRUE;
}

bool wifi_console_write(const char *data,size_t size)
{
    if (!socket_lock) return false;
    xSemaphoreTake(socket_lock,portMAX_DELAY);
    bool okay=client_fd>=0;
    while (okay && size) {
        int sent=send(client_fd,data,size,MSG_DONTWAIT);
        if (sent>0) { data+=sent; size-=sent; }
        else if (sent<0 && errno==EINTR) continue;
        else { okay=false; disconnect_requested=true; }
    }
    xSemaphoreGive(socket_lock);
    return okay;
}

void wifi_console_disconnect(void) { disconnect_requested=true; }
bool wifi_console_connected(void)
{
    if (!socket_lock) return false;
    xSemaphoreTake(socket_lock,portMAX_DELAY);
    bool connected=client_fd>=0;
    xSemaphoreGive(socket_lock);
    return connected;
}
bool wifi_console_ready(void) { return server_ready; }
void wifi_console_ip(char *out,size_t size)
{
    if (!size) return;
    esp_netif_ip_info_t info;
    if (ap_netif && esp_netif_get_ip_info(ap_netif,&info)==ESP_OK)
        snprintf(out,size,IPSTR,IP2STR(&info.ip));
    else snprintf(out,size,"unavailable");
}
