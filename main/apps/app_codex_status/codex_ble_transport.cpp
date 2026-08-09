#include "codex_ble_transport.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <host/ble_hs.h>
#include <host/ble_hs_id.h>
#include <host/util/util.h>
#include <hal/utils/settings/settings.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <os/os_mbuf.h>
#include <services/gap/ble_svc_gap.h>
#include <services/gatt/ble_svc_gatt.h>
#include <store/config/ble_store_config.h>

#include <algorithm>
#include <array>
#include <cstring>

extern "C" void ble_store_config_init(void);


namespace {

constexpr const char* kTag = "CodexBLE";
constexpr size_t kMaxFrameSize = 256;
constexpr size_t kFrameQueueDepth = 32;
constexpr size_t kVoiceControlQueueDepth = 8;
constexpr size_t kProductionVoiceControlQueueDepth = 8;
constexpr size_t kPairingFrameQueueDepth = 8;
constexpr size_t kMaxNotificationSize = 256;
constexpr uint32_t kPendingAuthenticationIdleTimeoutMs = 60000;
constexpr uint32_t kPendingAuthenticationMaxLifetimeMs = 120000;
constexpr uint16_t kDefaultMtu = 23;
constexpr size_t kMaxVoiceControlSize =
    std::max(ble_voice_probe::kControlCommandSize, voice_input::kControlSize);

uint64_t active_connection_token(
    const secure_pairing::AgentActiveConnection& connection)
{
    if (connection.handle == secure_pairing::kNoAgentConnection) {
        return 0;
    }
    return (static_cast<uint64_t>(connection.generation) << 32U) |
           (static_cast<uint32_t>(connection.handle) + 1U);
}

uint64_t active_connection_token(
    const secure_pairing::AgentVoiceLink& connection)
{
    if (connection.handle == secure_pairing::kNoAgentConnection) {
        return 0;
    }
    return (static_cast<uint64_t>(connection.generation) << 32U) |
           (static_cast<uint32_t>(connection.handle) + 1U);
}

struct QueuedFrame {
    uint16_t connection_handle = secure_pairing::kNoAgentConnection;
    uint16_t length = 0;
    uint8_t data[kMaxFrameSize] = {};
};

struct QueuedVoiceControl {
    uint16_t length = 0;
    uint8_t data[ble_voice_probe::kControlCommandSize] = {};
};

struct QueuedProductionVoiceControl {
    voice_input::VoiceControl control = {};
};

struct QueuedPairingFrame {
    uint16_t connection_handle = secure_pairing::kNoAgentConnection;
    uint16_t length = 0;
    uint8_t data[kMaxFrameSize] = {};
};

CodexBleTransport* g_transport = nullptr;
uint8_t g_own_address_type = 0;
std::string g_device_name;

const ble_uuid128_t kServiceUuid = BLE_UUID128_INIT(
    0x44, 0x4f, 0x43, 0x48, 0x43, 0x54, 0x41, 0x57,
    0x50, 0x4f, 0x74, 0x54, 0x01, 0x00, 0x23, 0x7a
);
const ble_uuid128_t kVoiceServiceUuid = BLE_UUID128_INIT(
    0x44, 0x4f, 0x43, 0x48, 0x43, 0x54, 0x41, 0x57,
    0x50, 0x4f, 0x74, 0x54, 0x05, 0x00, 0x23, 0x7a
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
const ble_uuid128_t kPairingServiceUuid = BLE_UUID128_INIT(
    0x44, 0x4f, 0x43, 0x48, 0x43, 0x54, 0x41, 0x57,
    0x50, 0x4f, 0x74, 0x54, 0x06, 0x00, 0x23, 0x7a
);
const ble_uuid128_t kPairingControlUuid = BLE_UUID128_INIT(
    0x44, 0x4f, 0x43, 0x48, 0x43, 0x54, 0x41, 0x57,
    0x50, 0x4f, 0x74, 0x54, 0x07, 0x00, 0x23, 0x7a
);
const ble_uuid128_t kPairingTxUuid = BLE_UUID128_INIT(
    0x44, 0x4f, 0x43, 0x48, 0x43, 0x54, 0x41, 0x57,
    0x50, 0x4f, 0x74, 0x54, 0x08, 0x00, 0x23, 0x7a
);
uint16_t g_voice_tx_value_handle = 0;
uint16_t g_pairing_tx_value_handle = 0;

bool configure_identity_address()
{
    Settings settings("codex_ble", true);
    std::array<uint8_t, 6> address = {};
    size_t length = address.size();
    if (settings.GetBlob("identity", address.data(), &length) != ESP_OK ||
        length != address.size()) {
        ble_addr_t generated = {};
        const int generate_result = ble_hs_id_gen_rnd(0, &generated);
        if (generate_result != 0) {
            ESP_LOGE(kTag, "failed to generate BLE identity: %d", generate_result);
            return false;
        }
        std::copy(std::begin(generated.val), std::end(generated.val), address.begin());
        if (settings.SetBlob("identity", address.data(), address.size()) != ESP_OK) {
            ESP_LOGE(kTag, "failed to persist BLE identity");
            return false;
        }
    }

    const int set_result = ble_hs_id_set_rnd(address.data());
    if (set_result != 0) {
        ESP_LOGE(kTag, "failed to set BLE identity: %d", set_result);
        return false;
    }
    return true;
}

int gatt_access(
    uint16_t connection_handle,
    uint16_t,
    ble_gatt_access_ctxt* context,
    void*
)
{
    if (g_transport == nullptr || context == nullptr ||
        context->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    if (!g_transport->authorize_gatt(
            connection_handle,
            secure_pairing::AgentGattSurface::Production)) {
        return BLE_ATT_ERR_INSUFFICIENT_AUTHOR;
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
    return g_transport->enqueue_frame_from_ble(
               connection_handle, bytes, copied)
               ? 0
               : BLE_ATT_ERR_INSUFFICIENT_RES;
}

int voice_control_access(
    uint16_t connection_handle,
    uint16_t,
    ble_gatt_access_ctxt* context,
    void*
)
{
    if (g_transport == nullptr || context == nullptr ||
        context->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    if (!g_transport->authorize_gatt(
            connection_handle,
            secure_pairing::AgentGattSurface::Production)) {
        return BLE_ATT_ERR_INSUFFICIENT_AUTHOR;
    }

    const uint16_t length = OS_MBUF_PKTLEN(context->om);
    if (length == 0 || length > kMaxVoiceControlSize) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    uint8_t bytes[kMaxVoiceControlSize] = {};
    uint16_t copied = 0;
    if (ble_hs_mbuf_to_flat(context->om, bytes, sizeof(bytes), &copied) != 0 || copied != length) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    return g_transport->enqueue_voice_control_from_ble(
               connection_handle, bytes, copied)
               ? 0
               : BLE_ATT_ERR_INSUFFICIENT_RES;
}

int voice_tx_access(
    uint16_t connection_handle,
    uint16_t,
    ble_gatt_access_ctxt*,
    void*
)
{
    if (g_transport == nullptr ||
        !g_transport->authorize_gatt(
            connection_handle,
            secure_pairing::AgentGattSurface::Production)) {
        return BLE_ATT_ERR_INSUFFICIENT_AUTHOR;
    }
    return BLE_ATT_ERR_REQ_NOT_SUPPORTED;
}

int pairing_control_access(
    uint16_t connection_handle,
    uint16_t,
    ble_gatt_access_ctxt* context,
    void*)
{
    if (g_transport == nullptr || context == nullptr ||
        context->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    if (!g_transport->authorize_gatt(
            connection_handle,
            secure_pairing::AgentGattSurface::Pairing)) {
        return BLE_ATT_ERR_INSUFFICIENT_AUTHOR;
    }
    const uint16_t length = OS_MBUF_PKTLEN(context->om);
    if (length == 0 || length > kMaxFrameSize) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    uint8_t bytes[kMaxFrameSize] = {};
    uint16_t copied = 0;
    if (ble_hs_mbuf_to_flat(
            context->om, bytes, sizeof(bytes), &copied) != 0 ||
        copied != length) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    return g_transport->enqueue_pairing_frame_from_ble(
               connection_handle, bytes, copied)
               ? 0
               : BLE_ATT_ERR_INSUFFICIENT_RES;
}

int pairing_tx_access(
    uint16_t connection_handle,
    uint16_t,
    ble_gatt_access_ctxt*,
    void*)
{
    if (g_transport == nullptr ||
        !g_transport->authorize_gatt(
            connection_handle,
            secure_pairing::AgentGattSurface::Pairing)) {
        return BLE_ATT_ERR_INSUFFICIENT_AUTHOR;
    }
    return BLE_ATT_ERR_REQ_NOT_SUPPORTED;
}

const ble_gatt_chr_def kStatusCharacteristics[] = {
    {
        &kStatusRxUuid.u,
        gatt_access,
        nullptr,
        nullptr,
        BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
        0,
        nullptr,
        nullptr,
    },
    {},
};

const ble_gatt_chr_def kVoiceCharacteristics[] = {
    {
        &kVoiceControlUuid.u,
        voice_control_access,
        nullptr,
        nullptr,
        BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
        0,
        nullptr,
        nullptr,
    },
    {
        &kVoiceTxUuid.u,
        voice_tx_access,
        nullptr,
        nullptr,
        BLE_GATT_CHR_F_NOTIFY,
        0,
        &g_voice_tx_value_handle,
        nullptr,
    },
    {},
};

const ble_gatt_chr_def kPairingCharacteristics[] = {
    {
        &kPairingControlUuid.u,
        pairing_control_access,
        nullptr,
        nullptr,
        BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
        0,
        nullptr,
        nullptr,
    },
    {
        &kPairingTxUuid.u,
        pairing_tx_access,
        nullptr,
        nullptr,
        BLE_GATT_CHR_F_NOTIFY,
        0,
        &g_pairing_tx_value_handle,
        nullptr,
    },
    {},
};

const ble_gatt_svc_def kServices[] = {
    {
        BLE_GATT_SVC_TYPE_PRIMARY,
        &kServiceUuid.u,
        nullptr,
        kStatusCharacteristics,
    },
    {
        BLE_GATT_SVC_TYPE_PRIMARY,
        &kVoiceServiceUuid.u,
        nullptr,
        kVoiceCharacteristics,
    },
    {
        BLE_GATT_SVC_TYPE_PRIMARY,
        &kPairingServiceUuid.u,
        nullptr,
        kPairingCharacteristics,
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
                if (!g_transport->handle_gap_connected(
                        event->connect.conn_handle)) {
                    ble_gap_terminate(
                        event->connect.conn_handle,
                        BLE_ERR_REM_USER_CONN_TERM);
                } else {
                    ESP_LOGI(kTag, "connected pending Agent");
                    if (!g_transport->stopping()) {
                        start_advertising();
                    }
                }
            } else if (!g_transport->stopping()) {
                start_advertising();
            }
            return 0;
        case BLE_GAP_EVENT_DISCONNECT:
            g_transport->handle_gap_disconnected(
                event->disconnect.conn.conn_handle);
            if (!g_transport->stopping()) {
                start_advertising();
            }
            return 0;
        case BLE_GAP_EVENT_SUBSCRIBE:
            g_transport->handle_subscription(
                event->subscribe.conn_handle,
                event->subscribe.attr_handle,
                event->subscribe.cur_notify != 0);
            return 0;
        case BLE_GAP_EVENT_MTU:
            g_transport->handle_mtu_update(event->mtu.conn_handle, event->mtu.value);
            return 0;
        case BLE_GAP_EVENT_ENC_CHANGE:
            ESP_LOGI(
                kTag,
                "encryption change, conn=%u status=%d",
                event->enc_change.conn_handle,
                event->enc_change.status);
            return 0;
        case BLE_GAP_EVENT_PASSKEY_ACTION:
            ESP_LOGI(
                kTag,
                "passkey action, conn=%u action=%u",
                event->passkey.conn_handle,
                event->passkey.params.action);
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
    advertising.uuids128 = const_cast<ble_uuid128_t*>(&kVoiceServiceUuid);
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
    if (!configure_identity_address() ||
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
    _production_voice_control_queue = xQueueCreate(
        kProductionVoiceControlQueueDepth,
        sizeof(QueuedProductionVoiceControl));
    if (_production_voice_control_queue == nullptr) {
        vQueueDelete(static_cast<QueueHandle_t>(_frame_queue));
        _frame_queue = nullptr;
        vQueueDelete(static_cast<QueueHandle_t>(_voice_control_queue));
        _voice_control_queue = nullptr;
        return false;
    }
    _pairing_frame_queue = xQueueCreate(
        kPairingFrameQueueDepth, sizeof(QueuedPairingFrame));
    if (_pairing_frame_queue == nullptr) {
        vQueueDelete(static_cast<QueueHandle_t>(_frame_queue));
        _frame_queue = nullptr;
        vQueueDelete(static_cast<QueueHandle_t>(_voice_control_queue));
        _voice_control_queue = nullptr;
        vQueueDelete(
            static_cast<QueueHandle_t>(
                _production_voice_control_queue));
        _production_voice_control_queue = nullptr;
        return false;
    }

    g_transport = this;
    g_device_name = device_name.empty() ? "StopWatch-Codex" : device_name;
    _stopping = false;
    _connection_router.clear();
    _active_disconnect_token.store(0);
    _active_production_revoked_token.store(0);
    {
        std::lock_guard<std::mutex> lock(_rejected_disconnect_mutex);
        _rejected_disconnect_handle =
            secure_pairing::kNoAgentConnection;
    }
    _production_control_guard.reset();
    g_voice_tx_value_handle = 0;
    g_pairing_tx_value_handle = 0;

    const esp_err_t init_result = nimble_port_init();
    if (init_result != ESP_OK) {
        ESP_LOGE(kTag, "nimble_port_init failed: %s", esp_err_to_name(init_result));
        vQueueDelete(static_cast<QueueHandle_t>(_frame_queue));
        _frame_queue = nullptr;
        vQueueDelete(static_cast<QueueHandle_t>(_voice_control_queue));
        _voice_control_queue = nullptr;
        vQueueDelete(static_cast<QueueHandle_t>(_production_voice_control_queue));
        _production_voice_control_queue = nullptr;
        vQueueDelete(static_cast<QueueHandle_t>(_pairing_frame_queue));
        _pairing_frame_queue = nullptr;
        g_transport = nullptr;
        return false;
    }

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding = 0;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = 0;
    ble_hs_cfg.sm_their_key_dist = 0;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    g_voice_tx_value_handle = 0;
    g_pairing_tx_value_handle = 0;
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
        vQueueDelete(static_cast<QueueHandle_t>(_production_voice_control_queue));
        _production_voice_control_queue = nullptr;
        vQueueDelete(static_cast<QueueHandle_t>(_pairing_frame_queue));
        _pairing_frame_queue = nullptr;
        g_voice_tx_value_handle = 0;
        g_pairing_tx_value_handle = 0;
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
        const uint16_t active_handle = _connection_router.active_handle();
        const uint16_t pending_handle_value =
            _connection_router.pending_handle();
        const uint16_t disconnecting_handle =
            _connection_router.disconnecting_handle();
        uint16_t rejected_handle = secure_pairing::kNoAgentConnection;
        {
            std::lock_guard<std::mutex> lock(
                _rejected_disconnect_mutex);
            rejected_handle = _rejected_disconnect_handle;
        }
        if (active_handle != secure_pairing::kNoAgentConnection) {
            ble_gap_terminate(active_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
        if (pending_handle_value != secure_pairing::kNoAgentConnection) {
            ble_gap_terminate(
                pending_handle_value, BLE_ERR_REM_USER_CONN_TERM);
        }
        if (disconnecting_handle != secure_pairing::kNoAgentConnection) {
            ble_gap_terminate(
                disconnecting_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
        if (rejected_handle != secure_pairing::kNoAgentConnection) {
            ble_gap_terminate(
                rejected_handle, BLE_ERR_REM_USER_CONN_TERM);
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
    if (_production_voice_control_queue != nullptr) {
        vQueueDelete(static_cast<QueueHandle_t>(_production_voice_control_queue));
        _production_voice_control_queue = nullptr;
    }
    if (_pairing_frame_queue != nullptr) {
        vQueueDelete(static_cast<QueueHandle_t>(_pairing_frame_queue));
        _pairing_frame_queue = nullptr;
    }
    _messages.clear();
    _reassembler.reset();
    _connection_router.clear();
    _active_disconnect_token.store(0);
    _active_production_revoked_token.store(0);
    {
        std::lock_guard<std::mutex> lock(_rejected_disconnect_mutex);
        _rejected_disconnect_handle =
            secure_pairing::kNoAgentConnection;
    }
    _production_control_guard.reset();
    g_voice_tx_value_handle = 0;
    g_pairing_tx_value_handle = 0;
    _initialized = false;
}


void CodexBleTransport::poll(uint32_t now_ms)
{
    const uint64_t disconnect_token = _active_disconnect_token.load();
    if (disconnect_token != 0) {
        const auto active = _connection_router.active_connection();
        if (active_connection_token(active) != disconnect_token) {
            uint64_t expected = disconnect_token;
            (void)_active_disconnect_token.compare_exchange_strong(
                expected, 0);
            expected = disconnect_token;
            (void)_active_production_revoked_token.compare_exchange_strong(
                expected, 0);
        } else if (_initialized) {
            (void)ble_gap_terminate(
                active.handle, BLE_ERR_REM_USER_CONN_TERM);
        }
    }
    const uint16_t expired_pending =
        _connection_router.expire_unauthorized_pending(
            now_ms,
            kPendingAuthenticationIdleTimeoutMs,
            kPendingAuthenticationMaxLifetimeMs);
    if (expired_pending != secure_pairing::kNoAgentConnection) {
        if (_pairing_frame_queue != nullptr) {
            xQueueReset(static_cast<QueueHandle_t>(_pairing_frame_queue));
        }
    }
    const uint16_t disconnecting =
        _connection_router.disconnecting_handle();
    if (disconnecting != secure_pairing::kNoAgentConnection &&
        _initialized) {
        (void)ble_gap_terminate(
            disconnecting, BLE_ERR_REM_USER_CONN_TERM);
    }
    {
        std::lock_guard<std::mutex> lock(_rejected_disconnect_mutex);
        if (_rejected_disconnect_handle !=
                secure_pairing::kNoAgentConnection &&
            _initialized) {
            (void)ble_gap_terminate(
                _rejected_disconnect_handle,
                BLE_ERR_REM_USER_CONN_TERM);
        }
    }
    if (_frame_queue == nullptr) {
        return;
    }
    QueuedFrame frame;
    while (xQueueReceive(static_cast<QueueHandle_t>(_frame_queue), &frame, 0) == pdTRUE) {
        if (!authorize_gatt(
                frame.connection_handle,
                secure_pairing::AgentGattSurface::Production)) {
            continue;
        }
        auto message = _reassembler.push(frame.data, frame.length, now_ms);
        if (message.has_value()) {
            _messages.push_back(std::move(*message));
        }
    }
}


bool CodexBleTransport::connected() const
{
    return _connection_router.active_handle() !=
           secure_pairing::kNoAgentConnection;
}


uint32_t CodexBleTransport::connection_epoch() const
{
    return _connection_router.active_generation();
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


bool CodexBleTransport::take_production_voice_control(
    CodexProductionVoiceControl& control)
{
    if (_production_voice_control_queue == nullptr) {
        return false;
    }
    QueuedProductionVoiceControl queued = {};
    if (xQueueReceive(
            static_cast<QueueHandle_t>(_production_voice_control_queue),
            &queued,
            0) != pdTRUE) {
        return false;
    }
    control.control = queued.control;
    return true;
}

bool CodexBleTransport::take_pairing_frame(CodexPairingFrame& frame)
{
    if (_pairing_frame_queue == nullptr) {
        return false;
    }
    QueuedPairingFrame queued = {};
    while (xQueueReceive(
               static_cast<QueueHandle_t>(_pairing_frame_queue),
               &queued,
               0) == pdTRUE) {
        if (!authorize_gatt(
                queued.connection_handle,
                secure_pairing::AgentGattSurface::Pairing)) {
            continue;
        }
        frame.connection_handle = queued.connection_handle;
        frame.length = queued.length;
        std::copy_n(queued.data, queued.length, frame.data.begin());
        return true;
    }
    return false;
}


bool CodexBleTransport::voice_subscribed() const
{
    return _connection_router.active_voice_subscribed();
}


uint16_t CodexBleTransport::negotiated_mtu() const
{
    return _connection_router.active_mtu();
}


size_t CodexBleTransport::voice_notification_limit() const
{
    const uint16_t negotiated_mtu = _connection_router.active_mtu();
    if (negotiated_mtu <= 3) {
        return 0;
    }
    return std::min(kMaxNotificationSize, static_cast<size_t>(negotiated_mtu - 3));
}


CodexBleNotifyResult CodexBleTransport::notify_voice(const uint8_t* data, size_t length)
{
    std::lock_guard<std::mutex> voice_link_lock(_voice_link_mutex);
    const secure_pairing::AgentVoiceLink link =
        _connection_router.active_voice_link();
    const bool revoked =
        _active_production_revoked_token.load() ==
        active_connection_token(link);
    if (revoked || !link.authorized || !link.subscribed || link.mtu <= 3 ||
        data == nullptr || length == 0 ||
        length > std::min(
            kMaxNotificationSize,
            static_cast<size_t>(link.mtu - 3))) {
        return CodexBleNotifyResult::InvalidSize;
    }
    return notify_voice_for_link(link, data, length);
}

VoiceNotificationSendResult CodexBleTransport::send_voice_notification(
    const voice_input::Notification& notification)
{
    std::lock_guard<std::mutex> voice_link_lock(_voice_link_mutex);
    const secure_pairing::AgentVoiceLink link =
        _connection_router.active_voice_link();
    const bool revoked =
        _active_production_revoked_token.load() ==
        active_connection_token(link);
    if (revoked || link.generation != notification.connection_generation ||
        !link.authorized || !link.subscribed || notification.size == 0 ||
        link.mtu <= 3 ||
        notification.size > static_cast<size_t>(link.mtu - 3)) {
        return VoiceNotificationSendResult::PermanentFailure;
    }
    const CodexBleNotifyResult result = notify_voice_for_link(
        link, notification.bytes.data(), notification.size);
    if (result == CodexBleNotifyResult::Accepted) {
        return VoiceNotificationSendResult::Accepted;
    }
    if (result == CodexBleNotifyResult::Congested) {
        return VoiceNotificationSendResult::Congested;
    }
    return VoiceNotificationSendResult::PermanentFailure;
}

CodexBleNotifyResult CodexBleTransport::notify_voice_for_link(
    const secure_pairing::AgentVoiceLink& link,
    const uint8_t* data,
    size_t length)
{
    if (link.handle == secure_pairing::kNoAgentConnection ||
        g_voice_tx_value_handle == 0) {
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
        link.handle, g_voice_tx_value_handle, mbuf);
    if (result == 0) {
        return CodexBleNotifyResult::Accepted;
    }
    if (result == BLE_HS_ENOMEM || result == BLE_HS_EBUSY) {
        return CodexBleNotifyResult::Congested;
    }
    return CodexBleNotifyResult::NotReady;
}

CodexBleNotifyResult CodexBleTransport::notify_pairing(
    uint16_t connection_handle,
    const uint8_t* data,
    size_t length)
{
    const uint16_t mtu = _connection_router.mtu(connection_handle);
    const size_t limit = mtu <= 3
                             ? 0
                             : std::min(
                                   kMaxNotificationSize,
                                   static_cast<size_t>(mtu - 3));
    if (data == nullptr || length == 0 || length > limit) {
        return CodexBleNotifyResult::InvalidSize;
    }
    if (!authorize_gatt(
            connection_handle,
            secure_pairing::AgentGattSurface::Pairing) ||
        !_connection_router.pairing_subscribed(connection_handle) ||
        g_pairing_tx_value_handle == 0) {
        return CodexBleNotifyResult::NotReady;
    }
    os_mbuf* mbuf =
        os_msys_get_pkthdr(static_cast<uint16_t>(length), 0);
    if (mbuf == nullptr) {
        return CodexBleNotifyResult::Congested;
    }
    if (os_mbuf_append(
            mbuf, data, static_cast<uint16_t>(length)) != 0) {
        os_mbuf_free_chain(mbuf);
        return CodexBleNotifyResult::Congested;
    }
    const int result = ble_gatts_notify_custom(
        connection_handle, g_pairing_tx_value_handle, mbuf);
    if (result == 0) {
        return CodexBleNotifyResult::Accepted;
    }
    if (result == BLE_HS_ENOMEM || result == BLE_HS_EBUSY) {
        return CodexBleNotifyResult::Congested;
    }
    return CodexBleNotifyResult::NotReady;
}

bool CodexBleTransport::set_voice_credential(
    const voice_input::crypto::Key& key,
    uint32_t key_epoch)
{
    if (key_epoch == 0 || !_production_voice_crypto.set_key(key)) {
        return false;
    }
    _production_voice_key_epoch = key_epoch;
    _production_control_guard.reset();
    return true;
}

void CodexBleTransport::clear_voice_credential()
{
    _production_voice_key_epoch = 0;
    _production_voice_crypto.clear();
    _production_control_guard.reset();
}


bool CodexBleTransport::authorize_gatt(
    uint16_t connection_handle,
    secure_pairing::AgentGattSurface surface) const
{
    std::lock_guard<std::mutex> voice_link_lock(_voice_link_mutex);
    const uint64_t revoked_token =
        _active_production_revoked_token.load();
    if (surface == secure_pairing::AgentGattSurface::Production &&
        revoked_token != 0 &&
        revoked_token == active_connection_token(
            _connection_router.active_connection())) {
        return false;
    }
    return _connection_router.can_access(connection_handle, surface);
}

bool CodexBleTransport::authorize_pending(
    uint16_t connection_handle,
    const secure_pairing::AgentFingerprint& fingerprint)
{
    return _connection_router.authorize_pending(
        connection_handle, fingerprint);
}

bool CodexBleTransport::note_pairing_activity(
    uint16_t connection_handle,
    uint32_t now_ms)
{
    return _connection_router.note_pending_activity(
        connection_handle, now_ms);
}

bool CodexBleTransport::promote_pending(uint16_t connection_handle)
{
    std::lock_guard<std::mutex> voice_link_lock(_voice_link_mutex);
    if (!_connection_router.promote_pending(connection_handle)) {
        return false;
    }
    _production_control_guard.reset();
    if (_voice_control_queue != nullptr) {
        xQueueReset(static_cast<QueueHandle_t>(_voice_control_queue));
    }
    if (_production_voice_control_queue != nullptr) {
        xQueueReset(
            static_cast<QueueHandle_t>(
                _production_voice_control_queue));
    }
    _reassembler.reset();
    _messages.clear();
    return true;
}

bool CodexBleTransport::reject_pending(uint16_t connection_handle)
{
    if (!_connection_router.reject_pending(connection_handle)) {
        return false;
    }
    if (_pairing_frame_queue != nullptr) {
        xQueueReset(static_cast<QueueHandle_t>(_pairing_frame_queue));
    }
    if (_initialized) {
        ble_gap_terminate(
            connection_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
    return true;
}

bool CodexBleTransport::request_active_disconnect(bool revoke_production)
{
    secure_pairing::AgentActiveConnection active = {};
    {
        std::lock_guard<std::mutex> voice_link_lock(_voice_link_mutex);
        active = _connection_router.active_connection();
        if (!_initialized ||
            active.handle == secure_pairing::kNoAgentConnection) {
            return false;
        }
        const uint64_t token = active_connection_token(active);
        if (revoke_production) {
            _active_production_revoked_token.store(token);
        }
        _active_disconnect_token.store(token);
    }
    return ble_gap_terminate(
               active.handle, BLE_ERR_REM_USER_CONN_TERM) == 0;
}

bool CodexBleTransport::is_active_handle(
    uint16_t connection_handle) const
{
    return connection_handle != secure_pairing::kNoAgentConnection &&
           _connection_router.active_handle() == connection_handle;
}

uint16_t CodexBleTransport::pending_handle() const
{
    return _connection_router.pending_handle();
}

secure_pairing::AgentFingerprint
CodexBleTransport::active_fingerprint() const
{
    return _connection_router.active_fingerprint();
}

secure_pairing::AgentFingerprint
CodexBleTransport::pending_fingerprint() const
{
    const uint16_t handle = _connection_router.pending_handle();
    return _connection_router.fingerprint(handle);
}

bool CodexBleTransport::enqueue_frame_from_ble(
    uint16_t connection_handle,
    const uint8_t* data,
    size_t length)
{
    if (!authorize_gatt(
            connection_handle,
            secure_pairing::AgentGattSurface::Production) ||
        _frame_queue == nullptr || data == nullptr || length == 0 ||
        length > kMaxFrameSize) {
        return false;
    }
    QueuedFrame frame = {};
    frame.connection_handle = connection_handle;
    frame.length = static_cast<uint16_t>(length);
    std::memcpy(frame.data, data, length);
    return xQueueSend(static_cast<QueueHandle_t>(_frame_queue), &frame, 0) == pdTRUE;
}


bool CodexBleTransport::enqueue_voice_control_from_ble(
    uint16_t connection_handle,
    const uint8_t* data,
    size_t length)
{
    if (!authorize_gatt(
            connection_handle,
            secure_pairing::AgentGattSurface::Production) ||
        data == nullptr || length == 0) {
        return false;
    }
    if (data[0] == voice_input::kProtocolVersion) {
        if (_production_voice_control_queue == nullptr ||
            length != voice_input::kControlSize) {
            return false;
        }
        QueuedProductionVoiceControl production = {};
        const uint32_t expected_key_epoch =
            _production_voice_key_epoch.load();
        if (!_production_control_guard.decode_once(
                data,
                length,
                _production_voice_crypto,
                expected_key_epoch,
                production.control)) {
            return false;
        }
        return xQueueSend(
                   static_cast<QueueHandle_t>(_production_voice_control_queue),
                   &production,
                   0) == pdTRUE;
    }
    if (_voice_control_queue == nullptr ||
        length > ble_voice_probe::kControlCommandSize ||
        data[0] != ble_voice_probe::kProtocolVersion) {
        return false;
    }
    QueuedVoiceControl control;
    control.length = static_cast<uint16_t>(length);
    std::memcpy(control.data, data, length);
    return xQueueSend(static_cast<QueueHandle_t>(_voice_control_queue), &control, 0) == pdTRUE;
}

bool CodexBleTransport::enqueue_pairing_frame_from_ble(
    uint16_t connection_handle,
    const uint8_t* data,
    size_t length)
{
    if (!authorize_gatt(
            connection_handle,
            secure_pairing::AgentGattSurface::Pairing) ||
        _pairing_frame_queue == nullptr || data == nullptr ||
        length == 0 || length > kMaxFrameSize) {
        return false;
    }
    QueuedPairingFrame frame = {};
    frame.connection_handle = connection_handle;
    frame.length = static_cast<uint16_t>(length);
    std::memcpy(frame.data, data, length);
    return xQueueSend(
               static_cast<QueueHandle_t>(_pairing_frame_queue),
               &frame,
               0) == pdTRUE;
}

bool CodexBleTransport::handle_gap_connected(uint16_t connection_handle)
{
    const uint32_t now_ms = static_cast<uint32_t>(
        static_cast<uint64_t>(esp_timer_get_time()) / 1000U);
    secure_pairing::AgentConnectionAdmission admission = {};
    {
        std::lock_guard<std::mutex> voice_link_lock(_voice_link_mutex);
        admission = _connection_router.connected(connection_handle, now_ms);
    }
    if (admission ==
        secure_pairing::AgentConnectionAdmission::RejectedFull) {
        std::lock_guard<std::mutex> lock(_rejected_disconnect_mutex);
        _rejected_disconnect_handle = connection_handle;
        return false;
    }
    return true;
}


void CodexBleTransport::handle_gap_disconnected(
    uint16_t connection_handle)
{
    {
        std::lock_guard<std::mutex> lock(_rejected_disconnect_mutex);
        if (_rejected_disconnect_handle == connection_handle) {
            _rejected_disconnect_handle =
                secure_pairing::kNoAgentConnection;
        }
    }
    std::lock_guard<std::mutex> voice_link_lock(_voice_link_mutex);
    const secure_pairing::AgentConnectionRole previous =
        _connection_router.disconnected(connection_handle);
    if (previous == secure_pairing::AgentConnectionRole::Active) {
        const uint64_t disconnected_handle_token =
            static_cast<uint32_t>(connection_handle) + 1U;
        const uint64_t disconnect_token =
            _active_disconnect_token.load();
        if ((disconnect_token & 0xffffffffULL) ==
            disconnected_handle_token) {
            uint64_t expected = disconnect_token;
            (void)_active_disconnect_token.compare_exchange_strong(
                expected, 0);
        }
        const uint64_t revoked_token =
            _active_production_revoked_token.load();
        if ((revoked_token & 0xffffffffULL) ==
            disconnected_handle_token) {
            uint64_t expected = revoked_token;
            (void)_active_production_revoked_token.compare_exchange_strong(
                expected, 0);
        }
        _production_control_guard.reset();
        _reassembler.reset();
        _messages.clear();
        if (_frame_queue != nullptr) {
            xQueueReset(static_cast<QueueHandle_t>(_frame_queue));
        }
        if (_voice_control_queue != nullptr) {
            xQueueReset(static_cast<QueueHandle_t>(_voice_control_queue));
        }
        if (_production_voice_control_queue != nullptr) {
            xQueueReset(
                static_cast<QueueHandle_t>(
                    _production_voice_control_queue));
        }
    } else if ((
                   previous == secure_pairing::AgentConnectionRole::Pending ||
                   previous ==
                       secure_pairing::AgentConnectionRole::Disconnecting) &&
        _pairing_frame_queue != nullptr) {
        xQueueReset(static_cast<QueueHandle_t>(_pairing_frame_queue));
    }
}


void CodexBleTransport::handle_subscription(
    uint16_t connection_handle,
    uint16_t attr_handle,
    bool enabled)
{
    std::lock_guard<std::mutex> voice_link_lock(_voice_link_mutex);
    if (attr_handle == g_voice_tx_value_handle) {
        (void)_connection_router.update_voice_subscription(
            connection_handle, enabled);
    } else if (attr_handle == g_pairing_tx_value_handle) {
        (void)_connection_router.update_pairing_subscription(
            connection_handle, enabled);
    }
}


void CodexBleTransport::handle_mtu_update(uint16_t connection_handle, uint16_t mtu)
{
    std::lock_guard<std::mutex> voice_link_lock(_voice_link_mutex);
    (void)_connection_router.update_mtu(connection_handle, mtu);
}


bool CodexBleTransport::stopping() const
{
    return _stopping.load();
}
