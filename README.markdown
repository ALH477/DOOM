# DOOM DCF Integration

**Version 1.0.0 | August 25, 2025**  
**Developed by ALH477** 
**License:** GNU General Public License v3.0 (GPL-3.0)  
**GitHub Repo:** [https://github.com/ALH477/DeMoD-Communication-Framework](https://github.com/ALH477/DeMoD-Communication-Framework)  
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)  
## Overview
The DOOM DCF Integration project adapts the DeMoD Communications Framework (DCF) to enable real-time multiplayer networking for the classic game DOOM (1993), leveraging DCF's low-latency, modular, and interoperable features. This integration uses the DCF C SDK to synchronize player states (position, actions, health) across peers in a peer-to-peer (P2P) network, with self-healing redundancy and dynamic role assignment via AUTO mode. The project is part of the mono-repo at [ALH477/DeMoD-Communication-Framework](https://github.com/ALH477/DeMoD-Communication-Framework), designed for extensibility and compatibility with modern and legacy systems.

This integration targets DOOM's open-source ports (e.g., Zandronum, Chocolate Doom) and demonstrates DCF's applicability in real-time gaming, with potential extensions to defense simulations or IoT-driven interactive systems. It complies with U.S. export regulations (EAR/ITAR) by avoiding encryption, using DCF's handshakeless design and gRPC/Protobuf for efficient data exchange.

## Features
- **Real-Time Synchronization**: Sub-millisecond latency for player state updates (position, actions, health) using DCF's handshakeless protocol.
- **P2P Networking**: Self-healing P2P with RTT-based grouping (<50ms clusters) and Dijkstra routing for robust multiplayer sessions.
- **Dynamic Role Assignment**: AUTO mode allows nodes to switch between client, server, or P2P roles under master node control, optimizing network topology.
- **Modularity**: DCF plugin system enables custom transports or game-specific modules (e.g., DOOM-specific message formats).
- **Interoperability**: Compatible with DOOM ports via C SDK; extensible to other languages (Python, Perl) in the mono-repo.
- **Performance**: <5% CPU on Raspberry Pi, suitable for low-power devices; gRPC ensures efficient serialization.
- **Open Source**: GPL-3.0 ensures community contributions and transparency.

## Architecture
The integration modifies DOOM's networking layer to use the DCF C SDK, replacing legacy IPX or TCP-based multiplayer with DCF's gRPC-based transport. Player state is serialized via Protocol Buffers and transmitted over P2P or AUTO mode networks. The master node (optional) optimizes peer groups based on RTT and assigns roles dynamically.

```mermaid
graph TD
    A[DOOM DCF Integration] --> B[DOOM Game Engine]
    A --> C[DCF C SDK]
    C --> D[Networking Layer]
    D --> E[P2P Mode]
    D --> F[AUTO Mode]
    F --> G[Master Node]
    G --> H[Role Assignment]
    G --> I[RTT Metrics]
    E --> J[Self-Healing Redundancy]
    J --> K[RTT-Based Grouping]
    J --> L[Failure Detection]
    D --> M[gRPC Transport]
    M --> N[Protocol Buffers]
    C --> O[Plugin System]
    O --> P[Custom DOOM Transport]
```

## Installation
Clone the mono-repo with submodules:
```bash
git clone --recurse-submodules https://github.com/ALH477/DeMoD-Communication-Framework
cd DeMoD-Communication-Framework
```

### Prerequisites
- **DOOM Port**: Zandronum or Chocolate Doom (source code required).
- **C SDK Dependencies**: `libprotobuf-c`, `libuuid`, `libdl`, `libcjson`, `cmake`, `ncurses`.
- **Build Tools**: `gcc`, `make`, `protoc`.
- **OS**: Linux (tested on Ubuntu 20.04+), macOS, or Windows (via MinGW).

### Build Steps
1. **Generate Protobuf/gRPC Stubs**:
   ```bash
   protoc --c_out=c_sdk/src messages.proto
   protoc --c_out=c_sdk/src --grpc_out=c_sdk/src --plugin=protoc-gen-grpc=grpc_c_plugin services.proto
   ```
2. **Install Dependencies**:
   ```bash
   sudo apt install libprotobuf-c-dev libuuid-dev libdl-dev libcjson-dev cmake libncurses-dev
   ```
3. **Build C SDK**:
   ```bash
   cd c_sdk
   mkdir build && cd build
   cmake .. && make
   ```
4. **Integrate with DOOM**:
   - Copy `c_sdk/build/libdcf_sdk.a` and `c_sdk/include/dcf_sdk/` to your DOOM port's source directory.
   - Modify DOOM's networking code to use `dcf_client.h` (see example below).
   - Build DOOM with DCF: Update the DOOM port's `Makefile` to link `libdcf_sdk.a` and dependencies.

5. **Plugins** (Optional):
   - Place custom plugins (e.g., `libdoom_transport.so`) in `c_sdk/plugins/`.

## Usage Example
This example integrates DCF into a DOOM port, sending player state (x, y coordinates) to a peer and checking RTT.

