#include "midi_in.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "usb/usb_helpers.h"
#include "usb/usb_host.h"

#define QUEUE_LEN 64
#define MAX_RETRIES 3 // подряд неудачных трансферов, потом сдаёмся и уходим в демо
#define USB_CLASS_AUDIO 0x01
#define USB_SUBCLASS_MIDISTREAMING 0x03

static const char *TAG = "midi";

static usb_host_client_handle_t s_client;
static QueueHandle_t s_queue;

// Всё ниже трогается только из задачи клиента (колбэки тоже идут в ней).
static usb_device_handle_t s_dev;
static usb_transfer_t *s_xfer;
static uint8_t s_iface;
static uint8_t s_ep;
static int s_in_flight;
static int s_closing;
static int s_errors;
static volatile int s_connected;
// Диагностика: что пришло от устройства. Пишет только колбэк приёма, читает stats_task.
static acid_midi_stats_t s_stats;
static uint32_t s_packets, s_dropped;

int midi_in_connected(void)
{
    return s_connected;
}

int midi_in_receive(acid_midi_event_t *ev)
{
    return xQueueReceive(s_queue, ev, 0) == pdTRUE;
}

// Только когда трансфер не в полёте: стек не отдаст интерфейс, пока колбэк не вернулся.
static void finish_close(void)
{
    esp_err_t rel = usb_host_interface_release(s_client, s_dev, s_iface);
    esp_err_t cls = usb_host_device_close(s_client, s_dev);
    usb_host_transfer_free(s_xfer);
    s_xfer = NULL;
    s_dev = NULL;
    s_closing = 0;
    if (rel != ESP_OK || cls != ESP_OK) {
        ESP_LOGE(TAG, "midi close failed: release %s, close %s", esp_err_to_name(rel), esp_err_to_name(cls));
    } else {
        ESP_LOGI(TAG, "midi device closed");
    }
}

// Без приёма контроллер мёртв: снимаем live, синт уходит в демо (с нуля, ноты чистые).
static void give_up(const char *why)
{
    s_connected = 0;
    ESP_LOGW(TAG, "midi receive stopped: %s", why); // обычно это просто выдернутый кабель
}

static void submit(void)
{
    s_xfer->num_bytes = s_xfer->data_buffer_size;
    if (usb_host_transfer_submit(s_xfer) == ESP_OK) {
        s_in_flight = 1;
    } else {
        give_up("bulk in submit failed");
    }
}

static void xfer_cb(usb_transfer_t *xfer)
{
    s_in_flight = 0;
    if (s_closing) {
        return; // закроемся в client_task, когда колбэк вернётся
    }
    if (xfer->status == USB_TRANSFER_STATUS_COMPLETED) {
        s_errors = 0;
        acid_midi_event_t ev[16];
        int n = acid_midi_parse(xfer->data_buffer, (size_t)xfer->actual_num_bytes, ev, 16);
        s_packets += (uint32_t)xfer->actual_num_bytes / 4;
        acid_midi_count(&s_stats, ev, n);
        for (int i = 0; i < n; i++) {
            if (xQueueSend(s_queue, &ev[i], 0) != pdTRUE) { // очередь полна - событие теряем, рендер не ждёт
                s_dropped++;
            }
        }
        submit();
    } else if (xfer->status == USB_TRANSFER_STATUS_NO_DEVICE || xfer->status == USB_TRANSFER_STATUS_CANCELED) {
        // Устройство уходит: дальше придёт DEV_GONE.
    } else if (++s_errors <= MAX_RETRIES) {
        // STALL или ошибка шины: стек уже остановил и очистил эндпоинт, снимаем halt и пробуем снова.
        ESP_LOGW(TAG, "bulk in status %d, retry %d", xfer->status, s_errors);
        usb_host_endpoint_clear(s_dev, s_ep);
        submit();
    } else {
        give_up("too many transfer errors");
    }
}

