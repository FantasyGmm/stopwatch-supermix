#include "codex_ble_transport.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <host/ble_hs.h>
#include <host/ble_hs_id.h>
#include <host/util/util.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <os/os_mbuf.h>
#include <services/gap/ble_svc_gap.h>
#include <services/gatt/ble_svc_gatt.h>
#include <store/config/ble_store_config.h>

#include <algorithm>
#include <cstring>

extern "C" void ble_store_config_init(void);


namespace {

constexpr const char* kTag = "CodexBLE";
constexpr size_t kMaxFrameSize = 256;
constexpr size_t kFrameQueueDepth = 32;
constexpr size_t kVoiceControlQueueDepth = 8;
constexpr size_t kMaxNotificationSize = 256;
constexpr uint16_t kDefaultMtu = 23;

struct QueuedFrame {
    uint16_t length = 0;
    uint8_t data[kMaxFrameSize] = {};
};

struct QueuedVoiceControl {
    uint16_t length = 0;
    uint8_t data[ble_voice_probe::kControlCommandSize] = {};
};

CodexBleTransport* g_transport = nullptr;
uint8_t g_own_address_type = 0;
std::string g_device_name;

const ble_uuid128_t kServiceUuid = BLE_UUID128_INIT(
    0x44, 0x4f, 0x43, 0x48, 0x43, 0x54, 0x41, 0x57,
    0x50, 0x4f, 0x74, 0x54, 0x01, 0x00, 0x23, 0x7a
);
const ble_uuid128_t kStatusRxUuid = BLE_UUID128_INIT(
    0x44, 0x4f, 0x43, 0x48, 0x43, 0x54, 0x41, 0x57,
    0x50, 0x4f, 0x74, 0x54, 0x02, 0x00, 0x23, 0x7a
);
const ble_uuid128_t kVoiceControlUuid = BLE_UUID128_INIT(
    0x44, 0x4f, 0x43, 0x48, 0x43, 0x54, 0x41, 0x57,
    0x50, 0x4f, 0x74, 0x54, 0x03, 0x00, 0x23, 0x7a
);
const ble_uuid128_t kVoiceTxUuid = BLE_UUID128_INIT(
    0x44, 0x4f, 0x43, 0x48, 0x43, 0x54, 0x41, 0x57,
    0x50, 0x4f, 0x74, 0x54, 0x04, 0x00, 0x23, 0x7a
);
uint16_t g_voice_tx_value_handle = 0;

int gatt_access(
    uint16_t,
    uint16_t,
    ble_gatt_access_ctxt* context,
    void*
)
{
    if (g_transport == nullptr || context == nullptr ||
        context->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    const uint16_t length = OS_MBUF_PKTLEN(context->om);
    if (length == 0 || length > kMaxFrameSize) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    uint8_t bytes[kMaxFrameSize];
    uint16_t copied = 0;
    if (ble_hs_mbuf_to_flat(context->om, bytes, sizeof(bytes), &copied) != 0 || copied != length) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    return g_transport->enqueue_frame_from_ble(bytes, copied)
               ? 0
               : BLE_ATT_ERR_INSUFFICIENT_RES;
}

int voice_control_access(
    uint16_t,
    uint16_t,
    ble_gatt_access_ctxt* context,
    void*
)
{
    if (g_transport == nullptr || context == nullptr ||
        context->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    const uint16_t length = OS_MBUF_PKTLEN(context->om);
    if (length == 0 || length > ble_voice_probe::kControlCommandSize) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    uint8_t bytes[ble_voice_probe::kControlCommandSize] = {};
    uint16_t copied = 0;
    if (ble_hs_mbuf_to_flat(context->om, bytes, sizeof(bytes), &copied) != 0 || copied != length) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    return g_transport->enqueue_voice_control_from_ble(bytes, copied)
               ? 0
               : BLE_ATT_ERR_INSUFFICIENT_RES;
}

const ble_gatt_chr_def kCharacteristics[] = {
    {
        &kStatusRxUuid.u,
        gatt_access,
        nullptr,
        nullptr,
        BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE_ENC,
        0,
        nullptr,
        nullptr,
    },
    {
        &kVoiceControlUuid.u,
        voice_control_access,
        nullptr,
        nullptr,
        BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE_ENC,
        0,
        nullptr,
        nullptr,
    },
    {
        &kVoiceTxUuid.u,
        nullptr,
        nullptr,
        nullptr,
        BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_NOTIFY_INDICATE_ENC,
        0,
        &g_voice_tx_value_handle,
        nullptr,
    },
    {},
};

const ble_gatt_svc_def kServices[] = {
    {
        BLE_GATT_SVC_TYPE_PRIMARY,
        &kServiceUuid.u,
        nullptr,
        kCharacteristics,
    },
    {},
};

int start_advertising();

int gap_event(ble_gap_event* event, void*)
{
    if (g_transport == nullptr || event == nullptr) {
        return 0;
    }

    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                g_transport->handle_gap_connected(event->connect.conn_handle);
                const int security_result = ble_gap_security_initiate(event->connect.conn_handle);
                ESP_LOGI(kTag, "connected, security=%d", security_result);
            } else if (!g_transport->stopping()) {
                start_advertising();
            }
            return 0;
        case BLE_GAP_EVENT_DISCONNECT:
            g_transport->handle_gap_disconnected();
            if (!g_transport->stopping()) {
                start_advertising();
            }
            return 0;
        case BLE_GAP_EVENT_SUBSCRIBE:
            g_transport->handle_voice_subscription(
                event->subscribe.attr_handle,
                event->subscribe.cur_notify != 0);
            return 0;
        case BLE_GAP_EVENT_MTU:
            g_transport->handle_mtu_update(event->mtu.conn_handle, event->mtu.value);
            return 0;
        case BLE_GAP_EVENT_ADV_COMPLETE:
            if (!g_transport->stopping()) {
                start_advertising();
            }
            return 0;
        case BLE_GAP_EVENT_REPEAT_PAIRING: {
            ble_gap_conn_desc description = {};
            if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &description) == 0) {
                ble_store_util_delete_peer(&description.peer_id_addr);
            }
            return BLE_GAP_REPEAT_PAIRING_RETRY;
        }
        default:
            return 0;
    }
}

