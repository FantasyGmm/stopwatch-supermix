#include "runtime/resource_supervisor.h"
#include "runtime/services/shared_wifi_policy.h"
#include "runtime/services/shared_wifi_service.h"

#include <cassert>
#include <utility>

namespace {
class FakeWifiDriver final : public SharedWifiDriver {
public:
    bool startSta() override
    {
        ++sta_starts;
        return sta_start_succeeds;
    }

    void stopSta() override
    {
        ++sta_stops;
    }

    bool startAp() override
    {
        ++ap_starts;
        return ap_start_succeeds;
    }

    void stopAp() override
    {
        ++ap_stops;
    }

    bool startConfigAp() override
    {
        ++config_ap_starts;
        return config_ap_start_succeeds;
    }

    void stopConfigAp() override
    {
        ++config_ap_stops;
    }

    bool staRequiredBySystem() const override
    {
        return system_requires_sta;
    }

    bool staConnected() const override
    {
        return sta_connected;
    }

    bool sta_start_succeeds = true;
    bool ap_start_succeeds  = true;
    bool config_ap_start_succeeds = true;
    bool system_requires_sta = false;
    bool sta_connected = false;
    int sta_starts = 0;
    int sta_stops  = 0;
    int ap_starts  = 0;
    int ap_stops   = 0;
    int config_ap_starts = 0;
    int config_ap_stops  = 0;
};
} // namespace

int main()
{
    ResourceSupervisor resources;
    FakeWifiDriver driver;
    SharedWifiPolicy policy(resources, driver);
    SharedWifiService wifi(policy);

    {
        auto assistant = wifi.acquireSta(RuntimeOwner::Assistant);
        assert(assistant.valid());
        assert(driver.sta_starts == 1);
        assert(resources.wifiLeaseCount() == 1);
        assert(!wifi.staConnected());

        driver.sta_connected = true;
        assert(wifi.staConnected());

        auto duplicate = wifi.acquireSta(RuntimeOwner::Assistant);
        assert(!duplicate.valid());
        assert(driver.sta_starts == 1);

        auto codex = wifi.acquireSta(RuntimeOwner::Codex);
        assert(codex.valid());
        assert(driver.sta_starts == 1);
        assert(resources.wifiLeaseCount() == 2);

        assistant.reset();
        assert(driver.sta_stops == 0);
        assert(resources.wifiLeaseCount() == 1);

        WifiStaLease moved = std::move(codex);
        assert(!codex.valid());
        assert(moved.valid());
    }
    assert(driver.sta_stops == 1);
    assert(resources.wifiLeaseCount() == 0);

    driver.system_requires_sta = true;
    {
        auto system_shared = wifi.acquireSta(RuntimeOwner::Codex);
        assert(system_shared.valid());
    }
    assert(driver.sta_starts == 2);
    assert(driver.sta_stops == 1);
    driver.system_requires_sta = false;

    auto assistant = wifi.acquireSta(RuntimeOwner::Assistant);
    assert(assistant.valid());
    auto blocked_ap = wifi.acquireAp(RuntimeOwner::Badge);
    assert(!blocked_ap.valid());
    assert(driver.ap_starts == 0);
    assistant.reset();

    {
        auto badge = wifi.acquireAp(RuntimeOwner::Badge);
        assert(badge.valid());
        assert(driver.ap_starts == 1);
        assert(resources.snapshot().wifi_ap_active);

        auto blocked_sta = wifi.acquireSta(RuntimeOwner::Codex);
        assert(!blocked_sta.valid());

        WifiApSession moved = std::move(badge);
        assert(!badge.valid());
        assert(moved.valid());
    }
    assert(driver.ap_stops == 1);
    assert(!resources.snapshot().wifi_ap_active);

    driver.sta_start_succeeds = false;
    auto failed_sta = wifi.acquireSta(RuntimeOwner::Codex);
    assert(!failed_sta.valid());
    assert(resources.wifiLeaseCount() == 0);
    driver.sta_start_succeeds = true;

    driver.ap_start_succeeds = false;
    auto failed_ap = wifi.acquireAp(RuntimeOwner::Badge);
    assert(!failed_ap.valid());
    assert(!resources.snapshot().wifi_ap_active);

    driver.ap_start_succeeds = true;

    assert(!wifi.reconnectSta(RuntimeOwner::Settings));

    {
        auto settings_sta = wifi.acquireSta(RuntimeOwner::Settings);
        assert(settings_sta.valid());
        assert(wifi.reconnectSta(RuntimeOwner::Settings));
        assert(!wifi.reconnectSta(RuntimeOwner::Codex));

        auto blocked_config_ap = wifi.acquireConfigApDetailed(RuntimeOwner::Settings);
        assert(blocked_config_ap.result == WifiConfigApAcquireResult::Busy);
        assert(!blocked_config_ap.session.valid());
    }
    assert(!wifi.reconnectSta(RuntimeOwner::Settings));

    {
        auto raw_ap = wifi.acquireAp(RuntimeOwner::Badge);
        assert(raw_ap.valid());
        auto blocked_config_ap = wifi.acquireConfigApDetailed(RuntimeOwner::Settings);
        assert(blocked_config_ap.result == WifiConfigApAcquireResult::Busy);
        assert(!blocked_config_ap.session.valid());
    }

    {
        const int ap_starts_before = driver.ap_starts;
        const int ap_stops_before = driver.ap_stops;
        auto config_ap = wifi.acquireConfigAp(RuntimeOwner::Settings);
        assert(config_ap.valid());
        assert(driver.config_ap_starts == 1);
        assert(driver.config_ap_stops == 0);
        assert(driver.ap_starts == ap_starts_before);
        assert(driver.ap_stops == ap_stops_before);
        assert(resources.snapshot().wifi_ap_active);
    }
    assert(driver.config_ap_stops == 1);
    assert(!resources.snapshot().wifi_ap_active);

    driver.config_ap_start_succeeds = false;
    auto failed_config_ap = wifi.acquireConfigApDetailed(RuntimeOwner::Settings);
    assert(failed_config_ap.result == WifiConfigApAcquireResult::StartFailed);
    assert(!failed_config_ap.session.valid());
    assert(!resources.snapshot().wifi_ap_active);

    return 0;
}
