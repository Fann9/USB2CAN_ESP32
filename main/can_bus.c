#include "can_bus.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG="can_bus";
static QueueHandle_t requests, received, echo_frames;
static TaskHandle_t worker;
static volatile bool running;
static volatile uint32_t dropped, tx_failed;
static volatile uint32_t periodic_ms, periodic_id;
static can_config_t active_config;
static uint32_t standard_counts[2048];
static uint32_t echo_id=UINT32_MAX;
static uint32_t echo_dropped;

typedef struct {
    enum { RECONFIGURE, TRANSMIT, STOP_PERIODIC } kind;
    can_config_t config;
    twai_message_t frame;
    uint32_t period_ms;
    TaskHandle_t reply;
    bool *result;
} request_t;

static twai_timing_config_t timing(uint32_t bitrate)
{
    switch (bitrate) {
    case 25000: { twai_timing_config_t t=TWAI_TIMING_CONFIG_25KBITS(); return t; }
    case 50000: { twai_timing_config_t t=TWAI_TIMING_CONFIG_50KBITS(); return t; }
    case 100000: { twai_timing_config_t t=TWAI_TIMING_CONFIG_100KBITS(); return t; }
    case 125000: { twai_timing_config_t t=TWAI_TIMING_CONFIG_125KBITS(); return t; }
    case 250000: { twai_timing_config_t t=TWAI_TIMING_CONFIG_250KBITS(); return t; }
    case 500000: { twai_timing_config_t t=TWAI_TIMING_CONFIG_500KBITS(); return t; }
    case 800000: return (twai_timing_config_t){.clk_src=TWAI_CLK_SRC_DEFAULT,.quanta_resolution_hz=8000000,.tseg_1=5,.tseg_2=4,.sjw=1};
    default: return (twai_timing_config_t){.clk_src=TWAI_CLK_SRC_DEFAULT,.quanta_resolution_hz=10000000,.tseg_1=5,.tseg_2=4,.sjw=1};
    }
}

static bool configure(const can_config_t *cfg)
{
    if (running) {
        twai_stop();
        twai_driver_uninstall();
        running=false;
    }
    twai_general_config_t g=TWAI_GENERAL_CONFIG_DEFAULT(cfg->tx_gpio,cfg->rx_gpio,TWAI_MODE_NORMAL);
    g.tx_queue_len=32;
    g.rx_queue_len=128;
    twai_timing_config_t t=timing(cfg->bitrate);
    twai_filter_config_t f=TWAI_FILTER_CONFIG_ACCEPT_ALL();
    esp_err_t err=twai_driver_install(&g,&t,&f);
    if (err==ESP_OK) err=twai_start();
    if (err!=ESP_OK) {
        ESP_LOGE(TAG,"TWAI start failed: %s",esp_err_to_name(err));
        (void)twai_driver_uninstall();
        return false;
    }
    running=true;
    xQueueReset(received);
    ESP_LOGI(TAG,"TWAI running on CPU%d: %lu bit/s, TX=%lu RX=%lu",xPortGetCoreID(),
        (unsigned long)cfg->bitrate,(unsigned long)cfg->tx_gpio,(unsigned long)cfg->rx_gpio);
    return true;
}

static void can_worker(void *arg)
{
    (void)arg;
    twai_message_t repeated = {0};
    TickType_t period_ticks = 0, next_due = 0;
    for (;;) {
        request_t req;
        while (xQueueReceive(requests,&req,0)==pdTRUE) {
            if (req.kind==RECONFIGURE) {
                period_ticks=0;
                periodic_ms=0;
                bool was_running=running;
                bool ok=configure(&req.config);
                if (ok) active_config=req.config;
                else if (was_running) (void)configure(&active_config);
                *req.result=ok;
                xTaskNotifyGive(req.reply);
            } else if (req.kind==STOP_PERIODIC) {
                if (period_ticks && running) (void)twai_clear_transmit_queue();
                period_ticks=0;
                periodic_ms=0;
            } else {
                /* A one-shot frame also cancels the prior repeating frame. */
                if (period_ticks && running) (void)twai_clear_transmit_queue();
                period_ticks=0;
                periodic_ms=0;
                if (running) {
                    if (twai_transmit(&req.frame,0)!=ESP_OK) ++tx_failed;
                    if (req.period_ms) {
                        repeated=req.frame;
                        period_ticks=pdMS_TO_TICKS(req.period_ms);
                        next_due=xTaskGetTickCount()+period_ticks;
                        periodic_id=req.frame.identifier;
                        periodic_ms=req.period_ms;
                    }
                }
            }
        }
        if (running && period_ticks && (int32_t)(xTaskGetTickCount()-next_due)>=0) {
            if (twai_transmit(&repeated,0)!=ESP_OK) ++tx_failed;
            next_due+=period_ticks;
            if ((int32_t)(xTaskGetTickCount()-next_due)>=0)
                next_due=xTaskGetTickCount()+period_ticks;
        }
        if (running) {
            twai_message_t frame;
            if (twai_receive(&frame,pdMS_TO_TICKS(2))==ESP_OK) {
                if (!frame.extd && frame.identifier<=0x7ff)
                    __atomic_fetch_add(&standard_counts[frame.identifier],1,__ATOMIC_RELAXED);
                uint32_t selected=__atomic_load_n(&echo_id,__ATOMIC_RELAXED);
                if ((selected==UINT32_MAX-1 ||
                     (!frame.extd && frame.identifier==selected)) &&
                    xQueueSend(echo_frames,&frame,0)!=pdTRUE)
                    __atomic_fetch_add(&echo_dropped,1,__ATOMIC_RELAXED);
                if (xQueueSend(received,&frame,0)!=pdTRUE) ++dropped;
            }
        } else vTaskDelay(pdMS_TO_TICKS(2));
    }
}

