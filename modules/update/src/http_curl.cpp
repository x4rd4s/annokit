// SPDX-License-Identifier: GPL-3.0-or-later
// HttpClient through the system's libcurl, loaded at run time (Linux, macOS and the other Unix
// systems): the program starts without it, and only an update needs it. The options and codes are
// libcurl's stable ABI numbers, so no curl headers are needed to build.
#include <algorithm>
#include <dlfcn.h>
#include <mutex>

#include "annokit/update/http.hpp"

namespace annokit::update {

namespace {

using core::ErrorCode;
using core::make_error;

// curl.h: CURLOPTTYPE_LONG + n, OBJECTPOINT 10000 + n, FUNCTIONPOINT 20000 + n.
constexpr int kOptWriteData = 10001;
constexpr int kOptUrl = 10002;
constexpr int kOptErrorBuffer = 10010;
constexpr int kOptWriteFunction = 20011;
constexpr int kOptUserAgent = 10018;
constexpr int kOptLowSpeedLimit = 19;
constexpr int kOptLowSpeedTime = 20;
constexpr int kOptNoProgress = 43;
constexpr int kOptFollowLocation = 52;
constexpr int kOptXferInfoData = 10057;
constexpr int kOptMaxRedirs = 68;
constexpr int kOptConnectTimeout = 78;
constexpr int kOptNoSignal = 99;
constexpr int kOptProtocols = 181;       // before 7.85
constexpr int kOptRedirProtocols = 182;  // before 7.85
constexpr int kOptXferInfoFunction = 20219;
constexpr int kOptProtocolsStr = 10318;
constexpr int kOptRedirProtocolsStr = 10319;
constexpr int kInfoResponseCode = 0x200002;
constexpr long kProtoHttps = 1L << 1;
constexpr long kGlobalDefault = 3;
constexpr int kOk = 0;
constexpr int kAbortedByCallback = 42;
constexpr int kWriteError = 23;
constexpr std::size_t kErrorSize = 256;

struct Curl {
    void* (*easy_init)() = nullptr;
    int (*easy_setopt)(void*, int, ...) = nullptr;
    int (*easy_perform)(void*) = nullptr;
    int (*easy_getinfo)(void*, int, ...) = nullptr;
    void (*easy_cleanup)(void*) = nullptr;
    const char* (*easy_strerror)(int) = nullptr;
    int (*global_init)(long) = nullptr;
    std::string error;  ///< why it could not be loaded
};

template <typename F>
void bind(void* library, const char* name, F& function) {
    function = reinterpret_cast<F>(dlsym(library, name));
}

const Curl& curl() {
    static const Curl loaded = [] {
        Curl c;
#if defined(__APPLE__)
        const char* const names[] = {"/usr/lib/libcurl.4.dylib", "libcurl.4.dylib"};
#else
        const char* const names[] = {"libcurl.so.4", "libcurl-gnutls.so.4"};
#endif
        void* library = nullptr;
        for (const char* name : names) {
            library = dlopen(name, RTLD_NOW | RTLD_LOCAL);
            if (library != nullptr) {
                break;
            }
        }
        if (library == nullptr) {
            c.error = "libcurl is not installed (libcurl.so.4)";
            return c;
        }
        bind(library, "curl_easy_init", c.easy_init);
        bind(library, "curl_easy_setopt", c.easy_setopt);
        bind(library, "curl_easy_perform", c.easy_perform);
        bind(library, "curl_easy_getinfo", c.easy_getinfo);
        bind(library, "curl_easy_cleanup", c.easy_cleanup);
        bind(library, "curl_easy_strerror", c.easy_strerror);
        bind(library, "curl_global_init", c.global_init);
        if (c.easy_init == nullptr || c.easy_setopt == nullptr || c.easy_perform == nullptr ||
            c.easy_getinfo == nullptr || c.easy_cleanup == nullptr || c.easy_strerror == nullptr ||
            c.global_init == nullptr) {
            c.error = "libcurl lacks functions it should have";
            c.easy_init = nullptr;
            return c;
        }
        // Not thread-safe in older versions: once, before any transfer.
        if (c.global_init(kGlobalDefault) != kOk) {
            c.error = "libcurl could not be set up";
            c.easy_init = nullptr;
        }
        return c;
    }();
    return loaded;
}

struct Transfer {
    const HttpRequest* request = nullptr;
    std::vector<std::uint8_t> body;
    bool too_large = false;
    bool cancelled = false;
};

std::size_t on_write(char* data, std::size_t size, std::size_t count, void* user) {
    auto* t = static_cast<Transfer*>(user);
    const std::size_t bytes = size * count;
    if (t->body.size() + bytes > t->request->max_size) {
        t->too_large = true;
        return 0;  // CURLE_WRITE_ERROR
    }
    t->body.insert(t->body.end(), data, data + bytes);
    return bytes;
}

int on_progress(void* user, std::int64_t total, std::int64_t done, std::int64_t, std::int64_t) {
    auto* t = static_cast<Transfer*>(user);
    if (total > 0 && static_cast<std::uint64_t>(total) > t->request->max_size) {
        t->too_large = true;
        return 1;
    }
    if (t->request->progress && !t->request->progress(static_cast<std::uint64_t>(std::max<std::int64_t>(done, 0)),
                                                      static_cast<std::uint64_t>(std::max<std::int64_t>(total, 0)))) {
        t->cancelled = true;
        return 1;
    }
    return 0;
}

class CurlClient final : public HttpClient {
public:
    core::Result<std::vector<std::uint8_t>> get(const HttpRequest& request) override {
        const Curl& c = curl();
        if (!request.url.starts_with("https://")) {
            return make_error(ErrorCode::Unsupported, "http: only https addresses are fetched");
        }
        void* easy = c.easy_init();
        if (easy == nullptr) {
            return make_error(ErrorCode::PlatformError, "http: libcurl could not start a transfer");
        }
        Transfer t;
        t.request = &request;
        char error[kErrorSize] = {};
        c.easy_setopt(easy, kOptUrl, request.url.c_str());
        c.easy_setopt(easy, kOptErrorBuffer, error);
        c.easy_setopt(easy, kOptUserAgent, request.user_agent.c_str());
        c.easy_setopt(easy, kOptWriteFunction, &on_write);
        c.easy_setopt(easy, kOptWriteData, static_cast<void*>(&t));
        c.easy_setopt(easy, kOptXferInfoFunction, &on_progress);
        c.easy_setopt(easy, kOptXferInfoData, static_cast<void*>(&t));
        c.easy_setopt(easy, kOptNoProgress, 0L);
        c.easy_setopt(easy, kOptFollowLocation, 1L);
        c.easy_setopt(easy, kOptMaxRedirs, 8L);
        c.easy_setopt(easy, kOptNoSignal, 1L);  // a worker thread: no SIGALRM for name lookups
        c.easy_setopt(easy, kOptConnectTimeout, static_cast<long>(request.connect_timeout.count()));
        c.easy_setopt(easy, kOptLowSpeedLimit, 1024L);
        c.easy_setopt(easy, kOptLowSpeedTime, static_cast<long>(request.stall_timeout.count()));
        if (c.easy_setopt(easy, kOptProtocolsStr, "https") != kOk) {
            c.easy_setopt(easy, kOptProtocols, kProtoHttps);
        }
        if (c.easy_setopt(easy, kOptRedirProtocolsStr, "https") != kOk) {
            c.easy_setopt(easy, kOptRedirProtocols, kProtoHttps);
        }
        const int code = c.easy_perform(easy);
        long status = 0;
        c.easy_getinfo(easy, kInfoResponseCode, &status);
        c.easy_cleanup(easy);
        if (t.cancelled) {
            return make_error(ErrorCode::Cancelled, "http: cancelled");
        }
        if (t.too_large) {
            return make_error(ErrorCode::LimitExceeded, "http: " + request.url + " is larger than " +
                                                            std::to_string(request.max_size) + " bytes");
        }
        if (code != kOk) {
            const std::string why = error[0] != 0 ? std::string(error) : std::string(c.easy_strerror(code));
            return make_error(
                code == kAbortedByCallback || code == kWriteError ? ErrorCode::Cancelled : ErrorCode::IoError,
                "http: " + request.url + ": " + why);
        }
        if (status != 200) {
            return make_error(status == 404 ? ErrorCode::NotFound : ErrorCode::IoError,
                              "http: " + request.url + ": the server answered " + std::to_string(status));
        }
        return std::move(t.body);
    }
};

}  // namespace

core::Result<std::unique_ptr<HttpClient>> system_http_client() {
    const Curl& c = curl();
    if (c.easy_init == nullptr) {
        return make_error(ErrorCode::PlatformError, "http: " + c.error);
    }
    return std::unique_ptr<HttpClient>(std::make_unique<CurlClient>());
}

}  // namespace annokit::update
