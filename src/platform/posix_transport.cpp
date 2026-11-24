#include "convex/platform/posix_transport.hpp"

#if defined(CONVEX_PLATFORM_POSIX)

#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <arpa/inet.h>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/sha.h>

#include <cstring>
#include <random>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace convex {

namespace {

// WebSocket opcodes
constexpr uint8_t WS_OPCODE_CONTINUATION = 0x00;
constexpr uint8_t WS_OPCODE_TEXT = 0x01;
constexpr uint8_t WS_OPCODE_BINARY = 0x02;
constexpr uint8_t WS_OPCODE_CLOSE = 0x08;
constexpr uint8_t WS_OPCODE_PING = 0x09;
constexpr uint8_t WS_OPCODE_PONG = 0x0A;

// Base64 encoding for WebSocket key
std::string base64_encode(const uint8_t* data, size_t len) {
    static const char* chars = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    
    std::string result;
    result.reserve((len + 2) / 3 * 4);
    
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = data[i] << 16;
        if (i + 1 < len) n |= data[i + 1] << 8;
        if (i + 2 < len) n |= data[i + 2];
        
        result += chars[(n >> 18) & 0x3F];
        result += chars[(n >> 12) & 0x3F];
        result += (i + 1 < len) ? chars[(n >> 6) & 0x3F] : '=';
        result += (i + 2 < len) ? chars[n & 0x3F] : '=';
    }
    
    return result;
}

// Generate random WebSocket key
std::string generate_ws_key() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint8_t> dis(0, 255);
    
    uint8_t key[16];
    for (int i = 0; i < 16; ++i) {
        key[i] = dis(gen);
    }
    
    return base64_encode(key, 16);
}

// Generate mask for WebSocket frame
void generate_mask(uint8_t mask[4]) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint8_t> dis(0, 255);
    for (int i = 0; i < 4; ++i) {
        mask[i] = dis(gen);
    }
}

} // anonymous namespace

PosixTransport::PosixTransport() {
    // Initialize OpenSSL
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
    
    read_buffer_.resize(8192);
}

PosixTransport::~PosixTransport() {
    close();
}

void PosixTransport::parse_url(
    const std::string& url,
    std::string& host,
    int& port,
    std::string& path,
    bool& use_tls
) {
    std::string remaining = url;
    
    // Parse scheme
    if (remaining.find("wss://") == 0) {
        use_tls = true;
        port = 443;
        remaining = remaining.substr(6);
    } else if (remaining.find("ws://") == 0) {
        use_tls = false;
        port = 80;
        remaining = remaining.substr(5);
    } else {
        throw std::runtime_error("Invalid WebSocket URL: " + url);
    }
    
    // Parse host and path
    size_t path_start = remaining.find('/');
    if (path_start == std::string::npos) {
        host = remaining;
        path = "/";
    } else {
        host = remaining.substr(0, path_start);
        path = remaining.substr(path_start);
    }
    
    // Parse port from host
    size_t port_start = host.find(':');
    if (port_start != std::string::npos) {
        port = std::stoi(host.substr(port_start + 1));
        host = host.substr(0, port_start);
    }
}

