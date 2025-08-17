// dcf_doom_integration.cpp
// C++ Implementation of DeMoD Communications Framework (DCF) v5.0.0
// Integrated into id Tech 1 (DOOM) as a native netcode replacement.


// Include guards and essentials
#ifndef DCF_DOOM_INTEGRATION_H
#define DCF_DOOM_INTEGRATION_H

#include <chrono>
#include <thread>
#include <vector>
#include <string>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>

#include "doomnet.h"  // Original DOOM net headers for compatibility
#include "messages.pb.h"  // Generated from messages.proto
#include "services.grpc.pb.h"  // Generated from services.proto

#include <grpcpp/grpcpp.h>
#include <google/protobuf/util/json_util.h>

// Defines for DOOM-specific simplicity
#define DCF_MAX_NODES MAXNETNODES  // 8
#define DCF_PACKET_SIZE 512  // Match original
#define DCF_DEFAULT_PORT 50051  // gRPC default
#define DCF_TRANSPORT_GRPC  // Default to gRPC; undef and def DCF_TRANSPORT_UDP for UDP, etc.
#define DCF_P2P_REDUNDANCY  // Enable self-healing P2P

// Forward declarations
class DCFNetworking;

// Global doomcom for compatibility - but internally use DCF
extern doomcom_t doomcom;
extern int vectorishooked;
extern void interrupt (*olddoomvect)(void);

// DCF Message Queue for async handling (replaces original queues)
struct DCFQueue {
    std::queue<std::string> data;  // Serialized protobuf strings
    std::mutex mutex;
    std::condition_variable cv;
    size_t head = 0, tail = 0;  // For overflow checks
};

// DCF Peer structure for P2P
struct DCFPeer {
    std::string address;
    int port;
    std::unique_ptr<DCF::DCFService::Stub> stub;  // For gRPC
    bool active = true;
    std::chrono::steady_clock::time_point last_heartbeat;
};

// Core DCF Networking class - modular, but minimal for DOOM
class DCFNetworking {
public:
    DCFNetworking() : running_(false) {}
    ~DCFNetworking() { Shutdown(); }

    // Init replaces InitPort/GetUart
    void Init(int argc, char** argv) {
        ParseArgs(argc, argv);
        if (transport_ == "grpc") {
            channel_ = grpc::CreateChannel(host_ + ":" + std::to_string(port_), grpc::InsecureChannelCredentials());
            stub_ = DCF::DCFService::NewStub(channel_);
        }
        // Start polling thread (replaces ISR)
        running_ = true;
        poll_thread_ = std::thread(&DCFNetworking::PollLoop, this);
#ifdef DCF_P2P_REDUNDANCY
        redundancy_thread_ = std::thread(&DCFNetworking::RedundancyLoop, this);
#endif
    }

    // Shutdown replaces ShutdownPort
    void Shutdown() {
        running_ = false;
        if (poll_thread_.joinable()) poll_thread_.join();
#ifdef DCF_P2P_REDUNDANCY
        if (redundancy_thread_.joinable()) redundancy_thread_.join();
#endif
    }

    // Send replaces WritePacket
    void Send(const char* buffer, int len, int remote_node) {
        DCF::DCFMessage msg;
        msg.set_data(std::string(buffer, len));
        msg.set_recipient(std::to_string(remote_node));
        msg.set_sender(std::to_string(doomcom.consoleplayer));
        msg.set_timestamp(std::chrono::system_clock::now().time_since_epoch().count());

        std::string serialized;
        msg.SerializeToString(&serialized);

        // Enqueue for async send
        {
            std::lock_guard<std::mutex> lock(out_queue_.mutex);
            out_queue_.data.push(serialized);
        }
        out_queue_.cv.notify_one();
    }