int start_advertising()
{
    ble_hs_adv_fields advertising = {};
    advertising.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    advertising.uuids128 = const_cast<ble_uuid128_t*>(&kServiceUuid);
    advertising.num_uuids128 = 1;
    advertising.uuids128_is_complete = 1;
    int result = ble_gap_adv_set_fields(&advertising);
    if (result != 0) {
        ESP_LOGE(kTag, "set advertising fields failed: %d", result);
        return result;
    }

    ble_hs_adv_fields response = {};
    response.name = reinterpret_cast<uint8_t*>(const_cast<char*>(g_device_name.c_str()));
    response.name_len = static_cast<uint8_t>(g_device_name.size());
    response.name_is_complete = 1;
    result = ble_gap_adv_rsp_set_fields(&response);
    if (result != 0) {
        ESP_LOGE(kTag, "set scan response failed: %d", result);
        return result;
    }

    ble_gap_adv_params parameters = {};
    parameters.conn_mode = BLE_GAP_CONN_MODE_UND;
    parameters.disc_mode = BLE_GAP_DISC_MODE_GEN;
    result = ble_gap_adv_start(
        g_own_address_type,
        nullptr,
        BLE_HS_FOREVER,
        &parameters,
        gap_event,
        nullptr
    );
    if (result != 0 && result != BLE_HS_EALREADY) {
        ESP_LOGE(kTag, "start advertising failed: %d", result);
    }
    return result;
}

void on_reset(int reason)
{
    ESP_LOGE(kTag, "NimBLE reset: %d", reason);
}

void on_sync()
{
    uint8_t identity_address[6] = {};
    if (ble_hs_id_copy_addr(BLE_ADDR_PUBLIC, identity_address, nullptr) != 0) {
        ESP_LOGE(kTag, "failed to read public BLE identity address");
        return;
    }

    identity_address[0] ^= 0xa5;
    identity_address[5] = static_cast<uint8_t>((identity_address[5] & 0x3f) | 0xc0);
    if (ble_hs_id_set_rnd(identity_address) != 0 ||
        ble_hs_util_ensure_addr(1) != 0 ||
        ble_hs_id_infer_auto(0, &g_own_address_type) != 0) {
        ESP_LOGE(kTag, "failed to select BLE identity address");
        return;
    }
    start_advertising();
}

void host_task(void*)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

}  // namespace