bool PosixTransport::connect(const std::string& url, const std::string& client_id) {
    std::string host;
    int port;
    std::string path;
    
    parse_url(url, host, port, path, use_tls_);
    
    state_ = ConnectionState::Connecting;
    
    // Resolve hostname
    struct addrinfo hints{}, *result;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    
    std::string port_str = std::to_string(port);
    int err = getaddrinfo(host.c_str(), port_str.c_str(), &hints, &result);
    if (err != 0) {
        if (callbacks_.on_error) {
            callbacks_.on_error("Failed to resolve host: " + host);
        }
        state_ = ConnectionState::Disconnected;
        return false;
    }
    
    // Create socket and connect
    socket_fd_ = socket(result->ai_family, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
        freeaddrinfo(result);
        if (callbacks_.on_error) {
            callbacks_.on_error("Failed to create socket");
        }
        state_ = ConnectionState::Disconnected;
        return false;
    }
    
    if (::connect(socket_fd_, result->ai_addr, result->ai_addrlen) < 0) {
        freeaddrinfo(result);
        ::close(socket_fd_);
        socket_fd_ = -1;
        if (callbacks_.on_error) {
            callbacks_.on_error("Failed to connect to " + host);
        }
        state_ = ConnectionState::Disconnected;
        return false;
    }
    
    freeaddrinfo(result);
    
    // Set up TLS if needed
    if (use_tls_) {
        ssl_ctx_ = SSL_CTX_new(TLS_client_method());
        if (!ssl_ctx_) {
            ::close(socket_fd_);
            socket_fd_ = -1;
            if (callbacks_.on_error) {
                callbacks_.on_error("Failed to create SSL context");
            }
            state_ = ConnectionState::Disconnected;
            return false;
        }
        
        ssl_ = SSL_new(static_cast<SSL_CTX*>(ssl_ctx_));
        SSL_set_fd(static_cast<SSL*>(ssl_), socket_fd_);
        SSL_set_tlsext_host_name(static_cast<SSL*>(ssl_), host.c_str());
        
        if (SSL_connect(static_cast<SSL*>(ssl_)) <= 0) {
            SSL_free(static_cast<SSL*>(ssl_));
            SSL_CTX_free(static_cast<SSL_CTX*>(ssl_ctx_));
            ::close(socket_fd_);
            ssl_ = nullptr;
            ssl_ctx_ = nullptr;
            socket_fd_ = -1;
            if (callbacks_.on_error) {
                callbacks_.on_error("SSL handshake failed");
            }
            state_ = ConnectionState::Disconnected;
            return false;
        }
    }
    
    // Perform WebSocket handshake
    if (!perform_handshake(host, path, client_id)) {
        close();
        return false;
    }
    
    // Set socket to non-blocking
    int flags = fcntl(socket_fd_, F_GETFL, 0);
    fcntl(socket_fd_, F_SETFL, flags | O_NONBLOCK);
    
    state_ = ConnectionState::Connected;
    
    if (callbacks_.on_open) {
        callbacks_.on_open();
    }
    
    return true;
}

bool PosixTransport::perform_handshake(
    const std::string& host,
    const std::string& path,
    const std::string& client_id
) {
    std::string ws_key = generate_ws_key();
    
    // Build HTTP upgrade request
    std::ostringstream request;
    request << "GET " << path << " HTTP/1.1\r\n";
    request << "Host: " << host << "\r\n";
    request << "Upgrade: websocket\r\n";
    request << "Connection: Upgrade\r\n";
    request << "Sec-WebSocket-Key: " << ws_key << "\r\n";
    request << "Sec-WebSocket-Version: 13\r\n";
    request << "Convex-Client: " << client_id << "\r\n";
    request << "\r\n";
    
    std::string req_str = request.str();
    if (!send_raw(reinterpret_cast<const uint8_t*>(req_str.data()), req_str.size())) {
        if (callbacks_.on_error) {
            callbacks_.on_error("Failed to send WebSocket handshake");
        }
        return false;
    }
    
    // Read response
    if (!read_http_response()) {
        if (callbacks_.on_error) {
            callbacks_.on_error("Failed to read WebSocket handshake response");
        }
        return false;
    }
    
    return true;
}

bool PosixTransport::read_http_response() {
    std::string response;
    char buf[1];
    
    // Read until we get \r\n\r\n
    while (true) {
        size_t bytes_read;
        if (!receive_raw(reinterpret_cast<uint8_t*>(buf), 1, bytes_read)) {
            return false;
        }
        if (bytes_read == 0) continue;
        
        response += buf[0];
        
        if (response.size() >= 4 && 
            response.substr(response.size() - 4) == "\r\n\r\n") {
            break;
        }
        
        if (response.size() > 8192) {
            return false;  // Response too large
        }
    }
    
    // Check for 101 Switching Protocols
    if (response.find("HTTP/1.1 101") == std::string::npos) {
        return false;
    }
    
    return true;
}

