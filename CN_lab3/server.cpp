// Author: Karyampudi Komal
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <csignal>
#include <cstring>
#include <stdexcept>
#include <random>
#include "protocol.h"
#include "crc8.h"

using namespace std;

atomic<bool> g_running(true);
SOCKET g_server_sock = INVALID_SOCKET;

void handle_signal(int sig) {
    (void)sig;
    cout << "\n[Server] Shutdown signal received. Closing sockets..." << endl;
    g_running = false;
    if (g_server_sock != INVALID_SOCKET) {
        close_socket(g_server_sock);
        g_server_sock = INVALID_SOCKET;
    }
}

void client_handler(SOCKET client_sock, sockaddr_in client_addr, double ack_error_prob) {
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_addr.sin_addr), ip, INET_ADDRSTRLEN);
    int port = ntohs(client_addr.sin_port);
    cout << "[Server] Connected to client " << ip << ":" << port << endl;

    int last_delivered_seq = -1;

    while (g_running) {
        Header hdr;
        if (!recv_all(client_sock, &hdr, sizeof(hdr))) {
            break;
        }

        if (hdr.type == PKT_END) {
            cout << "[Server] Client " << ip << ":" << port << " disconnected gracefully." << endl;
            break;
        }

        if (hdr.type == PKT_DATA) {
            uint16_t len = ntohs(hdr.length);
            vector<uint8_t> tx(len);
            if (!recv_all(client_sock, tx.data(), len)) {
                break;
            }

            cout << "\n[Server] Received frame for seq " << (int)hdr.seq_num
                 << " (Total bytes: " << len << ")" << endl;

            bool valid = CRC8::verify(tx);
            Header response_hdr;
            memset(&response_hdr, 0, sizeof(response_hdr));
            response_hdr.seq_num = hdr.seq_num;
            response_hdr.length = 0;

            if (valid && (int)hdr.seq_num == last_delivered_seq) {
                cout << "[Server] Duplicate seq " << (int)hdr.seq_num
                     << " detected (previous ACK lost/corrupted). Discarding duplicate, re-sending ACK." << endl;
                response_hdr.type = PKT_ACK;
            } else if (valid) {
                string msg = CRC8::extract_message(tx);
                cout << "[Server] CRC-8 Check: PASSED (Divisible by G(x) = x^8+x^2+x+1)" << endl;
                cout << "[Server] Decoded Message: \"" << msg << "\"" << endl;
                response_hdr.type = PKT_ACK;
                last_delivered_seq = (int)hdr.seq_num;
            } else {
                cout << "[Server] CRC-8 Check: FAILED (Error detected! Not divisible by G(x))" << endl;
                response_hdr.type = PKT_NACK;
            }

            bool drop_ack = false;
            if (ack_error_prob > 0.0) {
                static thread_local mt19937 gen(random_device{}());
                uniform_real_distribution<double> dis(0.0, 1.0);
                double r = dis(gen);
                if (r < ack_error_prob / 2.0) {
                    cout << "[Channel] Simulating ACK/NACK drop (Triggering client timeout)..." << endl;
                    drop_ack = true;
                } else if (r < ack_error_prob) {
                    cout << "[Channel] Simulating ACK/NACK corruption (response garbled in transit)..." << endl;
                    response_hdr.type = 0xFF;
                }
            }

            if (!drop_ack) {
                if (response_hdr.type == PKT_ACK) {
                    cout << "[Server] Sending ACK for seq " << (int)hdr.seq_num << endl;
                } else if (response_hdr.type == PKT_NACK) {
                    cout << "[Server] Sending NACK for seq " << (int)hdr.seq_num << endl;
                } else {
                    cout << "[Server] Sending GARBLED response for seq " << (int)hdr.seq_num << endl;
                }
                if (!send_all(client_sock, &response_hdr, sizeof(response_hdr))) {
                    cerr << "[Server] Failed to send response to " << ip << ":" << port
                         << " (connection lost)." << endl;
                    break;
                }
            }
        }
    }
    close_socket(client_sock);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        cerr << "Usage: " << argv[0] << " <Server Port number>" << endl;
        return 1;
    }

    int port = 0;
    try {
        port = stoi(argv[1]);
    } catch (const exception&) {
        cerr << "Invalid port number: " << argv[1] << " (expected an integer between 1 and 65535)" << endl;
        return 1;
    }
    if (port < 1 || port > 65535) {
        cerr << "Port number out of range: " << port << " (expected 1-65535)" << endl;
        return 1;
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    init_networking();

    g_server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (g_server_sock == INVALID_SOCKET) {
        cerr << "Error creating socket" << endl;
        cleanup_networking();
        return 1;
    }

    int opt = 1;
    setsockopt(g_server_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(g_server_sock, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        cerr << "Bind failed on port " << port << endl;
        close_socket(g_server_sock);
        cleanup_networking();
        return 1;
    }

    if (listen(g_server_sock, 10) == SOCKET_ERROR) {
        cerr << "Listen failed" << endl;
        close_socket(g_server_sock);
        cleanup_networking();
        return 1;
    }

    cout << "=================================================" << endl;
    cout << " CRC-8 Concurrent Server (Stop-and-Wait ARQ)" << endl;
    cout << " Generator Polynomial G(x) = x^8 + x^2 + x + 1 (0x107)" << endl;
    cout << " Server listening on port " << port << endl;
    cout << "=================================================" << endl;

    double ack_error_prob = 0.0;
    cout << "Enter ACK/NACK error probability (0.0 to 1.0): " << flush;
    if (!(cin >> ack_error_prob) || ack_error_prob < 0.0) {
        cout << "[Server] No/invalid input - defaulting to 0.0 (no ACK/NACK errors)." << endl;
        ack_error_prob = 0.0;
    } else if (ack_error_prob > 1.0) {
        cout << "[Server] Probability greater than 1.0 - clamping to 1.0." << endl;
        ack_error_prob = 1.0;
    }
    string dummy;
    getline(cin, dummy);   // consume the trailing newline (same pattern as client)
    cout << "[Server] ACK/NACK error probability set to " << ack_error_prob << endl;

    while (g_running) {
        sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        SOCKET client_sock = accept(g_server_sock, (sockaddr*)&client_addr, &addr_len);
        if (client_sock == INVALID_SOCKET) {
            if (!g_running) break;
            continue;
        }

        thread t(client_handler, client_sock, client_addr, ack_error_prob);
        t.detach();
    }

    cleanup_networking();
    cout << "[Server] Server stopped gracefully." << endl;
    return 0;
}
