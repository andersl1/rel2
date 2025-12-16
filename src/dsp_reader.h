#pragma once

#include <vector>
#include <string>
#include <nlohmann/json.hpp>

struct DspData {
    std::vector<double> values;
    double total_investment;
    int smooth_value;
    std::string format;
    size_t n;
    
    // Helper to get descriptive name
    std::string GetName() const {
        return "Investment (S" + std::to_string(smooth_value) + ")";
    }
};

class DspReader {
public:
    static DspData Load(const std::string& filepath);

private:
    static std::vector<int64_t> DecodeSleb128(const std::vector<uint8_t>& buffer);
    static std::vector<int64_t> DeltaDecode(const std::vector<int64_t>& deltas);
};
