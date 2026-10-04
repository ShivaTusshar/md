// Author: Kasireddy Sai Chandra Kiran Naidu
#include <iostream>
#include <string>
#include <vector>
#include <iomanip>
#include <csignal>
#include <stdexcept> 
#include <chrono>
#include <thread>
#include "protocol.h"
#include "crc8.h"

using namespace std;

int main(int argc, char* argv[]) {
    if (argc < 3) {
        cerr << "Usage: " << argv[0] << " <Server IP Address> <Server Port number>" << endl;
        return 1;
    }

    string server_ip = argv[1];
    int port = 0;
    try {
        port = stoi(argv[2]);
    } catch (const exception&) {
        cerr << "Invalid port number: " << argv[2] << " (expected an integer between 1 and 65535)" << endl;
        return 1;
    }
    if (port < 1 || port > 65535) {
        cerr << "Port number out of range: " << port << " (expected 1-65535)" << endl;
        return 1;
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        cerr << "Error creating socket" << endl;
        cleanup_networking();
        return 1;
    }

    sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr) != 1) {
        cerr << "Invalid IP address: " << server_ip << endl;
        close_socket(sock);
        cleanup_networking();
        return 1;
    }

    if (connect(sock, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        cerr << "Connection to " << server_ip << ":" << port << " failed." << endl;
        close_socket(sock);
        cleanup_networking();
        return 1;
    }

    cout << "=================================================" << endl;
    cout << " CRC-8 Stop-and-Wait Client" << endl;
    cout << " Generator Polynomial G(x) = x^8 + x^2 + x + 1" << endl;
    cout << " Connected to " << server_ip << ":" << port << endl;
    cout << "=================================================" << endl;

    double ber = 0.0;
    cout << "Enter Bit Error Rate (BER) / Error Probability (0.0 to 1.0): " << flush;
    if (!(cin >> ber) || ber < 0.0) {
        cout << "[Client] No/invalid input - defaulting to 0.0 (no bit errors)." << endl;
        ber = 0.0;
    } else if (ber > 1.0) {
        cout << "[Client] BER greater than 1.0 - clamping to 1.0." << endl;
        ber = 1.0;
    }
    string dummy;
    getline(cin, dummy);

    uint8_t seq = 0;
    const int TIMEOUT_SEC = 2;
    const int MAX_RETRANSMISSIONS = 10;
    bool running = true; 

    while (running) {
        cout << "\nEnter message to send (or 'exit' to quit): " << flush;
        string raw_msg;
        if (!getline(cin, raw_msg) || raw_msg == "exit") {
            Header end_hdr;
            memset(&end_hdr, 0, sizeof(end_hdr));
            end_hdr.type = PKT_END;
            if (!send_all(sock, &end_hdr, sizeof(end_hdr))) {   // [FIX C1]
                cerr << "[Client] Failed to send END packet (connection lost)." << endl;
            }
            break;
        }

        if (raw_msg.empty()) continue;

        vector<uint8_t> tx_clean = CRC8::create_tx(raw_msg);
        uint8_t crc_val = tx_clean.back();

        cout << "[Client] Raw message: \"" << raw_msg << "\"" << endl;
        cout << "[Client] Computed CRC-8: 0x" << hex << uppercase << setw(2) 
             << setfill('0') << (int)crc_val << dec << endl;
        cout << "[Client] Generated T(x) size: " << tx_clean.size() << " bytes" << endl;

        int attempts = 0;
        bool delivered = false;

        while (!delivered && attempts < MAX_RETRANSMISSIONS) {
            attempts++;
            int flipped_bits = 0;
            vector<uint8_t> tx_to_send = CRC8::inject_errors(tx_clean, ber, flipped_bits);

            if (flipped_bits > 0) {
                cout << "[Channel] BER Injected " << flipped_bits << " bit error(s) into T(x)!" << endl;
            } else {
                cout << "[Channel] Transmission clean (no bit flips)" << endl;
            }

            Header data_hdr;
            data_hdr.type = PKT_DATA;
            data_hdr.seq_num = seq;
            data_hdr.length = htons((uint16_t)tx_to_send.size());

            cout << "[Client] Sending frame (Seq: " << (int)seq 
                 << ", Attempt: " << attempts << ")..." << endl;

            if (!send_all(sock, &data_hdr, sizeof(data_hdr)) ||
                !send_all(sock, tx_to_send.data(), tx_to_send.size())) {
                cerr << "[Client] Send failed - server connection lost. Exiting." << endl;
                running = false;
                break;
            }

            cout << "[Client] Timer started (" << TIMEOUT_SEC << "s timeout). Waiting for ACK/NACK..." << endl;

            int sel_res = wait_for_read(sock, TIMEOUT_SEC);
            if (sel_res <= 0) {
                cout << "[Timer] Timeout! No ACK/NACK received within " << TIMEOUT_SEC << "s. Retransmitting..." << endl;
                continue;
            }

            Header resp_hdr;
            if (!recv_all(sock, &resp_hdr, sizeof(resp_hdr))) {
                cerr << "[Client] Server connection lost. Exiting." << endl;
                running = false;
                break;
            }

            if (resp_hdr.type == PKT_ACK && resp_hdr.seq_num == seq) {
                cout << "[Client] ACK received for Seq " << (int)seq << "! Frame delivered successfully." << endl;
                delivered = true;
                seq = 1 - seq;
            } else if (resp_hdr.type == PKT_NACK) {
                cout << "[Client] NACK received for Seq " << (int)seq << " (CRC check failed at receiver). Retransmitting..." << endl;
            } else {
                cout << "[Client] Unexpected response or corrupted ACK. Retransmitting..." << endl;
            }
        }

        if (!delivered && running) {
            cout << "[Client] Failed to deliver frame after " << MAX_RETRANSMISSIONS << " attempts." << endl;
        }
    }

    close_socket(sock);
    cleanup_networking();
    cout << "[Client] Terminated gracefully." << endl;
    return 0;
}
