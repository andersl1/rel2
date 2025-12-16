#pragma once

#include <vector>
#include <string>
#include <optional>

class AlphaVantage {
public:
    // Fetches daily closing prices for the given symbol.
    // Returns a vector of prices (oldest to newest) if successful.
    // Returns std::nullopt or throws generic exception on failure.
    static std::vector<double> FetchDaily(const std::string& symbol, const std::string& apiKey);
};
