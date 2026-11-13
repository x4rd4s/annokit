// SPDX-License-Identifier: GPL-3.0-or-later
// HttpClient through WinHTTP: the system's TLS, certificates and proxy settings.
#include "annokit/update/http.hpp"

#include <algorithm>
#include <string>
#include <windows.h>
#include <winhttp.h>

namespace annokit::update {

namespace {

using core::ErrorCode;
using core::make_error;

std::wstring wide(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(std::max(length, 0)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), length);
    return out;
}

/// Closes a WinHTTP handle when it goes out of scope.
struct Handle {
    HINTERNET h = nullptr;
    explicit Handle(HINTERNET handle) : h(handle) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() {
        if (h != nullptr) {
            WinHttpCloseHandle(h);
        }
    }
};

core::Error failed(const std::string& url, const char* step) {
    const DWORD code = GetLastError();
    const ErrorCode kind = code == ERROR_WINHTTP_NAME_NOT_RESOLVED || code == ERROR_WINHTTP_CANNOT_CONNECT ||
                                   code == ERROR_WINHTTP_TIMEOUT || code == ERROR_WINHTTP_CONNECTION_ERROR
                               ? ErrorCode::IoError
                               : ErrorCode::PlatformError;
    return make_error(kind, "http: " + url + ": " + step + " failed (WinHTTP error " + std::to_string(code) + ")");
}

class WinHttpClient final : public HttpClient {
public:
    core::Result<std::vector<std::uint8_t>> get(const HttpRequest& request) override {
        if (!request.url.starts_with("https://")) {
            return make_error(ErrorCode::Unsupported, "http: only https addresses are fetched");
        }
        const std::wstring url = wide(request.url);
        URL_COMPONENTS parts{};
        parts.dwStructSize = sizeof(parts);
        parts.dwHostNameLength = static_cast<DWORD>(-1);
        parts.dwUrlPathLength = static_cast<DWORD>(-1);
        parts.dwExtraInfoLength = static_cast<DWORD>(-1);
        if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS) {
            return make_error(ErrorCode::InvalidFormat, "http: " + request.url + " is not an https address");
        }
        const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
        std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
        path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);

        Handle session(WinHttpOpen(wide(request.user_agent).c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                   WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
        if (session.h == nullptr) {
            // Before Windows 8.1 there is no automatic proxy: the default one.
            session.h = WinHttpOpen(wide(request.user_agent).c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        }
        if (session.h == nullptr) {
            return failed(request.url, "WinHttpOpen");
        }
        const int connect_ms = static_cast<int>(request.connect_timeout.count() * 1000);
        const int stall_ms = static_cast<int>(request.stall_timeout.count() * 1000);
        WinHttpSetTimeouts(session.h, connect_ms, connect_ms, stall_ms, stall_ms);
        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
        protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
        if (!WinHttpSetOption(session.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols))) {
            protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;  // TLS 1.3 needs Windows 11
            WinHttpSetOption(session.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
        }
        DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
        WinHttpSetOption(session.h, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects));

        Handle connection(WinHttpConnect(session.h, host.c_str(), parts.nPort, 0));
        if (connection.h == nullptr) {
            return failed(request.url, "WinHttpConnect");
        }
        Handle req(WinHttpOpenRequest(connection.h, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
        if (req.h == nullptr) {
            return failed(request.url, "WinHttpOpenRequest");
        }
        if (!WinHttpSendRequest(req.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            return failed(request.url, "sending the request");
        }
        if (!WinHttpReceiveResponse(req.h, nullptr)) {
            return failed(request.url, "receiving the answer");
        }
        DWORD status = 0;
        DWORD size = sizeof(status);
        if (!WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX)) {
            return failed(request.url, "reading the answer");
        }
        if (status != 200) {
            return make_error(status == 404 ? ErrorCode::NotFound : ErrorCode::IoError,
                              "http: " + request.url + ": the server answered " + std::to_string(status));
        }
        std::uint64_t total = 0;
        wchar_t length_text[32] = {};
        DWORD length_size = sizeof(length_text);
        if (WinHttpQueryHeaders(req.h, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, length_text,
                                &length_size, WINHTTP_NO_HEADER_INDEX)) {
            total = std::wcstoull(length_text, nullptr, 10);
        }
        if (total > request.max_size) {
            return make_error(ErrorCode::LimitExceeded, "http: " + request.url + " is larger than " +
                                                            std::to_string(request.max_size) + " bytes");
        }
        std::vector<std::uint8_t> body;
        body.reserve(static_cast<std::size_t>(total));
        while (true) {
            if (request.progress && !request.progress(body.size(), total)) {
                return make_error(ErrorCode::Cancelled, "http: cancelled");
            }
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(req.h, &available)) {
                return failed(request.url, "receiving the data");
            }
            if (available == 0) {
                break;
            }
            if (body.size() + available > request.max_size) {
                return make_error(ErrorCode::LimitExceeded, "http: " + request.url + " is larger than " +
                                                                std::to_string(request.max_size) + " bytes");
            }
            const std::size_t at = body.size();
            body.resize(at + available);
            DWORD read = 0;
            if (!WinHttpReadData(req.h, body.data() + at, available, &read)) {
                return failed(request.url, "receiving the data");
            }
            body.resize(at + read);
        }
        if (request.progress) {
            request.progress(body.size(), total);
        }
        return body;
    }
};

}  // namespace

core::Result<std::unique_ptr<HttpClient>> system_http_client() {
    return std::unique_ptr<HttpClient>(std::make_unique<WinHttpClient>());
}

}  // namespace annokit::update
