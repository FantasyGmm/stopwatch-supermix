#include "apps/app_codex_status/status_message_router.h"

#include <cassert>
#include <cstring>


static StatusMessageSource route(const char* json)
{
    return route_status_message(json, std::strlen(json));
}


int main()
{
    assert(route(R"({"codex_model":"gpt-5"})") ==
           StatusMessageSource::LegacyCodex);
    assert(route(R"({"source":"codex","codex_model":"gpt-5"})") ==
           StatusMessageSource::Codex);
    assert(route(R"({"type":"status","source":"trae"})") ==
           StatusMessageSource::Trae);
    assert(route(R"({"source":"other"})") == StatusMessageSource::Unknown);
    assert(route(R"({"source":7})") == StatusMessageSource::Unknown);
    assert(route("not-json") == StatusMessageSource::Unknown);
}
