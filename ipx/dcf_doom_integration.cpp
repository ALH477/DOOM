// dcf_doom_integration.cpp (Updated for Cross-Platform: Wasm, ARM64, Android, iOS)
// Keep it simple: #ifdefs for platform quirks (e.g., no threads in Wasm single-threaded mode).
// For Wasm: Use Emscripten (emcc) with -s USE_PTHREADS=0 for single-thread (poll in main loop).
// For Android/iOS: Assume SDL2 for graphics/input (add via CMake); DCF uses native sockets/WS.
// No bloat: Optional #defines per platform. Polling adapts (threadless in Wasm).
// WebSocket layer unchanged, but add Emscripten WS support if needed.

#ifndef DCF_DOOM_INTEGRATION_H
#define DCF_DOOM_INTEGRATION_H

#include <chrono>
#include <vector>
#include <string>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>

// Platform detection (extended for new targets)
#if defined(__EMSCRIPTEN__)
    #define DCF_PLATFORM_WASM
    #include <emscripten/emscripten.h>
    #include <emscripten/websocket.h>  // For WS in browser
#elif defined(__ANDROID__)
    #define DCF_PLATFORM_ANDROID
    #include <android/log.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
#elif defined(__APPLE__) && defined(TARGET_OS_IPHONE)
    #define DCF_PLATFORM_IOS
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
#elif defined(__aarch64__) || defined(_M_ARM64)
    #define DCF_PLATFORM_ARM64
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
#elif defined(_WIN32) || defined(_WIN64)
    #define DCF_PLATFORM_WINDOWS
    #include <winsock2.h>
    #include <ws2tcpip.h>
#elif defined(__APPLE__)
    #define DCF_PLATFORM_MAC
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
#else
    #define DCF_PLATFORM_LINUX
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <netdb.h>
#endif

#include "doomnet.h"  // Original API
#include "messages.pb.h"  // Protobuf
#include "services.grpc.pb.h"  // gRPC

#include <grpcpp/grpcpp.h>
#include <google/protobuf/util/json_util.h>

// For WebSocket: websocket++ for non-Wasm; Emscripten WS for Wasm
#ifndef DCF_PLATFORM_WASM
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <websocketpp/client.hpp>
#endif

// Defines: Transports (enable one)
#define DCF_TRANSPORT_GRPC  // Default for most
// #define DCF_TRANSPORT_UDP
// #define DCF_TRANSPORT_WEBSOCKET  // Good for Wasm/browser
#define DCF_P2P_REDUNDANCY  // Optional

// Wasm adaptations: No threads, poll in main loop via emscripten_set_main_loop
#ifdef DCF_PLATFORM_WASM
#undef DCF_P2P_REDUNDANCY  // No threads
#endif

// Global unchanged

// DCFQueue/peer unchanged, but add WS handle for Emscripten
#ifdef DCF_PLATFORM_WASM
struct DCFPeer {
    std::string address;
    int port;
    EM_BOOL ws_connected = false;  // Wasm WS state
    // No stub/ws_hdl; use emscripten_websocket_send_binary
    bool active = true;
    std::chrono::steady_clock::time_point last_heartbeat;
};
#endif

class DCFNetworking {
public:
    DCFNetworking() : running_(false) {}
    ~DCFNetworking() { Shutdown(); }

    void Init(int argc, char** argv) {
        ParseArgs(argc, argv);

#if defined(DCF_PLATFORM_WINDOWS)
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2,2), &wsaData);
#elif defined(DCF_PLATFORM_ANDROID) || defined(DCF_PLATFORM_IOS)
        // Mobile init (e.g., SDL_Init for graphics if used)
#endif

#ifdef DCF_TRANSPORT_GRPC
        channel_ = grpc::CreateChannel(host_ + ":" + std::to_string(port_), grpc::InsecureChannelCredentials());
        stub_ = DCF::DCFService::NewStub(channel_);