    // Receive replaces ReadPacket
    bool Receive(char* buffer, int& len, int& remote_node) {
        std::unique_lock<std::mutex> lock(in_queue_.mutex);
        if (in_queue_.cv.wait_for(lock, std::chrono::milliseconds(1), [this] { return !in_queue_.data.empty(); })) {
            std::string serialized = in_queue_.data.front();
            in_queue_.data.pop();

            DCF::DCFMessage msg;
            if (msg.ParseFromString(serialized)) {
                len = msg.data().size();
                memcpy(buffer, msg.data().c_str(), len);
                remote_node = std::stoi(msg.sender());
                return true;
            }
        }
        return false;
    }

private:
    void ParseArgs(int argc, char** argv) {
        // Simple arg parsing, replace CheckParm
        for (int i = 1; i < argc; ++i) {
            if (std::string(argv[i]) == "-port") port_ = std::stoi(argv[i+1]);
            else if (std::string(argv[i]) == "-host") host_ = argv[i+1];
            else if (std::string(argv[i]) == "-transport") transport_ = argv[i+1];
            else if (std::string(argv[i]) == "-peer") peers_.emplace_back(DCFPeer{argv[i+1], port_, nullptr, true, std::chrono::steady_clock::now()});
        }
        if (port_ == 0) port_ = DCF_DEFAULT_PORT;
        if (host_.empty()) host_ = "localhost";
        if (transport_.empty()) transport_ = "grpc";
    }

    void PollLoop() {
        while (running_) {
            // Outbound: Send queued messages
            std::string serialized;
            {
                std::unique_lock<std::mutex> lock(out_queue_.mutex);
                if (out_queue_.cv.wait_for(lock, std::chrono::milliseconds(1), [this] { return !out_queue_.data.empty(); })) {
                    serialized = out_queue_.data.front();
                    out_queue_.data.pop();
                } else continue;
            }

#ifdef DCF_TRANSPORT_GRPC
            DCF::DCFMessage req, resp;
            req.ParseFromString(serialized);
            grpc::ClientContext ctx;
            stub_->SendMessage(&ctx, req, &resp);
            // Handle response if needed (for DOOM, often fire-and-forget)
#endif

            // Inbound: Poll for incoming (simulate ISR)
            // For gRPC, use async streams if needed; here simple poll
            // TODO: Implement UDP/TCP polling if defined

            std::this_thread::sleep_for(std::chrono::milliseconds(1));  // Low-latency poll
        }
    }

#ifdef DCF_P2P_REDUNDANCY
    void RedundancyLoop() {
        while (running_) {
            auto now = std::chrono::steady_clock::now();
            for (auto& peer : peers_) {
                if (std::chrono::duration_cast<std::chrono::seconds>(now - peer.last_heartbeat) > std::chrono::seconds(10)) {
                    peer.active = false;
                    // Reroute to another peer
                    printf("DCF: Peer %s inactive, rerouting.\n", peer.address.c_str());
                } else {
                    // Send heartbeat
                    Send("HEARTBEAT", 9, std::stoi(peer.address));  // Simplified
                    peer.last_heartbeat = now;
                }
            }
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }
#endif

    std::atomic<bool> running_;
    std::thread poll_thread_, redundancy_thread_;
    DCFQueue in_queue_, out_queue_;
    std::string host_, transport_;
    int port_ = 0;
    std::vector<DCFPeer> peers_;

    // gRPC specifics
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<DCF::DCFService::Stub> stub_;
};

// Global instance for native feel
DCFNetworking* g_dcf_net = nullptr;

// Replacement for LaunchDOOM
void LaunchDOOM() {
    if (!g_dcf_net) {
        g_dcf_net = new DCFNetworking();
        g_dcf_net->Init(myargc, myargv);
    }

    // Original logic, but with DCF
    // Set up doomcom as before, but DCF handles comms
    // Spawn DOOM process (original spawnv)
    // ...
    // On return, shutdown
    g_dcf_net->Shutdown();
    delete g_dcf_net;
    g_dcf_net = nullptr;
}

// Replacement for NetISR (now polled, but called as interrupt if needed)
void interrupt NetISR() {
    if (doomcom.command == CMD_SEND) {
        g_dcf_net->Send((const char*)&doomcom.data, doomcom.datalength, doomcom.remotenode);
    } else if (doomcom.command == CMD_GET) {
        int len;
        if (g_dcf_net->Receive((char*)&doomcom.data, len, doomcom.remotenode)) {
            doomcom.datalength = len;
        } else {
            doomcom.remotenode = -1;
        }
    }
}

// Hook vector as original, but set to NetISR
// In main/init, replace old init with DCF init

#endif  // DCF_DOOM_INTEGRATION_H
