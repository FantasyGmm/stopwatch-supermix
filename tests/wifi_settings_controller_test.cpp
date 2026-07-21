#include "apps/app_setup/workers/wifi_settings_controller.h"

#include <cassert>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

class FakeWifiSettingsBackend final : public WifiSettingsBackend {
public:
    std::vector<std::string> saved_ssids;
    std::vector<std::string> operations;
    std::vector<std::string>* operation_sink = nullptr;
    bool sta_held = false;
    bool config_ap_held = false;
    bool config_ap_mode = false;
    bool allow_sta = true;
    WifiSettingsConfigApResult config_ap_result = WifiSettingsConfigApResult::Acquired;
    bool connected = false;
    std::string current_ssid;
    std::string current_ip_address;
    std::string current_ap_ssid;
    std::string current_ap_url;
    int current_rssi = 0;

    std::vector<std::string> copySavedSsids() const override
    {
        return saved_ssids;
    }

    bool selectSavedSsid(std::size_t index) override
    {
        if (index >= saved_ssids.size()) {
            return false;
        }
        record("select:" + std::to_string(index));
        current_ssid = saved_ssids[index];
        return true;
    }

    bool acquireSta() override
    {
        record("acquire-sta");
        sta_held = allow_sta;
        return sta_held;
    }

    void releaseSta() override
    {
        record("release-sta");
        sta_held = false;
    }

    bool staHeld() const override
    {
        return sta_held;
    }

    bool reconnectSta() override
    {
        record("reconnect-sta");
        return sta_held;
    }

    WifiSettingsConfigApResult acquireConfigAp() override
    {
        record("acquire-config-ap");
        config_ap_held = config_ap_result == WifiSettingsConfigApResult::Acquired;
        config_ap_mode = config_ap_held;
        return config_ap_result;
    }

    void releaseConfigAp() override
    {
        record("release-config-ap");
        config_ap_held = false;
        config_ap_mode = false;
    }

    bool configApHeld() const override
    {
        return config_ap_held;
    }

    bool configApMode() const override
    {
        return config_ap_mode;
    }

    std::string ssid() const override
    {
        return current_ssid;
    }

    bool isConnected() const override
    {
        return connected;
    }

    std::string ipAddress() const override
    {
        return current_ip_address;
    }

    std::string apSsid() const override
    {
        return current_ap_ssid;
    }

    std::string apUrl() const override
    {
        return current_ap_url;
    }

    int rssi() const override
    {
        return current_rssi;
    }

private:
    void record(std::string operation)
    {
        operations.push_back(operation);
        if (operation_sink != nullptr) {
            operation_sink->push_back(std::move(operation));
        }
    }
};

std::unique_ptr<FakeWifiSettingsBackend> makeBackend(FakeWifiSettingsBackend*& backend)
{
    auto owned = std::make_unique<FakeWifiSettingsBackend>();
    backend = owned.get();
    return owned;
}

void assertOperations(const FakeWifiSettingsBackend& backend, std::vector<std::string> expected)
{
    assert(backend.operations == expected);
}

void testNoSavedStartup()
{
    FakeWifiSettingsBackend* backend = nullptr;
    WifiSettingsController controller(makeBackend(backend));

    assert(controller.poll().state == WifiSettingsState::NoSaved);
    assertOperations(*backend, {});
}

void testSavedStartup()
{
    FakeWifiSettingsBackend* backend = nullptr;
    auto owned = makeBackend(backend);
    backend->saved_ssids = {"Office"};
    WifiSettingsController controller(std::move(owned));

    assert(controller.poll().state == WifiSettingsState::NotConnected);
    assertOperations(*backend, {"acquire-sta"});
}

void testStartPairingOrder()
{
    FakeWifiSettingsBackend* backend = nullptr;
    auto owned = makeBackend(backend);
    backend->saved_ssids = {"Office"};
    WifiSettingsController controller(std::move(owned));
    backend->operations.clear();

    assert(controller.startPairing());
    assert(controller.poll().state == WifiSettingsState::Pairing);
    assertOperations(*backend, {"release-sta", "acquire-config-ap"});
}

void testStartPairingFailureRestoresSta()
{
    FakeWifiSettingsBackend* backend = nullptr;
    auto owned = makeBackend(backend);
    backend->saved_ssids = {"Office"};
    backend->config_ap_result = WifiSettingsConfigApResult::StartFailed;
    WifiSettingsController controller(std::move(owned));
    backend->operations.clear();

    assert(!controller.startPairing());
    assert(controller.poll().state == WifiSettingsState::PairingFailed);
    assertOperations(*backend, {"release-sta", "acquire-config-ap", "acquire-sta"});
}