bool PosixTransport::send_raw(const uint8_t* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n;
        if (use_tls_ && ssl_) {
            n = SSL_write(static_cast<SSL*>(ssl_), data + sent, len - sent);
        } else {
            n = ::send(socket_fd_, data + sent, len - sent, 0);
        }
        
        if (n <= 0) {
            return false;
        }
        sent += n;
    }
    return true;
}

bool PosixTransport::receive_raw(uint8_t* buffer, size_t len, size_t& bytes_read) {
    ssize_t n;
    if (use_tls_ && ssl_) {
        n = SSL_read(static_cast<SSL*>(ssl_), buffer, len);
    } else {
        n = ::recv(socket_fd_, buffer, len, 0);
    }
    
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            bytes_read = 0;
            return true;
        }
        return false;
    }
    
    bytes_read = n;
    return true;
}

void PosixTransport::send_websocket_frame(uint8_t opcode, const std::string& payload) {
    std::lock_guard<std::mutex> lock(send_mutex_);
    
    std::vector<uint8_t> frame;
    
    // First byte: FIN + opcode
    frame.push_back(0x80 | opcode);
    
    // Second byte: MASK + length
    size_t len = payload.size();
    if (len < 126) {
        frame.push_back(0x80 | static_cast<uint8_t>(len));
    } else if (len < 65536) {
        frame.push_back(0x80 | 126);
        frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(len & 0xFF));
    } else {
        frame.push_back(0x80 | 127);
        for (int i = 7; i >= 0; --i) {
            frame.push_back(static_cast<uint8_t>((len >> (i * 8)) & 0xFF));
        }
    }
    
    // Mask
    uint8_t mask[4];
    generate_mask(mask);
    frame.insert(frame.end(), mask, mask + 4);
    
    // Masked payload
    for (size_t i = 0; i < payload.size(); ++i) {
        frame.push_back(static_cast<uint8_t>(payload[i]) ^ mask[i % 4]);
    }
    
    send_raw(frame.data(), frame.size());
}

bool PosixTransport::receive_websocket_frame() {
    // Read frame header
    uint8_t header[2];
    size_t bytes_read;
    
    if (!receive_raw(header, 2, bytes_read) || bytes_read < 2) {
        return bytes_read == 0;  // No data available is OK
    }
    
    bool fin = (header[0] & 0x80) != 0;
    uint8_t opcode = header[0] & 0x0F;
    bool masked = (header[1] & 0x80) != 0;
    uint64_t payload_len = header[1] & 0x7F;
    
    // Extended payload length
    if (payload_len == 126) {
        uint8_t ext[2];
        if (!receive_raw(ext, 2, bytes_read) || bytes_read < 2) {
            return false;
        }
        payload_len = (static_cast<uint64_t>(ext[0]) << 8) | ext[1];
    } else if (payload_len == 127) {
        uint8_t ext[8];
        if (!receive_raw(ext, 8, bytes_read) || bytes_read < 8) {
            return false;
        }
        payload_len = 0;
        for (int i = 0; i < 8; ++i) {
            payload_len = (payload_len << 8) | ext[i];
        }
    }
    
    // Mask (server shouldn't send masked frames, but handle it)
    uint8_t mask[4] = {0};
    if (masked) {
        if (!receive_raw(mask, 4, bytes_read) || bytes_read < 4) {
            return false;
        }
    }
    
    // Read payload
    std::string payload;
    payload.resize(payload_len);
    size_t total_read = 0;
    while (total_read < payload_len) {
        if (!receive_raw(
            reinterpret_cast<uint8_t*>(&payload[total_read]),
            payload_len - total_read,
            bytes_read
        )) {
            return false;
        }
        if (bytes_read == 0) {
            // Need to wait for more data
            struct pollfd pfd;
            pfd.fd = socket_fd_;
            pfd.events = POLLIN;
            ::poll(&pfd, 1, 100);
            continue;
        }
        total_read += bytes_read;
    }
    
    // Unmask if needed
    if (masked) {
        for (size_t i = 0; i < payload.size(); ++i) {
            payload[i] ^= mask[i % 4];
        }
    }
    
    // Handle frame based on opcode
    switch (opcode) {
        case WS_OPCODE_CONTINUATION:
            if (in_fragmented_message_) {
                partial_message_ += payload;
                if (fin) {
                    in_fragmented_message_ = false;
                    if (callbacks_.on_message) {
                        callbacks_.on_message(partial_message_);
                    }
                    partial_message_.clear();
                }
            }
            break;
            
        case WS_OPCODE_TEXT:
        case WS_OPCODE_BINARY:
            if (fin) {
                if (callbacks_.on_message) {
                    callbacks_.on_message(payload);
                }
            } else {
                in_fragmented_message_ = true;
                partial_message_ = payload;
            }
            break;
            
        case WS_OPCODE_CLOSE:
            state_ = ConnectionState::Closing;
            send_close_frame();
            if (callbacks_.on_close) {
                callbacks_.on_close(payload.size() >= 2 ? payload.substr(2) : "");
            }
            return false;
            
        case WS_OPCODE_PING:
            send_pong(payload);
            break;
            
        case WS_OPCODE_PONG:
            // Ignore pong
            break;
    }
    
    return true;
}