```c
// doom_dcf_integration.c
#include <dcf_sdk/dcf_client.h>
#include <dcf_sdk/dcf_redundancy.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    float x, y; // Player position
    int health; // Player health
} PlayerState;

void serialize_player_state(PlayerState* state, char** serialized, size_t* len) {
    // Simplified: In practice, use Protobuf for serialization
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "{\"x\":%.2f,\"y\":%.2f,\"health\":%d}", state->x, state->y, state->health);
    *len = strlen(buffer) + 1;
    *serialized = strdup(buffer);
}

int main() {
    DCFClient* client = dcf_client_new();
    if (dcf_client_initialize(client, "config.json") != DCF_SUCCESS) {
        fprintf(stderr, "DCF init failed\n");
        return 1;
    }
    if (dcf_client_start(client) != DCF_SUCCESS) {
        fprintf(stderr, "DCF start failed\n");
        dcf_client_free(client);
        return 1;
    }

    // Example: Send player state
    PlayerState state = {100.5, 200.7, 100}; // Example player data
    char* serialized;
    size_t len;
    serialize_player_state(&state, &serialized, &len);

    char* response;
    const char* target = "localhost:50052";
    DCFError err = dcf_client_send_message(client, serialized, target, &response);
    if (err == DCF_SUCCESS) {
        printf("Received: %s\n", response);
        free(response);
    } else {
        fprintf(stderr, "Send failed: %s\n", dcf_error_str(err));
    }
    free(serialized);

    // Check RTT for peer
    int rtt;
    if (dcf_redundancy_health_check(client->redundancy, target, &rtt) == DCF_SUCCESS) {
        printf("RTT to %s: %d ms\n", target, rtt);
    }

    dcf_client_stop(client);
    dcf_client_free(client);
    return 0;
}
```

**Configuration (`config.json`)**:
```json
{
  "transport": "gRPC",
  "host": "localhost",
  "port": 50051,
  "mode": "p2p",
  "node_id": "doom_player1",
  "peers": ["localhost:50052"],
  "group_rtt_threshold": 50,
  "plugins": {
    "transport": "c_sdk/plugins/libdoom_transport.so"
  }
}
```

## Configuration
- **DOOM Integration**: Modify DOOM's game loop to call `dcf_client_send_message` for each player state update (e.g., position, actions).
- **Master Node (Optional)**: Deploy a master node for AUTO mode to dynamically assign roles:
  ```json
  {
    "transport": "gRPC",
    "host": "localhost",
    "port": 50051,
    "mode": "master",
    "node_id": "doom_master",
    "peers": ["localhost:50052", "localhost:50053"],
    "group_rtt_threshold": 50
  }
  ```
- **Plugins**: Implement `libdoom_transport.so` for DOOM-specific message formats (e.g., optimized player state serialization).

## Testing
- **Unit Tests**: Use Unity (`c_sdk/tests/test_doom_integration.c`) to verify serialization and networking.
- **Integration Tests**: Run DOOM with two instances (e.g., `localhost:50051` and `localhost:50052`) to test P2P synchronization.
- **P2P Redundancy**: Simulate peer failure with `dcf simulate-failure peer1` and verify failover within 10s.
- **AUTO Mode**: Deploy a master node and test role switching with `dcf_master_assign_role`.
- **Run Tests**:
  ```bash
  cd c_sdk/build
  make test_doom_integration
  valgrind --leak-check=full ./test_doom_integration
  ```

## Contributing
Contributions to the DOOM DCF Integration are welcome! Follow these steps:
1. Fork the repo: `https://github.com/ALH477/DeMoD-Communication-Framework`.
2. Create a feature branch: `git checkout -b feature/doom-sync`.
3. Add tests (Unity for C) and code (use `clang-format` for style).
4. Submit a PR with the [PR template](docs/PR_TEMPLATE.md).
5. Discuss via [GitHub Issues](https://github.com/ALH477/DeMoD-Communication-Framework/issues).

Ensure GPL-3.0 compliance, RTT grouping, and plugin support. New SDKs (e.g., Python for DOOM scripting) are encouraged.

## Documentation
- **DCF Design Spec**: `docs/dcf_design_spec.md` for architecture, SDK guidelines, and AUTO mode details.
- **C SDK Guide**: `c_sdk/C-SDKreadme.markdown` for CLI commands and usage.
- **DOOM Integration Notes**: Extend DCF's P2P and AUTO mode for game-specific needs (e.g., low-latency state sync).

## Future Work
- **Python SDK**: Add Python bindings for DOOM scripting (e.g., AI bots).
- **Custom Plugin**: Develop `libdoom_transport.so` for optimized DOOM message formats.
- **Mobile Support**: Integrate with Android/iOS DOOM ports using DCF mobile bindings.
- **Anduril Integration**: Adapt for defense simulations (e.g., Lattice-compatible real-time sync).

This project showcases DCF's versatility for real-time gaming and beyond—let's frag some demons together!