void testStartPairingBusyRestoresSta()
{
    FakeWifiSettingsBackend* backend = nullptr;
    auto owned = makeBackend(backend);
    backend->saved_ssids = {"Office"};
    backend->config_ap_result = WifiSettingsConfigApResult::Busy;
    WifiSettingsController controller(std::move(owned));
    backend->operations.clear();

    assert(!controller.startPairing());
    assert(controller.poll().state == WifiSettingsState::Busy);
    assertOperations(*backend, {"release-sta", "acquire-config-ap", "acquire-sta"});
}

void testPortalExitReleasesBeforeSta()
{
    FakeWifiSettingsBackend* backend = nullptr;
    auto owned = makeBackend(backend);
    backend->saved_ssids = {"Office"};
    WifiSettingsController controller(std::move(owned));
    assert(controller.startPairing());
    backend->operations.clear();
    backend->config_ap_mode = false;

    assert(controller.poll().state == WifiSettingsState::NotConnected);
    assertOperations(*backend, {"release-config-ap", "acquire-sta"});
}

void testExplicitStop()
{
    FakeWifiSettingsBackend* backend = nullptr;
    auto owned = makeBackend(backend);
    backend->saved_ssids = {"Office"};
    WifiSettingsController controller(std::move(owned));
    assert(controller.startPairing());
    backend->operations.clear();

    controller.stopPairing();
    assert(controller.poll().state == WifiSettingsState::NotConnected);
    assertOperations(*backend, {"release-config-ap", "acquire-sta"});
}

void testSelectionReconnects()
{
    FakeWifiSettingsBackend* backend = nullptr;
    auto owned = makeBackend(backend);
    backend->saved_ssids = {"Office", "Home"};
    WifiSettingsController controller(std::move(owned));
    backend->operations.clear();

    assert(controller.selectSaved(1));
    assertOperations(*backend, {"select:1", "reconnect-sta"});
}

void testSelectionDuringPairingAcquiresStaWithoutReconnect()
{
    FakeWifiSettingsBackend* backend = nullptr;
    auto owned = makeBackend(backend);
    backend->saved_ssids = {"Office", "Home"};
    WifiSettingsController controller(std::move(owned));
    assert(controller.startPairing());
    backend->operations.clear();

    assert(controller.selectSaved(1));
    assertOperations(*backend, {"select:1", "release-config-ap", "acquire-sta"});
}

void testSelectionWithoutStaAcquiresOnceWithoutReconnect()
{
    FakeWifiSettingsBackend* backend = nullptr;
    auto owned = makeBackend(backend);
    backend->saved_ssids = {"Office", "Home"};
    backend->allow_sta = false;
    WifiSettingsController controller(std::move(owned));
    backend->allow_sta = true;
    backend->operations.clear();

    assert(controller.selectSaved(1));
    assertOperations(*backend, {"select:1", "acquire-sta"});
}

void testInvalidSelectionDoesNothing()
{
    FakeWifiSettingsBackend* backend = nullptr;
    auto owned = makeBackend(backend);
    backend->saved_ssids = {"Office"};
    WifiSettingsController controller(std::move(owned));
    backend->operations.clear();

    assert(!controller.selectSaved(1));
    assertOperations(*backend, {});
}

void testDestructorReleasesAllOwnership()
{
    FakeWifiSettingsBackend* backend = nullptr;
    auto owned = makeBackend(backend);
    backend->saved_ssids = {"Office"};
    std::vector<std::string> teardown_operations;
    backend->operation_sink = &teardown_operations;
    {
        WifiSettingsController controller(std::move(owned));
        assert(controller.startPairing());
        backend->operations.clear();
        teardown_operations.clear();
    }

    assert(teardown_operations == std::vector<std::string>({"release-config-ap", "release-sta"}));
}

} // namespace

int main()
{
    testNoSavedStartup();
    testSavedStartup();
    testStartPairingOrder();
    testStartPairingFailureRestoresSta();
    testStartPairingBusyRestoresSta();
    testPortalExitReleasesBeforeSta();
    testExplicitStop();
    testSelectionReconnects();
    testSelectionDuringPairingAcquiresStaWithoutReconnect();
    testSelectionWithoutStaAcquiresOnceWithoutReconnect();
    testInvalidSelectionDoesNothing();
    testDestructorReleasesAllOwnership();
    return 0;
}
