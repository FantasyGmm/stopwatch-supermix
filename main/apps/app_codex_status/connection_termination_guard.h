#pragma once

#include <mutex>
#include <utility>

namespace secure_pairing {

template <typename Operation>
decltype(auto) serialize_connection_termination(
    std::mutex& router_guard,
    Operation&& operation)
{
    std::lock_guard<std::mutex> lock(router_guard);
    return std::forward<Operation>(operation)();
}

}  // namespace secure_pairing
