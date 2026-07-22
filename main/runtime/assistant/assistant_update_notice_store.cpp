#include "runtime/assistant/assistant_update_notice_store.h"

#include <esp_err.h>
#include <esp_log.h>
#include <nvs.h>

namespace {

constexpr const char* tag = "AssistantUpdateStore";
constexpr const char* storage_namespace = "assistant_ui";
constexpr const char* version_key = "update_ver";

class ScopedNvsHandle {
public:
    explicit ScopedNvsHandle(nvs_handle_t handle) : _handle(handle) {}
    ~ScopedNvsHandle()
    {
        nvs_close(_handle);
    }

    ScopedNvsHandle(const ScopedNvsHandle&) = delete;
    ScopedNvsHandle& operator=(const ScopedNvsHandle&) = delete;

private:
    nvs_handle_t _handle;
};

void logMissing()
{
    ESP_LOGD(tag, "no persisted Assistant update notice");
}

} // namespace

std::optional<std::string> AssistantUpdateNoticeStore::load() const
{
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open(storage_namespace, NVS_READONLY, &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        logMissing();
        return std::nullopt;
    }
    if (error != ESP_OK) {
        ESP_LOGE(tag, "failed to open update notice storage: %s", esp_err_to_name(error));
        return std::nullopt;
    }
    const ScopedNvsHandle close_handle(handle);

    size_t length = 0;
    error = nvs_get_str(handle, version_key, nullptr, &length);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        logMissing();
        return std::nullopt;
    }
    if (error != ESP_OK) {
        ESP_LOGE(tag, "failed to query update notice version: %s", esp_err_to_name(error));
        return std::nullopt;
    }

    std::string version(length, '\0');
    error = nvs_get_str(handle, version_key, version.data(), &length);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        logMissing();
        return std::nullopt;
    }
    if (error != ESP_OK) {
        ESP_LOGE(tag, "failed to load update notice version: %s", esp_err_to_name(error));
        return std::nullopt;
    }
    while (!version.empty() && version.back() == '\0') {
        version.pop_back();
    }
    return version;
}

bool AssistantUpdateNoticeStore::save(std::string_view version) const
{
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open(storage_namespace, NVS_READWRITE, &handle);
    if (error != ESP_OK) {
        ESP_LOGE(tag, "failed to open update notice storage for save: %s", esp_err_to_name(error));
        return false;
    }
    const ScopedNvsHandle close_handle(handle);

    const std::string stored_version(version);
    error = nvs_set_str(handle, version_key, stored_version.c_str());
    if (error != ESP_OK) {
        ESP_LOGE(tag, "failed to save update notice version: %s", esp_err_to_name(error));
        return false;
    }
    error = nvs_commit(handle);
    if (error != ESP_OK) {
        ESP_LOGE(tag, "failed to commit update notice version: %s", esp_err_to_name(error));
        return false;
    }
    return true;
}

bool AssistantUpdateNoticeStore::clear() const
{
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open(storage_namespace, NVS_READWRITE, &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        logMissing();
        return true;
    }
    if (error != ESP_OK) {
        ESP_LOGE(tag, "failed to open update notice storage for clear: %s", esp_err_to_name(error));
        return false;
    }
    const ScopedNvsHandle close_handle(handle);

    error = nvs_erase_key(handle, version_key);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        logMissing();
        return true;
    }
    if (error != ESP_OK) {
        ESP_LOGE(tag, "failed to erase update notice version: %s", esp_err_to_name(error));
        return false;
    }
    error = nvs_commit(handle);
    if (error != ESP_OK) {
        ESP_LOGE(tag, "failed to commit cleared update notice: %s", esp_err_to_name(error));
        return false;
    }
    return true;
}