void PosixTransport::send_close_frame(uint16_t code, const std::string& reason) {
    std::string payload;
    payload += static_cast<char>((code >> 8) & 0xFF);
    payload += static_cast<char>(code & 0xFF);
    payload += reason;
    send_websocket_frame(WS_OPCODE_CLOSE, payload);
}

void PosixTransport::send_pong(const std::string& payload) {
    send_websocket_frame(WS_OPCODE_PONG, payload);
}

bool PosixTransport::send(const std::string& message) {
    if (state_ != ConnectionState::Connected) {
        return false;
    }
    
    send_websocket_frame(WS_OPCODE_TEXT, message);
    return true;
}

void PosixTransport::close(const std::string& reason) {
    if (state_ == ConnectionState::Connected) {
        state_ = ConnectionState::Closing;
        send_close_frame(1000, reason);
    }
    
    if (ssl_) {
        SSL_shutdown(static_cast<SSL*>(ssl_));
        SSL_free(static_cast<SSL*>(ssl_));
        ssl_ = nullptr;
    }
    
    if (ssl_ctx_) {
        SSL_CTX_free(static_cast<SSL_CTX*>(ssl_ctx_));
        ssl_ctx_ = nullptr;
    }
    
    if (socket_fd_ >= 0) {
        ::close(socket_fd_);
        socket_fd_ = -1;
    }
    
    state_ = ConnectionState::Disconnected;
}

ConnectionState PosixTransport::state() const {
    return state_;
}

void PosixTransport::set_callbacks(TransportCallbacks callbacks) {
    callbacks_ = std::move(callbacks);
}

void PosixTransport::poll(int timeout_ms) {
    if (socket_fd_ < 0 || state_ != ConnectionState::Connected) {
        return;
    }
    
    struct pollfd pfd;
    pfd.fd = socket_fd_;
    pfd.events = POLLIN;
    
    int ret = ::poll(&pfd, 1, timeout_ms);
    if (ret > 0 && (pfd.revents & POLLIN)) {
        if (!receive_websocket_frame()) {
            // Connection closed or error
            if (state_ == ConnectionState::Connected) {
                close();
                if (callbacks_.on_close) {
                    callbacks_.on_close("Connection closed");
                }
            }
        }
    } else if (ret < 0) {
        close();
        if (callbacks_.on_error) {
            callbacks_.on_error("Poll error");
        }
    }
}

void PosixTransport::run() {
    while (state_ == ConnectionState::Connected) {
        poll(100);
    }
}

} // namespace convex

#endif // CONVEX_PLATFORM_POSIX
