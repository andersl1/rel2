#include "dsp_reader.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <zstd.h>
#include <filesystem>

// Helper to read Big Endian uint32
uint32_t ReadU32BE(std::ifstream& f) {
    unsigned char bytes[4];
    f.read(reinterpret_cast<char*>(bytes), 4);
    if (!f) throw std::runtime_error("Failed to read 4 bytes");
    return (static_cast<uint32_t>(bytes[0]) << 24) |
           (static_cast<uint32_t>(bytes[1]) << 16) |
           (static_cast<uint32_t>(bytes[2]) << 8) |
           static_cast<uint32_t>(bytes[3]);
}

DspData DspReader::Load(const std::string& filepath) {
    std::ifstream f(filepath, std::ios::binary);
    if (!f.is_open()) {
        throw std::runtime_error("Could not open file: " + filepath);
    }

    // 1. Read Metadata
    uint32_t meta_len = ReadU32BE(f);
    std::vector<char> meta_buf(meta_len);
    f.read(meta_buf.data(), meta_len);
    nlohmann::json meta_json = nlohmann::json::parse(meta_buf.begin(), meta_buf.end());

    int n = meta_json["n"];
    double total_investment = meta_json["total_investment"];
    int smooth_value = meta_json["smooth_value"];

    // 2. Read Compressed Part 1
    uint32_t c1_len = ReadU32BE(f);
    std::vector<char> c1_buf(c1_len);
    f.read(c1_buf.data(), c1_len);

    // 3. Read Compressed Part 2
    uint32_t c2_len = ReadU32BE(f);
    std::vector<char> c2_buf(c2_len);
    f.read(c2_buf.data(), c2_len);

    // 4. Decompress
    auto Decompress = [](const std::vector<char>& src) -> std::vector<uint8_t> {
        if (src.empty()) return {};
        unsigned long long const rSize = ZSTD_getFrameContentSize(src.data(), src.size());
        if (rSize == ZSTD_CONTENTSIZE_ERROR) throw std::runtime_error("Not a zstd file");
        if (rSize == ZSTD_CONTENTSIZE_UNKNOWN) throw std::runtime_error("Original size unknown");

        std::vector<uint8_t> dst(rSize);
        size_t const dSize = ZSTD_decompress(dst.data(), rSize, src.data(), src.size());
        if (ZSTD_isError(dSize)) throw std::runtime_error(std::string("ZSTD decompress error: ") + ZSTD_getErrorName(dSize));
        return dst;
    };

    std::vector<uint8_t> enc1 = Decompress(c1_buf);
    std::vector<uint8_t> enc2 = Decompress(c2_buf);

    // 5. Decode SLEB128 & Delta
    std::vector<int64_t> deltas1 = DecodeSleb128(enc1);
    std::vector<int64_t> deltas2 = DecodeSleb128(enc2);

    if (deltas1.size() != n || deltas2.size() != n) {
        throw std::runtime_error("Decoded count mismatch. Expected " + std::to_string(n) + 
                               ", got " + std::to_string(deltas1.size()) + " and " + std::to_string(deltas2.size()));
    }

    std::vector<int64_t> part1 = DeltaDecode(deltas1);
    std::vector<int64_t> part2 = DeltaDecode(deltas2);

    // 6. Reconstruct values
    DspData result;
    result.total_investment = total_investment;
    result.smooth_value = smooth_value;
    result.n = n;
    result.format = meta_json.value("format", "unknown");
    result.values.reserve(n);

    for (int i = 0; i < n; ++i) {
        int64_t scaled = part1[i] * 10000 + part2[i];
        double normalized = static_cast<double>(scaled) / 1e8;
        
        double val;
        // Check for zero investment (FRED data case)
        if (std::abs(total_investment) < 1e-9) {
            val = normalized;
        } else {
            // Standard case: Reverse log transformation
            // val = T * (exp(normalized) - 1)
            val = total_investment * (std::exp(normalized) - 1.0);
        }
        result.values.push_back(val);
    }

    return result;
}

std::vector<int64_t> DspReader::DecodeSleb128(const std::vector<uint8_t>& buffer) {
    std::vector<int64_t> result;
    size_t idx = 0;
    while (idx < buffer.size()) {
        int64_t val = 0;
        int shift = 0;
        uint8_t byte;
        do {
            if (idx >= buffer.size()) throw std::runtime_error("Buffer underflow");
            byte = buffer[idx++];
            val |= (static_cast<int64_t>(byte & 0x7F) << shift);
            shift += 7;
        } while (byte & 0x80);

        // Sign extension
        if ((shift < 64) && (byte & 0x40)) {
            val |= (~0ULL << shift); 
        }
        result.push_back(val);
    }
    return result;
}

std::vector<int64_t> DspReader::DeltaDecode(const std::vector<int64_t>& deltas) {
    std::vector<int64_t> result;
    result.reserve(deltas.size());
    int64_t accum = 0;
    for (int64_t d : deltas) {
        accum += d;
        result.push_back(accum);
    }
    return result;
}
