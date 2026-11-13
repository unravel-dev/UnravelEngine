#include "https_get_platform.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>

#include <array>
#include <condition_variable>
#include <memory>
#include <mutex>

// The request runs in WinHTTP's asynchronous mode: the waiting thread can end it at any time by closing the request
// handle, which WinHTTP documents for that mode, and the callbacks keep their state alive until WinHTTP reports
// the handle closed.
namespace unravel::update_check::detail
{
namespace
{

constexpr DWORD HTTP_STATUS_SUCCESS = 200;
constexpr std::size_t READ_BUFFER_SIZE = 4096;
constexpr DWORD CALLBACK_FLAGS = WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES;

struct handle_closer
{
    void operator()(HINTERNET handle) const
    {
        WinHttpCloseHandle(handle);
    }
};
using internet_handle = std::unique_ptr<void, handle_closer>;

/// What a request's callbacks report to the thread that waits for it.
struct request_state
{
    std::mutex mutex;
    std::condition_variable changed;
    bool is_finished = false;
    bool has_body = false;
    std::string error;
    /// Written by the callbacks alone, read only once they finished with a body.
    std::string body;
    std::size_t max_body_size = 0;
    std::array<char, READ_BUFFER_SIZE> buffer{};
};
using shared_request_state = std::shared_ptr<request_state>;

struct url_parts
{
    std::wstring host;
    INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
    std::wstring path;
};

auto describe_failure(const char* step, DWORD code) -> std::string
{
    return std::string(step) + " failed with error " + std::to_string(code) + ".";
}

auto to_wide(const std::string& text) -> std::wstring
{
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
    std::wstring wide(std::size_t(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), wide.data(), size);
    return wide;
}

auto split_url(const std::string& url) -> hpp::optional<url_parts>
{
    const std::wstring wide_url = to_wide(url);
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    // A length without a buffer asks for pointers into the address.
    components.dwSchemeLength = DWORD(-1);
    components.dwHostNameLength = DWORD(-1);
    components.dwUrlPathLength = DWORD(-1);
    components.dwExtraInfoLength = DWORD(-1);
    if(!WinHttpCrackUrl(wide_url.c_str(), DWORD(wide_url.size()), 0, &components) ||
       components.nScheme != INTERNET_SCHEME_HTTPS || components.dwHostNameLength == 0)
    {
        return hpp::nullopt;
    }
    url_parts parts;
    parts.host.assign(components.lpszHostName, components.dwHostNameLength);
    parts.port = components.nPort;
    parts.path.assign(components.lpszUrlPath, components.dwUrlPathLength);
    if(components.lpszExtraInfo != nullptr)
    {
        parts.path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    }
    if(parts.path.empty())
    {
        parts.path = L"/";
    }
    return parts;
}

/// Ends the request with `error`, or with its body when `error` is empty. Only the first end counts.
void finish_request(request_state& state, std::string error)
{
    {
        const std::lock_guard<std::mutex> lock(state.mutex);
        if(state.is_finished)
        {
            return;
        }
        state.is_finished = true;
        state.has_body = error.empty();
        state.error = std::move(error);
    }
    state.changed.notify_all();
}

void read_next_chunk(HINTERNET request, request_state& state)
{
    if(!WinHttpReadData(request, state.buffer.data(), DWORD(state.buffer.size()), nullptr))
    {
        finish_request(state, describe_failure("WinHttpReadData", GetLastError()));
    }
}

void on_headers_available(HINTERNET request, request_state& state)
{
    DWORD status_code = 0;
    DWORD size = sizeof(status_code);
    if(!WinHttpQueryHeaders(request,
                            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX,
                            &status_code,
                            &size,
                            WINHTTP_NO_HEADER_INDEX))
    {
        finish_request(state, describe_failure("WinHttpQueryHeaders", GetLastError()));
        return;
    }
    if(status_code != HTTP_STATUS_SUCCESS)
    {
        finish_request(state, "The server answered with status " + std::to_string(status_code) + ".");
        return;
    }
    read_next_chunk(request, state);
}

void on_read_complete(HINTERNET request, request_state& state, DWORD size)
{
    if(size == 0)
    {
        finish_request(state, {});
        return;
    }
    if(state.body.size() + size > state.max_body_size)
    {
        finish_request(state, "The answer is larger than " + std::to_string(state.max_body_size) + " bytes.");
        return;
    }
    state.body.append(state.buffer.data(), size);
    read_next_chunk(request, state);
}

void CALLBACK on_request_status(HINTERNET request,
                                DWORD_PTR context,
                                DWORD status,
                                LPVOID information,
                                DWORD information_length)
{
    auto* owner = reinterpret_cast<shared_request_state*>(context);
    if(owner == nullptr)
    {
        return;
    }
    request_state& state = **owner;
    switch(status)
    {
        case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
            if(!WinHttpReceiveResponse(request, nullptr))
            {
                finish_request(state, describe_failure("WinHttpReceiveResponse", GetLastError()));
            }
            break;
        case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:
            on_headers_available(request, state);
            break;
        case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:
            on_read_complete(request, state, information_length);
            break;
        case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
            finish_request(state,
                           describe_failure("The request", static_cast<WINHTTP_ASYNC_RESULT*>(information)->dwError));
            break;
        case WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING:
        {
            // The last call for this handle: the callbacks' reference to the state goes with it.
            const std::unique_ptr<shared_request_state> callbacks_reference(owner);
            break;
        }
        default:
            break;
    }
}

auto open_session(const https_limits& limits) -> internet_handle
{
    internet_handle session(WinHttpOpen(to_wide(USER_AGENT).c_str(),
                                        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                        WINHTTP_NO_PROXY_NAME,
                                        WINHTTP_NO_PROXY_BYPASS,
                                        WINHTTP_FLAG_ASYNC));
    if(session)
    {
        const int timeout_ms = int(std::chrono::duration_cast<std::chrono::milliseconds>(limits.total_time).count());
        WinHttpSetTimeouts(session.get(), timeout_ms, timeout_ms, timeout_ms, timeout_ms);
    }
    return session;
}

auto configure_request(HINTERNET request, const https_limits& limits) -> bool
{
    DWORD disabled_features = WINHTTP_DISABLE_COOKIES;
    DWORD max_redirects = DWORD(limits.max_redirects);
    DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    return WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &disabled_features, sizeof(disabled_features)) &&
           WinHttpSetOption(request, WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS, &max_redirects, sizeof(max_redirects)) &&
           WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirect_policy, sizeof(redirect_policy));
}

