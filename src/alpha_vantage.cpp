#include "alpha_vantage.h"
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <algorithm>
#include <map>

std::vector<double> AlphaVantage::FetchDaily(const std::string& symbol, const std::string& apiKey) {
    std::string url = "https://www.alphavantage.co/query";
    cpr::Response r = cpr::Get(cpr::Url{url},
                               cpr::Parameters{{"function", "TIME_SERIES_DAILY"},
                                               {"symbol", symbol},
                                               {"apikey", apiKey},
                                               {"outputsize", "full"}}); // 'compact' by default returns 100

    if (r.status_code != 200) {
        throw std::runtime_error("HTTP Request Failed: " + std::to_string(r.status_code));
    }

    nlohmann::json j = nlohmann::json::parse(r.text);

    if (j.contains("Error Message")) {
        throw std::runtime_error("API Error: " + j["Error Message"].get<std::string>());
    }
    if (j.contains("Note")) {
        // API rate limit or other note
        // Just log it or throw if empty?
        // throw std::runtime_error("API Note: " + j["Note"].get<std::string>());
    }

    if (!j.contains("Time Series (Daily)")) {
        throw std::runtime_error("Invalid Response: No Time Series found. Check Symbol/Key.");
    }

    auto series = j["Time Series (Daily)"];
    
    // Use a map to sort by date automatically (yyyy-mm-dd)
    std::map<std::string, double> sortedData;

    for (auto& [date, kv] : series.items()) {
        try {
            std::string closeStr = kv["4. close"];
            sortedData[date] = std::stod(closeStr);
        } catch (...) {
            continue;
        }
    }

    std::vector<double> results;
    results.reserve(sortedData.size());
    for (const auto& [date, price] : sortedData) {
        results.push_back(price);
    }

    return results;
}
