// SPDX-License-Identifier: GPL-3.0-or-later
// The in-memory network: messages arrive whole, in order and after their latency; a cut link is
// seen at both ends.

#include <cstdint>
#include <vector>

#include "../test_framework.hpp"

#include "annokit/net/loopback.hpp"

using namespace annokit;

namespace {

std::vector<net::TransportEvent> poll(net::Transport& t) {
    std::vector<net::TransportEvent> events;
    t.poll(events);
    return events;
}

}  // namespace

TEST_CASE("loopback: connect, send in order after the latency, cut") {
    net::LoopbackNetwork network(50, 0, 7);
    auto host = network.listen();
    auto client = network.connect();
    network.advance(50);
    auto host_events = poll(*host);
    REQUIRE(host_events.size() == 1);
    CHECK(host_events[0].kind == net::TransportEvent::Kind::Connected);
    const net::LinkId link = host_events[0].link;
    (void)poll(*client);

    const std::vector<std::uint8_t> first = {1, 2, 3};
    const std::vector<std::uint8_t> second = {4};
    host->send(link, first);
    host->send(link, second);
    host->flush();
    network.advance(49);
    CHECK(poll(*client).empty());
    network.advance(1);
    auto received = poll(*client);
    REQUIRE(received.size() == 2);
    CHECK(received[0].data == first);
    CHECK(received[1].data == second);
    CHECK(network.messages_sent() == 2);

    network.cut(*client);
    network.advance(100);
    auto after = poll(*host);
    REQUIRE(!after.empty());
    CHECK(after.back().kind == net::TransportEvent::Kind::Disconnected);
}

TEST_CASE("loopback: jitter keeps the order of a link and is the same for the same seed") {
    std::vector<std::uint32_t> arrival[2];
    for (int run = 0; run < 2; ++run) {
        net::LoopbackNetwork network(20, 30, 99);
        auto host = network.listen();
        auto client = network.connect();
        network.advance(100);
        const auto events = poll(*host);
        REQUIRE(!events.empty());
        (void)poll(*client);
        for (std::uint8_t i = 0; i < 20; ++i) {
            const std::vector<std::uint8_t> message = {i};
            client->send(0, message);
        }
        client->flush();
        std::vector<std::uint8_t> order;
        for (std::uint32_t ms = 1; ms <= 200; ++ms) {
            network.advance(1);
            for (const auto& e : poll(*host)) {
                if (e.kind == net::TransportEvent::Kind::Received) {
                    order.push_back(e.data.at(0));
                    arrival[run].push_back(ms);
                }
            }
        }
        REQUIRE(order.size() == 20);
        for (std::uint8_t i = 0; i < 20; ++i) {
            CHECK(order[i] == i);
        }
    }
    CHECK(arrival[0] == arrival[1]);
}
