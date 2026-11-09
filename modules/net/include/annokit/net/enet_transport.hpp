// SPDX-License-Identifier: GPL-3.0-or-later
//
// The transport over the internet and local networks: ENet , reliable and
// ordered messages over UDP. Only the host needs a reachable port; the other computers
// connect out to it, which passes through home routers without opening anything.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "annokit/core/result.hpp"
#include "annokit/net/transport.hpp"

namespace annokit::net {

/// The host's end: listens on `port` (all addresses) for up to `max_computers` computers.
[[nodiscard]] core::Result<std::unique_ptr<Transport>> enet_listen(std::uint16_t port, std::size_t max_computers = 32);
/// A computer's end: connects to the host at `address` (name or IPv4 address). The connection is
/// there with the Connected event, or failed with Disconnected.
[[nodiscard]] core::Result<std::unique_ptr<Transport>> enet_connect(const std::string& address, std::uint16_t port);

}  // namespace annokit::net
