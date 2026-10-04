# Computer Networks Lab - Assignment 3 (Socket Programming)
## Application #8: Error Detection using Cyclic Redundancy Code (CRC-8)

### Team Members & Author Attributions
1. **Gogineni Satya Neeraj** - `crc8.h` (CRC-8 Generator polynomial $G(x) = x^8 + x^2 + x + 1$, modulo-2 division, verification, and BER error injection)
2. **Kunduru Shiva Tusshar** - `protocol.h` (Cross-platform socket abstraction, packet frames, header formats, network serialization)
3. **Karyampudi Komal** - `server.cpp` (Concurrent TCP server, multi-threaded client handling, CRC verification, ACK/NACK generation, graceful signal handling)
4. **Kasireddy Sai Chandra Kiran Naidu** - `client.cpp` (Stop-and-Wait ARQ client, frame construction $T(x)$, timer mechanism, retransmissions on NACK/timeout)

---

### Compilation Instructions

#### On Linux / macOS (GCC / Clang):
```bash
make
```
Or directly:
```bash
g++ -std=c++17 -Wall -Wextra -O2 -o server server.cpp -pthread
g++ -std=c++17 -Wall -Wextra -O2 -o client client.cpp -pthread
```

#### On Windows (MinGW / MSYS2 / MSVC):
```bash
g++ -std=c++17 -Wall -Wextra -O2 -o server.exe server.cpp -lws2_32
g++ -std=c++17 -Wall -Wextra -O2 -o client.exe client.cpp -lws2_32
```

---

### Execution Prototype

#### 1. Start Server:
```bash
./server <Server Port number>
# Example:
./server 8080
```

#### 2. Start Client:
```bash
./client <Server IP Address> <Server Port number>
# Example:
./client 127.0.0.1 8080
```

---

### System Architecture & Features
- **Generator Polynomial**: $G(x) = x^8 + x^2 + x + 1$ ($100000111_2$, Hex: `0x107`)
- **Stop-and-Wait ARQ**: Alternating sequence numbers (0 and 1) ensuring reliable, in-order delivery.
- **Error Detection**: Full modulo-2 division at receiver. If remainder is 0 $\to$ ACK, otherwise $\to$ NACK.
- **Error Simulation**: User-defined Bit Error Rate (BER $p \in [0.0, 1.0]$) to simulate random bit flips.
- **Timer & Retransmission**: Client maintains a 2-second timeout timer. Automatically retransmits $T(x)$ on receiving NACK or upon timeout.
- **Concurrent Server**: Multi-threaded architecture accepting multiple simultaneous client connections.
- **Graceful Termination**: Captures `SIGINT` (Ctrl+C) to release socket resources cleanly.