// Ищет интерфейс MIDIStreaming и его bulk IN. 1 если нашёл.
static int find_midi(const usb_config_desc_t *cfg, uint8_t *iface, uint8_t *ep, uint16_t *mps)
{
    int offset = 0;
    const usb_standard_desc_t *d = (const usb_standard_desc_t *)cfg;
    while ((d = usb_parse_next_descriptor_of_type(d, cfg->wTotalLength, USB_B_DESCRIPTOR_TYPE_INTERFACE,
                                                  &offset)) != NULL) {
        const usb_intf_desc_t *intf = (const usb_intf_desc_t *)d;
        if (intf->bInterfaceClass != USB_CLASS_AUDIO ||
            intf->bInterfaceSubClass != USB_SUBCLASS_MIDISTREAMING) {
            continue;
        }
        for (int i = 0; i < intf->bNumEndpoints; i++) {
            int ep_off = offset;
            const usb_ep_desc_t *e =
                usb_parse_endpoint_descriptor_by_index(intf, i, cfg->wTotalLength, &ep_off);
            if (e && USB_EP_DESC_GET_EP_DIR(e) && USB_EP_DESC_GET_XFERTYPE(e) == USB_TRANSFER_TYPE_BULK) {
                *iface = intf->bInterfaceNumber;
                *ep = e->bEndpointAddress;
                *mps = USB_EP_DESC_GET_MPS(e);
                return 1;
            }
        }
    }
    return 0;
}

static void try_open(uint8_t addr)
{
    if (s_dev) {
        return; // одно MIDI-устройство за раз
    }
    usb_device_handle_t dev;
    if (usb_host_device_open(s_client, addr, &dev) != ESP_OK) {
        return;
    }
    const usb_config_desc_t *cfg;
    uint16_t mps = 0;
    if (usb_host_get_active_config_descriptor(dev, &cfg) != ESP_OK ||
        !find_midi(cfg, &s_iface, &s_ep, &mps)) {
        usb_host_device_close(s_client, dev);
        return;
    }
    if (usb_host_interface_claim(s_client, dev, s_iface, 0) != ESP_OK) {
        ESP_LOGW(TAG, "addr %d: claim of midi interface %d failed", addr, s_iface);
        usb_host_device_close(s_client, dev);
        return;
    }
    if (usb_host_transfer_alloc(mps, 0, &s_xfer) != ESP_OK) {
        usb_host_interface_release(s_client, dev, s_iface);
        usb_host_device_close(s_client, dev);
        return;
    }
    s_dev = dev;
    s_xfer->device_handle = dev;
    s_xfer->bEndpointAddress = s_ep;
    s_xfer->callback = xfer_cb;
    s_xfer->timeout_ms = 0;
    s_errors = 0;
    s_connected = 1;
    submit();
    if (!s_in_flight) {
        finish_close();
        return;
    }
    ESP_LOGI(TAG, "midi device addr %d: interface %d, ep 0x%02x, mps %d", addr, s_iface, s_ep, mps);
}

static void client_cb(const usb_host_client_event_msg_t *msg, void *arg)
{
    (void)arg;
    if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        try_open(msg->new_dev.address);
    } else if (msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE && s_dev == msg->dev_gone.dev_hdl) {
        s_connected = 0;
        ESP_LOGI(TAG, "midi device gone");
        // Стек сам останавливает эндпоинты ушедшего устройства; трансфер в полёте вернётся с NO_DEVICE.
        if (s_in_flight) {
            s_closing = 1;
        } else {
            finish_close();
        }
    }
}

void midi_in_stats(acid_midi_stats_t *st, uint32_t *packets, uint32_t *dropped)
{
    *st = s_stats;
    *packets = s_packets;
    *dropped = s_dropped;
}

static void client_task(void *arg)
{
    (void)arg;
    const usb_host_client_config_t cfg = {
        .is_synchronous = false,
        .max_num_event_msg = 8,
        .async = {.client_event_callback = client_cb, .callback_arg = NULL},
    };
    ESP_ERROR_CHECK(usb_host_client_register(&cfg, &s_client));
    // Устройства, успевшие подключиться до регистрации, событий NEW_DEV уже не дадут.
    uint8_t addrs[8];
    int n = 0;
    if (usb_host_device_addr_list_fill(sizeof addrs, addrs, &n) == ESP_OK) {
        for (int i = 0; i < n; i++) {
            try_open(addrs[i]);
        }
    }
    for (;;) {
        usb_host_client_handle_events(s_client, portMAX_DELAY);
        if (s_closing && !s_in_flight) {
            finish_close();
        }
    }
}

esp_err_t midi_in_start(void)
{
    s_queue = xQueueCreate(QUEUE_LEN, sizeof(acid_midi_event_t));
    if (!s_queue) {
        return ESP_ERR_NO_MEM;
    }
    xTaskCreatePinnedToCore(client_task, "midi_in", 4096, NULL, 5, NULL, 0);
    return ESP_OK;
}