bool can_bus_start(const can_config_t *config)
{
    requests=xQueueCreate(64,sizeof(request_t));
    received=xQueueCreate(256,sizeof(twai_message_t));
    echo_frames=xQueueCreate(128,sizeof(twai_message_t));
    if (!requests || !received || !echo_frames) return false;
    if (xTaskCreatePinnedToCore(can_worker,"can_worker",4096,NULL,5,&worker,1)!=pdPASS) return false;
    return can_bus_reload(config);
}

bool can_bus_reload(const can_config_t *config)
{
    if (!worker) return false;
    bool result=false;
    request_t req={.kind=RECONFIGURE,.config=*config,.reply=xTaskGetCurrentTaskHandle(),.result=&result};
    if (xQueueSend(requests,&req,pdMS_TO_TICKS(100))!=pdTRUE) return false;
    ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
    return result;
}

bool can_bus_send(uint32_t id,const uint8_t *data,uint8_t length,uint32_t period_ms)
{
    if (!running || id>0x7ff || length>8 || (!data && length) ||
        period_ms>60000 || (period_ms && pdMS_TO_TICKS(period_ms)==0)) return false;
    request_t req={.kind=TRANSMIT,.period_ms=period_ms};
    req.frame.identifier=id;
    req.frame.data_length_code=length;
    if (length) memcpy(req.frame.data,data,length);
    return xQueueSend(requests,&req,0)==pdTRUE;
}

bool can_bus_stop_periodic(void)
{
    if (!worker) return false;
    request_t req={.kind=STOP_PERIODIC};
    return xQueueSend(requests,&req,0)==pdTRUE;
}

uint32_t can_bus_period_ms(void) { return periodic_ms; }
uint32_t can_bus_period_id(void) { return periodic_id; }

bool can_bus_receive(twai_message_t *frame) { return received && xQueueReceive(received,frame,0)==pdTRUE; }
bool can_bus_running(void) { return running; }
uint32_t can_bus_dropped(void) { return dropped; }
uint32_t can_bus_tx_failed(void) { return tx_failed; }

bool can_bus_measure_hz(uint16_t id,uint32_t sample_ms,uint32_t *frames)
{
    if (!running || id>0x7ff || !frames || sample_ms<100 || sample_ms>10000) return false;
    uint32_t before=__atomic_load_n(&standard_counts[id],__ATOMIC_RELAXED);
    vTaskDelay(pdMS_TO_TICKS(sample_ms));
    uint32_t after=__atomic_load_n(&standard_counts[id],__ATOMIC_RELAXED);
    if (!running) return false;
    *frames=after-before;
    return true;
}

uint32_t can_bus_seen_count(uint16_t id)
{
    return id<=0x7ff ? __atomic_load_n(&standard_counts[id],__ATOMIC_RELAXED) : 0;
}

bool can_bus_echo_start(uint32_t id)
{
    if (!running || (id>0x7ff && id!=UINT32_MAX) || !echo_frames) return false;
    xQueueReset(echo_frames);
    __atomic_store_n(&echo_dropped,0,__ATOMIC_RELAXED);
    __atomic_store_n(&echo_id,id==UINT32_MAX?UINT32_MAX-1:id,__ATOMIC_RELAXED);
    return true;
}

void can_bus_echo_stop(void)
{
    __atomic_store_n(&echo_id,UINT32_MAX,__ATOMIC_RELAXED);
    if (echo_frames) xQueueReset(echo_frames);
}

bool can_bus_echo_receive(twai_message_t *frame)
{
    return echo_frames && xQueueReceive(echo_frames,frame,0)==pdTRUE;
}

uint32_t can_bus_echo_dropped(void)
{
    return __atomic_load_n(&echo_dropped,__ATOMIC_RELAXED);
}