bool CodexBleTransport::start(const std::string& device_name)
{
    if (_initialized) {
        return true;
    }

    _frame_queue = xQueueCreate(kFrameQueueDepth, sizeof(QueuedFrame));
    if (_frame_queue == nullptr) {
        return false;
    }
    _voice_control_queue = xQueueCreate(kVoiceControlQueueDepth, sizeof(QueuedVoiceControl));
    if (_voice_control_queue == nullptr) {
        vQueueDelete(static_cast<QueueHandle_t>(_frame_queue));
        _frame_queue = nullptr;
        return false;
    }

    g_transport = this;
    g_device_name = device_name.empty() ? "StopWatch-Codex" : device_name;
    _stopping = false;
    _connected = false;
    _connection_handle = BLE_HS_CONN_HANDLE_NONE;
    _voice_subscribed = false;
    _negotiated_mtu = kDefaultMtu;
    g_voice_tx_value_handle = 0;

    const esp_err_t init_result = nimble_port_init();
    if (init_result != ESP_OK) {
        ESP_LOGE(kTag, "nimble_port_init failed: %s", esp_err_to_name(init_result));
        vQueueDelete(static_cast<QueueHandle_t>(_frame_queue));
        _frame_queue = nullptr;
        vQueueDelete(static_cast<QueueHandle_t>(_voice_control_queue));
        _voice_control_queue = nullptr;
        g_transport = nullptr;
        return false;
    }

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    g_voice_tx_value_handle = 0;
    int result = ble_gatts_count_cfg(kServices);
    if (result == 0) {
        result = ble_gatts_add_svcs(kServices);
    }
    if (result == 0) {
        result = ble_svc_gap_device_name_set(g_device_name.c_str());
    }
    if (result != 0) {
        ESP_LOGE(kTag, "GATT setup failed: %d", result);
        nimble_port_deinit();
        vQueueDelete(static_cast<QueueHandle_t>(_frame_queue));
        _frame_queue = nullptr;
        vQueueDelete(static_cast<QueueHandle_t>(_voice_control_queue));
        _voice_control_queue = nullptr;
        g_voice_tx_value_handle = 0;
        g_transport = nullptr;
        return false;
    }

    ble_store_config_init();
    nimble_port_freertos_init(host_task);
    _initialized = true;
    return true;
}


