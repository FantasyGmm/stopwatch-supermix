#include "status_message_router.h"

#include <cJSON.h>
#include <cstring>


StatusMessageSource route_status_message(const char* data, size_t length)
{
    if (data == nullptr || length == 0) {
        return StatusMessageSource::Unknown;
    }
    cJSON* root = cJSON_ParseWithLength(data, length);
    if (root == nullptr || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return StatusMessageSource::Unknown;
    }

    cJSON* source = cJSON_GetObjectItemCaseSensitive(root, "source");
    StatusMessageSource result = StatusMessageSource::Unknown;
    if (source == nullptr) {
        result = StatusMessageSource::LegacyCodex;
    } else if (cJSON_IsString(source) && source->valuestring != nullptr) {
        if (std::strcmp(source->valuestring, "codex") == 0) {
            result = StatusMessageSource::Codex;
        } else if (std::strcmp(source->valuestring, "trae") == 0) {
            result = StatusMessageSource::Trae;
        }
    }
    cJSON_Delete(root);
    return result;
}
