#include "convex/platform/win_transport.hpp"

#if defined(CONVEX_PLATFORM_WINDOWS)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <stdexcept>

namespace convex {

namespace {

std::wstring to_wide(const std::string& str) {
    if (str.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    std::wstring result(size - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &result[0], size);
    return result;
}

void parse_url(
    const std::string& url,
    std::wstring& host,
    int& port,
    std::wstring& path,
    bool& use_tls
) {
    std::string remaining = url;
    
    if (remaining.find("wss://") == 0) {
        use_tls = true;
        port = 443;
        remaining = remaining.substr(6);
    } else if (remaining.find("ws://") == 0) {
        use_tls = false;
        port = 80;
        remaining = remaining.substr(5);
    } else {
        throw std::runtime_error("Invalid WebSocket URL");
    }
    
    size_t path_start = remaining.find('/');
    std::string host_str;
    std::string path_str;
    
    if (path_start == std::string::npos) {
        host_str = remaining;
        path_str = "/";
    } else {
        host_str = remaining.substr(0, path_start);
        path_str = remaining.substr(path_start);
    }
    
    size_t port_start = host_str.find(':');
    if (port_start != std::string::npos) {
        port = std::stoi(host_str.substr(port_start + 1));
        host_str = host_str.substr(0, port_start);
    }
    
    host = to_wide(host_str);
    path = to_wide(path_str);
}

} // anonymous namespace

WinTransport::WinTransport() {
    read_buffer_.resize(8192);
}

WinTransport::~WinTransport() {
    close();
}

bool WinTransport::connect(const std::string& url, const std::string& client_id) {
    std::wstring host;
    int port;
    std::wstring path;
    bool use_tls;
    
    try {
        parse_url(url, host, port, path, use_tls);
    } catch (const std::exception& e) {
        if (callbacks_.on_error) {
            callbacks_.on_error(e.what());
        }
        return false;
    }
    
    state_ = ConnectionState::Connecting;
    
    // Create session
    session_ = WinHttpOpen(
        L"Convex C++ Client",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );
    
    if (!session_) {
        if (callbacks_.on_error) {
            callbacks_.on_error("Failed to create WinHTTP session");
        }
        state_ = ConnectionState::Disconnected;
        return false;
    }
    
    // Connect to server
    connection_ = WinHttpConnect(session_, host.c_str(), port, 0);
    if (!connection_) {
        WinHttpCloseHandle(session_);
        session_ = nullptr;
        if (callbacks_.on_error) {
            callbacks_.on_error("Failed to connect to server");
        }
        state_ = ConnectionState::Disconnected;
        return false;
    }
    
    // Open request for WebSocket upgrade
    DWORD flags = use_tls ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(
        connection_,
        L"GET",
        path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags
    );
    
    if (!request) {
        WinHttpCloseHandle(connection_);
        WinHttpCloseHandle(session_);
        connection_ = nullptr;
        session_ = nullptr;
        if (callbacks_.on_error) {
            callbacks_.on_error("Failed to create request");
        }
        state_ = ConnectionState::Disconnected;
        return false;
    }
    
    // Set WebSocket upgrade
    if (!WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0)) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection_);
        WinHttpCloseHandle(session_);
        connection_ = nullptr;
        session_ = nullptr;
        if (callbacks_.on_error) {
            callbacks_.on_error("Failed to set WebSocket option");
        }
        state_ = ConnectionState::Disconnected;
        return false;
    }
    
    // Add custom header
    std::wstring header = L"Convex-Client: " + to_wide(client_id);
    WinHttpAddRequestHeaders(request, header.c_str(), -1, WINHTTP_ADDREQ_FLAG_ADD);
    
    // Send request
    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection_);
        WinHttpCloseHandle(session_);
        connection_ = nullptr;
        session_ = nullptr;
        if (callbacks_.on_error) {
            callbacks_.on_error("Failed to send request");
        }
        state_ = ConnectionState::Disconnected;
        return false;
    }
    
    // Receive response
    if (!WinHttpReceiveResponse(request, nullptr)) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection_);
        WinHttpCloseHandle(session_);
        connection_ = nullptr;
        session_ = nullptr;
        if (callbacks_.on_error) {
            callbacks_.on_error("Failed to receive response");
        }
        state_ = ConnectionState::Disconnected;
        return false;
    }
    
    // Complete WebSocket upgrade
    websocket_ = WinHttpWebSocketCompleteUpgrade(request, 0);
    WinHttpCloseHandle(request);
    
    if (!websocket_) {
        WinHttpCloseHandle(connection_);
        WinHttpCloseHandle(session_);
        connection_ = nullptr;
        session_ = nullptr;
        if (callbacks_.on_error) {
            callbacks_.on_error("WebSocket upgrade failed");
        }
        state_ = ConnectionState::Disconnected;
        return false;
    }
    
    state_ = ConnectionState::Connected;
    
    if (callbacks_.on_open) {
        callbacks_.on_open();
    }
    
    return true;
}

bool WinTransport::send(const std::string& message) {
    if (!websocket_ || state_ != ConnectionState::Connected) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(send_mutex_);
    
    DWORD err = WinHttpWebSocketSend(
        websocket_,
        WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
        (PVOID)message.data(),
        static_cast<DWORD>(message.size())
    );
    
    return err == ERROR_SUCCESS;
}

void WinTransport::close(const std::string&) {
    partial_message_.clear();
    if (websocket_) {
        state_ = ConnectionState::Closing;
        WinHttpWebSocketClose(
            websocket_,
            WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS,
            nullptr, 0
        );
        WinHttpCloseHandle(websocket_);
        websocket_ = nullptr;
    }
    
    if (connection_) {
        WinHttpCloseHandle(connection_);
        connection_ = nullptr;
    }
    
    if (session_) {
        WinHttpCloseHandle(session_);
        session_ = nullptr;
    }
    
    state_ = ConnectionState::Disconnected;
}

ConnectionState WinTransport::state() const {
    return state_;
}

void WinTransport::set_callbacks(TransportCallbacks callbacks) {
    callbacks_ = std::move(callbacks);
}

void WinTransport::poll(int) {
    if (!websocket_ || state_ != ConnectionState::Connected) {
        return;
    }
    
    DWORD bytes_read = 0;
    WINHTTP_WEB_SOCKET_BUFFER_TYPE buffer_type;
    
    DWORD err = WinHttpWebSocketReceive(
        websocket_,
        read_buffer_.data(),
        static_cast<DWORD>(read_buffer_.size()),
        &bytes_read,
        &buffer_type
    );
    
    if (err != ERROR_SUCCESS) {
        if (err == ERROR_WINHTTP_OPERATION_CANCELLED) {
            return;  // No data available
        }
        
        close();
        if (callbacks_.on_error) {
            callbacks_.on_error("WebSocket receive error");
        }
        return;
    }
    
    if (bytes_read > 0) {
        partial_message_.append(
            reinterpret_cast<char*>(read_buffer_.data()),
            bytes_read
        );
        
        // Check if message is complete
        if (buffer_type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE ||
            buffer_type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE) {
            if (callbacks_.on_message) {
                callbacks_.on_message(partial_message_);
            }
            partial_message_.clear();
        }
        else if (buffer_type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
            state_ = ConnectionState::Disconnected;
            if (callbacks_.on_close) {
                callbacks_.on_close(partial_message_);
            }
            close();
        }
    }
}

void WinTransport::run() {
    while (state_ == ConnectionState::Connected) {
        poll(100);
        Sleep(10);  // Small sleep to prevent busy loop
    }
}

} // namespace convex

#endif // CONVEX_PLATFORM_WINDOWS
