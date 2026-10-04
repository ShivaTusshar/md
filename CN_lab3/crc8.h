// Author: Gogineni Satya Neeraj
#pragma once
#include <vector>
#include <cstdint>
#include <string>
#include <random>

using namespace std;

namespace CRC8 {
    const uint8_t GENERATOR = 0x07;

    inline uint8_t compute(const vector<uint8_t>& data) {
        uint8_t crc = 0;
        for (uint8_t byte : data) {
            crc ^= byte;
            for (int i = 0; i < 8; ++i) {
                if (crc & 0x80) {
                    crc = (crc << 1) ^ GENERATOR;
                } else {
                    crc = (crc << 1);
                }
            }
        }
        return crc;
    }

    inline uint8_t compute(const string& str) {
        vector<uint8_t> data(str.begin(), str.end());
        return compute(data);
    }

    inline bool verify(const vector<uint8_t>& frame) {
        if (frame.empty()) return false;
        uint8_t crc = 0;
        for (uint8_t byte : frame) {
            crc ^= byte;
            for (int i = 0; i < 8; ++i) {
                if (crc & 0x80) {
                    crc = (crc << 1) ^ GENERATOR;
                } else {
                    crc = (crc << 1);
                }
            }
        }
        return (crc == 0);
    }

    inline vector<uint8_t> create_tx(const string& raw_msg) {
        vector<uint8_t> tx(raw_msg.begin(), raw_msg.end());
        uint8_t remainder = compute(tx);
        tx.push_back(remainder);
        return tx;
    }

    inline string extract_message(const vector<uint8_t>& tx) {
        if (tx.size() <= 1) return "";
        return string(tx.begin(), tx.end() - 1);
    }

    inline vector<uint8_t> inject_errors(const vector<uint8_t>& data, double ber, int& flipped_bits) {
        flipped_bits = 0;
        if (ber <= 0.0) return data;
        static random_device rd;
        static mt19937 gen(rd());
        uniform_real_distribution<double> dis(0.0, 1.0);

        vector<uint8_t> corrupted = data;
        for (size_t i = 0; i < corrupted.size(); ++i) {
            for (int bit = 0; bit < 8; ++bit) {
                if (dis(gen) < ber) {
                    corrupted[i] ^= (1 << bit);
                    flipped_bits++;
                }
            }
        }
        return corrupted;
    }
}
