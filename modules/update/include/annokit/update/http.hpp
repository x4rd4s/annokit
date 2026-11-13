// SPDX-License-Identifier: GPL-3.0-or-later
//
// HTTPS downloads through what the system brings, so that no TLS library has to be shipped and kept
// up to date: WinHTTP on Windows (the system's certificates and proxy settings), elsewhere the
// system's libcurl, loaded when it is first needed (libcurl.so.4 or Debian's libcurl-gnutls.so.4 on
// Linux, /usr/lib/libcurl.4.dylib on macOS). Only https, redirects to https included.
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "annokit/core/result.hpp"

namespace annokit::update {

struct HttpRequest {
    std::string url;
    std::string user_agent = "annokit";
    /// A larger answer is refused (LimitExceeded) without reading the rest.
    std::uint64_t max_size = std::uint64_t{64} << 20;
    std::chrono::seconds connect_timeout{10};
    /// The download gives up when fewer than 1 KiB arrive in this time.
    std::chrono::seconds stall_timeout{30};
    /// Called while the answer arrives: bytes so far and all of them (0 if not known yet). Returning
    /// false stops the download (Cancelled).
    std::function<bool(std::uint64_t done, std::uint64_t total)> progress;
};

class HttpClient {
public:
    virtual ~HttpClient() = default;
    /// The body of a GET of `request.url`; an answer other than 200 is an error (NotFound for 404).
    /// Blocks: call it beside the game, never from the frame loop.
    [[nodiscard]] virtual core::Result<std::vector<std::uint8_t>> get(const HttpRequest& request) = 0;
};

/// The system's HTTPS client, or why there is none (PlatformError: no libcurl found).
[[nodiscard]] core::Result<std::unique_ptr<HttpClient>> system_http_client();

}  // namespace annokit::update