void CodexBleTransport::stop()
{
    if (_initialized) {
        _stopping = true;
        ble_gap_adv_stop();
        const uint16_t connection_handle = _connection_handle.load();
        if (connection_handle != BLE_HS_CONN_HANDLE_NONE) {
            ble_gap_terminate(connection_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
        const int stop_result = nimble_port_stop();
        if (stop_result == 0) {
            const esp_err_t deinit_result = nimble_port_deinit();
            if (deinit_result != ESP_OK) {
                ESP_LOGE(kTag, "nimble_port_deinit failed: %s", esp_err_to_name(deinit_result));
            }
        } else {
            ESP_LOGE(kTag, "nimble_port_stop failed: %d", stop_result);
        }
    }

    g_transport = nullptr;
    if (_frame_queue != nullptr) {
        vQueueDelete(static_cast<QueueHandle_t>(_frame_queue));
        _frame_queue = nullptr;
    }
    if (_voice_control_queue != nullptr) {
        vQueueDelete(static_cast<QueueHandle_t>(_voice_control_queue));
        _voice_control_queue = nullptr;
    }
    _messages.clear();
    _reassembler.reset();
    _connection_handle = BLE_HS_CONN_HANDLE_NONE;
    _connected = false;
    _voice_subscribed = false;
    _negotiated_mtu = kDefaultMtu;
    g_voice_tx_value_handle = 0;
    _initialized = false;
}


void CodexBleTransport::poll(uint32_t now_ms)
{
    if (_frame_queue == nullptr) {
        return;
    }
    QueuedFrame frame;
    while (xQueueReceive(static_cast<QueueHandle_t>(_frame_queue), &frame, 0) == pdTRUE) {
        auto message = _reassembler.push(frame.data, frame.length, now_ms);
        if (message.has_value()) {
            _messages.push_back(std::move(*message));
        }
    }
}


bool CodexBleTransport::connected() const
{
    return _connected.load();
}


bool CodexBleTransport::take_message(CodexBleMessage& message)
{
    if (_messages.empty()) {
        return false;
    }
    message = std::move(_messages.front());
    _messages.pop_front();
    return true;
}


bool CodexBleTransport::take_voice_control(CodexBleVoiceControl& control)
{
    if (_voice_control_queue == nullptr) {
        return false;
    }
    QueuedVoiceControl queued;
    if (xQueueReceive(static_cast<QueueHandle_t>(_voice_control_queue), &queued, 0) != pdTRUE) {
        return false;
    }
    control.length = queued.length;
    std::copy(queued.data, queued.data + ble_voice_probe::kControlCommandSize,
              control.data.begin());
    return true;
}


bool CodexBleTransport::voice_subscribed() const
{
    return _voice_subscribed.load();
}


uint16_t CodexBleTransport::negotiated_mtu() const
{
    return _negotiated_mtu.load();
}


size_t CodexBleTransport::voice_notification_limit() const
{
    const uint16_t negotiated_mtu = _negotiated_mtu.load();
    if (negotiated_mtu <= 3) {
        return 0;
    }
    return std::min(kMaxNotificationSize, static_cast<size_t>(negotiated_mtu - 3));
}


CodexBleNotifyResult CodexBleTransport::notify_voice(const uint8_t* data, size_t length)
{
    if (data == nullptr || length == 0 || length > voice_notification_limit()) {
        return CodexBleNotifyResult::InvalidSize;
    }
    if (!_connected.load() || !_voice_subscribed.load() || g_voice_tx_value_handle == 0) {
        return CodexBleNotifyResult::NotReady;
    }

    os_mbuf* mbuf = os_msys_get_pkthdr(static_cast<uint16_t>(length), 0);
    if (mbuf == nullptr) {
        return CodexBleNotifyResult::Congested;
    }
    if (os_mbuf_append(mbuf, data, static_cast<uint16_t>(length)) != 0) {
        os_mbuf_free_chain(mbuf);
        return CodexBleNotifyResult::Congested;
    }

    // NimBLE consumes mbuf for every notify_custom result.
    const int result = ble_gatts_notify_custom(
        _connection_handle.load(), g_voice_tx_value_handle, mbuf);
    if (result == 0) {
        return CodexBleNotifyResult::Accepted;
    }
    if (result == BLE_HS_ENOMEM || result == BLE_HS_EBUSY) {
        return CodexBleNotifyResult::Congested;
    }
    return CodexBleNotifyResult::NotReady;
}


bool CodexBleTransport::enqueue_frame_from_ble(const uint8_t* data, size_t length)
{
    if (_frame_queue == nullptr || data == nullptr || length == 0 || length > kMaxFrameSize) {
        return false;
    }
    QueuedFrame frame;
    frame.length = static_cast<uint16_t>(length);
    std::memcpy(frame.data, data, length);
    return xQueueSend(static_cast<QueueHandle_t>(_frame_queue), &frame, 0) == pdTRUE;
}


bool CodexBleTransport::enqueue_voice_control_from_ble(const uint8_t* data, size_t length)
{
    if (_voice_control_queue == nullptr || data == nullptr || length == 0 ||
        length > ble_voice_probe::kControlCommandSize) {
        return false;
    }
    QueuedVoiceControl control;
    control.length = static_cast<uint16_t>(length);
    std::memcpy(control.data, data, length);
    return xQueueSend(static_cast<QueueHandle_t>(_voice_control_queue), &control, 0) == pdTRUE;
}


void CodexBleTransport::handle_gap_connected(uint16_t connection_handle)
{
    _connection_handle = connection_handle;
    _connected = true;
    _voice_subscribed = false;
    _negotiated_mtu = kDefaultMtu;
}


void CodexBleTransport::handle_gap_disconnected()
{
    _connection_handle = BLE_HS_CONN_HANDLE_NONE;
    _connected = false;
    _voice_subscribed = false;
    _negotiated_mtu = kDefaultMtu;
    if (_voice_control_queue != nullptr) {
        xQueueReset(static_cast<QueueHandle_t>(_voice_control_queue));
    }
}


void CodexBleTransport::handle_voice_subscription(uint16_t attr_handle, bool enabled)
{
    if (attr_handle == g_voice_tx_value_handle) {
        _voice_subscribed = enabled;
    }
}


void CodexBleTransport::handle_mtu_update(uint16_t connection_handle, uint16_t mtu)
{
    if (_connected.load() && connection_handle == _connection_handle.load()) {
        _negotiated_mtu = mtu;
    }
}


bool CodexBleTransport::stopping() const
{
    return _stopping.load();
}