#elif defined(DCF_TRANSPORT_WEBSOCKET)
#ifdef DCF_PLATFORM_WASM
        InitWasmWebSocket();
#else
        InitWebSocket();  // Unchanged
#endif
#endif

        running_ = true;
#ifndef DCF_PLATFORM_WASM
        poll_thread_ = std::thread(&DCFNetworking::PollLoop, this);
#ifdef DCF_P2P_REDUNDANCY
        redundancy_thread_ = std::thread(&DCFNetworking::RedundancyLoop, this);
#endif
#else
        emscripten_set_main_loop_arg([](void* arg) { static_cast<DCFNetworking*>(arg)->PollLoopIteration(); }, this, 1000, 1);  // 1ms loop
#endif
    }

    void Shutdown() {
        running_ = false;
#ifndef DCF_PLATFORM_WASM
        if (poll_thread_.joinable()) poll_thread_.join();
#ifdef DCF_P2P_REDUNDANCY
        if (redundancy_thread_.joinable()) redundancy_thread_.join();
#endif
#else
        emscripten_cancel_main_loop();
#endif
#ifdef DCF_TRANSPORT_WEBSOCKET
#ifdef DCF_PLATFORM_WASM
        // Close Wasm WS
#else
        ws_server_.stop();
        ws_client_.stop();
#endif
#endif
#if defined(DCF_PLATFORM_WINDOWS)
        WSACleanup();
#endif
    }

    // Send/Receive unchanged

private:
    void ParseArgs(int argc, char** argv) {
        // Unchanged; add mobile-specific if needed (e.g., from JNI on Android)
    }

#ifdef DCF_TRANSPORT_WEBSOCKET
#ifdef DCF_PLATFORM_WASM
    void InitWasmWebSocket() {
        // Emscripten WS init for peers
        for (auto& peer : peers_) {
            std::string uri = "ws://" + peer.address + ":" + std::to_string(peer.port);
            EM_WEBSOCKET_INIT_DATA init_data = {0};
            EM_BOOL result = emscripten_websocket_new(uri.c_str(), &init_data, &peer.ws_connected);
            // Handle connect callback if needed
        }
    }

    // Wasm send: emscripten_websocket_send_binary
    void WasmWSSend(const std::string& serialized, int remote_node) {
        for (auto& peer : peers_) {
            if (std::stoi(peer.address) == remote_node && peer.ws_connected) {
                // emscripten_websocket_send_binary(peer.ws_connected, serialized.data(), serialized.size());
                break;
            }
        }
    }
#else
    // Non-Wasm WS init unchanged
#endif
#endif

    void PollLoop() {
        while (running_) {
            PollLoopIteration();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    void PollLoopIteration() {
        // Shared poll logic (for thread or main loop)
        std::string serialized;
        {
            std::unique_lock<std::mutex> lock(out_queue_.mutex);
            if (!out_queue_.data.empty()) {
                serialized = out_queue_.data.front();
                out_queue_.data.pop();
            } else return;
        }

#ifdef DCF_TRANSPORT_GRPC
        // Unchanged
#elif defined(DCF_TRANSPORT_UDP)
        // Cross-platform UDP (use getaddrinfo for IPv4/6)
        // e.g., int sock = socket(AF_INET, SOCK_DGRAM, 0);
        // struct sockaddr_in addr; inet_pton(AF_INET, host_.c_str(), &addr.sin_addr);
        // sendto(sock, serialized.data(), serialized.size(), 0, (struct sockaddr*)&addr, sizeof(addr));
#elif defined(DCF_TRANSPORT_WEBSOCKET)
#ifdef DCF_PLATFORM_WASM
        WasmWSSend(serialized, doomcom.remotenode);
#else
        // Non-Wasm send unchanged
#endif
#endif
    }

    // RedundancyLoop: Skip in Wasm (no threads); call periodically in main loop if needed

    // Members unchanged
};

// Global/LaunchDOOM/NetISR unchanged

#endif
