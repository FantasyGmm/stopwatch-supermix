#include "main/apps/app_codex_status/codex_task_detail_store.h"

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

namespace {

constexpr uint64_t kRoute = UINT64_C(0x0123456789abcdef);

CodexTaskDetailChunk chunk(
    uint64_t route,
    int revision,
    size_t index,
    size_t count,
    uint8_t nonce_tail,
    uint32_t sequence,
    std::string text)
{
    CodexTaskDetailChunk value;
    value.route = route;
    value.revision = revision;
    value.index = index;
    value.count = count;
    value.exchange_nonce = {1, 2, 3, 4, 5, 6, 7, nonce_tail};
    value.sequence = sequence;
    value.text = std::move(text);
    return value;
}

}  // namespace

int main()
{
    CodexTaskDetailStore store;
    store.reset_for_credential(7);

    assert(store.accept(chunk(kRoute, 4, 2, 3, 8, 1, "three")));
    assert(store.find(kRoute) == nullptr);
    assert(store.accept(chunk(kRoute, 4, 0, 3, 8, 2, "one ")));
    assert(store.find(kRoute) == nullptr);
    assert(store.accept(chunk(kRoute, 4, 1, 3, 8, 3, "two ")));
    assert(store.find(kRoute) != nullptr);
    assert(store.find(kRoute)->revision == 4);
    assert(store.find(kRoute)->text == "one two three");

    // Logical duplicates need fresh wire sequence and identical content.
    assert(store.accept(chunk(kRoute, 5, 0, 2, 8, 4, "new ")));
    assert(store.accept(chunk(kRoute, 5, 0, 2, 8, 5, "new ")));
    assert(!store.accept(chunk(kRoute, 5, 0, 2, 8, 5, "new ")));
    assert(!store.accept(chunk(kRoute, 5, 0, 2, 8, 6, "conflict")));
    assert(store.find(kRoute)->revision == 4);
    assert(store.accept(chunk(kRoute, 5, 1, 2, 8, 7, "reply")));
    assert(store.find(kRoute)->revision == 4);  // conflict invalidated pending revision
    assert(store.accept(chunk(kRoute, 5, 0, 2, 8, 8, "new ")));
    assert(store.find(kRoute)->revision == 5);
    assert(store.find(kRoute)->text == "new reply");
    assert(!store.accept(chunk(kRoute, 4, 0, 1, 8, 9, "old")));
    assert(!store.accept(chunk(kRoute, 5, 0, 1, 8, 10, "conflicting published")));

    CodexTaskDetailStore newer_count;
    newer_count.reset_for_credential(1);
    assert(newer_count.accept(chunk(kRoute, 1, 0, 2, 7, 1, "old ")));
    assert(newer_count.accept(chunk(kRoute, 2, 0, 1, 7, 2, "new")));
    assert(newer_count.find(kRoute) != nullptr);
    assert(newer_count.find(kRoute)->revision == 2);
    assert(newer_count.find(kRoute)->text == "new");

    // New nonces can start a session; retired nonces cannot return.
    assert(store.accept(chunk(kRoute, 6, 0, 1, 9, 1, "session two")));
    assert(store.accept(chunk(kRoute, 7, 0, 1, 9, UINT32_MAX, "last sequence")));
    assert(!store.accept(chunk(kRoute, 8, 0, 1, 9, UINT32_MAX, "exhausted")));
    assert(!store.accept(chunk(kRoute, 7, 0, 1, 8, 10, "replayed nonce")));
    for (uint8_t nonce = 10; nonce < 18; ++nonce) {
        assert(store.accept(chunk(kRoute, 6 + nonce, 0, 1, nonce, 1, "rotate")));
    }
    assert(!store.accept(chunk(kRoute, 30, 0, 1, 9, 2, "recent replay")));

    store.reset_connection();
    assert(store.accept(chunk(kRoute, 40, 0, 1, 42, 1, "connected")));
    assert(!store.accept(chunk(kRoute, 41, 0, 1, 17, 2, "retired")));

    store.reset_for_credential(8);
    assert(store.find(kRoute) == nullptr);
    assert(store.accept(chunk(kRoute, 1, 0, 1, 8, 1, "epoch reset")));

    // UTF-8 and assembly bounds fail closed.
    std::string invalid_utf8(1, static_cast<char>(0xff));
    assert(!store.accept(chunk(kRoute, 2, 0, 1, 8, 2, invalid_utf8)));
    assert(!store.accept(chunk(kRoute, 2, 0, 17, 8, 3, "too many")));
    assert(!store.accept(chunk(kRoute, 2, 0, 1, 8, 4, std::string(1301, 'x'))));

    CodexTaskDetailStore bounded;
    bounded.reset_for_credential(1);
    const std::string marker = "\n\n[内容过长，手表仅显示前 12 KiB]";
    for (size_t index = 0; index < 9; ++index) {
        assert(bounded.accept(chunk(
            kRoute, 1, index, 10, 1, static_cast<uint32_t>(index + 1), std::string(1300, 'x'))));
    }
    assert(bounded.accept(chunk(kRoute, 1, 9, 10, 1, 10, std::string(588, 'x') + marker)));
    assert(bounded.find(kRoute)->text.size() == 12288 + marker.size());

    CodexTaskDetailStore multibyte_boundary;
    multibyte_boundary.reset_for_credential(1);
    for (size_t index = 0; index < 9; ++index) {
        assert(multibyte_boundary.accept(chunk(
            kRoute, 1, index, 10, 2, static_cast<uint32_t>(index + 1), std::string(1300, 'x'))));
    }
    assert(multibyte_boundary.accept(chunk(
        kRoute, 1, 9, 10, 2, 10, std::string(587, 'x') + marker)));
    assert(multibyte_boundary.find(kRoute)->text.size() == 12287 + marker.size());

    CodexTaskDetailStore routes;
    routes.reset_for_credential(1);
    for (uint64_t route = 1; route <= 8; ++route) {
        assert(routes.accept(chunk(route, 1, 0, 1, 1, static_cast<uint32_t>(route), "ok")));
    }
    assert(!routes.accept(chunk(9, 1, 0, 1, 1, 9, "full")));
    routes.retain_routes({1, 3, 5});
    assert(routes.find(1) != nullptr);
    assert(routes.find(2) == nullptr);
    assert(routes.accept(chunk(9, 1, 0, 1, 1, 10, "space")));

    CodexTaskDetailStore unique_routes;
    unique_routes.reset_for_credential(1);
    for (uint64_t route = 1; route <= 7; ++route) {
        assert(unique_routes.accept(chunk(route, 1, 0, 1, 3, static_cast<uint32_t>(route), "old")));
    }
    assert(unique_routes.accept(chunk(1, 2, 0, 2, 3, 8, "pending ")));
    assert(unique_routes.accept(chunk(8, 1, 0, 1, 3, 9, "eighth")));

    routes.reset();
    assert(routes.find(1) == nullptr);
    assert(!routes.accept(chunk(1, 1, 0, 1, 1, 1, "no credential")));
    routes.reset_for_credential(2);
    assert(routes.accept(chunk(1, 1, 0, 1, 1, 1, "fresh")));
}