/// Hands the state to the callbacks and sends the request. The error when it did not start; empty when it did.
auto start_request(HINTERNET request, const shared_request_state& state) -> std::string
{
    if(WinHttpSetStatusCallback(request, on_request_status, CALLBACK_FLAGS, 0) == WINHTTP_INVALID_STATUS_CALLBACK)
    {
        return describe_failure("WinHttpSetStatusCallback", GetLastError());
    }
    // A reference of the callbacks' own: once the handle holds it, the HANDLE_CLOSING callback releases it.
    auto owner = std::make_unique<shared_request_state>(state);
    auto context = reinterpret_cast<DWORD_PTR>(owner.get());
    if(!WinHttpSetOption(request, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context)))
    {
        return describe_failure("WinHttpSetOption", GetLastError());
    }
    owner.release();
    if(!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, context))
    {
        return describe_failure("WinHttpSendRequest", GetLastError());
    }
    return {};
}

/// Waits until the request finished, or ends it when `should_end` says so.
void wait_for_request(request_state& state, const stop_check& should_end)
{
    std::unique_lock<std::mutex> lock(state.mutex);
    while(!state.changed.wait_for(lock,
                                  STOP_POLL_INTERVAL,
                                  [&state]()
                                  {
                                      return state.is_finished;
                                  }))
    {
        if(should_end())
        {
            lock.unlock();
            finish_request(state, "The request was stopped or ran out of time.");
            return;
        }
    }
}

auto take_result(request_state& state) -> https_result
{
    const std::lock_guard<std::mutex> lock(state.mutex);
    if(!state.has_body)
    {
        return {hpp::nullopt, state.error};
    }
    return {std::move(state.body), {}};
}

auto fail(std::string error) -> https_result
{
    return {hpp::nullopt, std::move(error)};
}

} // namespace

auto get_over_https(const std::string& url, const https_limits& limits, const stop_check& should_end) -> https_result
{
    const auto parts = split_url(url);
    if(!parts)
    {
        return fail("The address could not be read: " + url);
    }
    const internet_handle session = open_session(limits);
    if(!session)
    {
        return fail(describe_failure("WinHttpOpen", GetLastError()));
    }
    const internet_handle connection(WinHttpConnect(session.get(), parts->host.c_str(), parts->port, 0));
    if(!connection)
    {
        return fail(describe_failure("WinHttpConnect", GetLastError()));
    }
    internet_handle request(WinHttpOpenRequest(connection.get(),
                                               L"GET",
                                               parts->path.c_str(),
                                               nullptr,
                                               WINHTTP_NO_REFERER,
                                               WINHTTP_DEFAULT_ACCEPT_TYPES,
                                               WINHTTP_FLAG_SECURE));
    if(!request)
    {
        return fail(describe_failure("WinHttpOpenRequest", GetLastError()));
    }
    if(!configure_request(request.get(), limits))
    {
        return fail(describe_failure("WinHttpSetOption", GetLastError()));
    }
    const auto state = std::make_shared<request_state>();
    state->max_body_size = limits.max_body_size;
    if(auto error = start_request(request.get(), state); !error.empty())
    {
        finish_request(*state, std::move(error));
    }
    wait_for_request(*state, should_end);
    // Closing ends a request that still runs; its callbacks may still come, on the state they hold.
    request.reset();
    return take_result(*state);
}

} // namespace unravel::update_check::detail
