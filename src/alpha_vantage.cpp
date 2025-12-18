#include "alpha_vantage.h"
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <algorithm>
#include <map>

std::vector<double> AlphaVantage::FetchDaily(const std::string& symbol, const std::string& apiKey) {
    std::string url = "https://www.alphavantage.co/query";
    cpr::Response r = cpr::Get(cpr::Url{url},
                               cpr::Parameters{{"function", "TIME_SERIES_DAILY_ADJUSTED"},
                                               {"symbol", symbol},
                                               {"apikey", apiKey},
                                               {"outputsize", "full"}});

    if (r.status_code != 200) {
        throw std::runtime_error("HTTP Request Failed: " + std::to_string(r.status_code));
    }

    nlohmann::json j = nlohmann::json::parse(r.text);

    if (j.contains("Error Message")) {
        throw std::runtime_error("API Error: " + j["Error Message"].get<std::string>());
    }
    
    // Check for both possible keys just in case
    nlohmann::json series;
    if (j.contains("Time Series (Daily)")) {
        series = j["Time Series (Daily)"];
    } else {
         throw std::runtime_error("Invalid Response: No Time Series found.");
    }
    
    // Use a map to sort by date automatically (yyyy-mm-dd)
    std::map<std::string, double> sortedData;

    for (auto& [date, kv] : series.items()) {
        try {
            // Try Adjusted Close first
            if (kv.contains("5. adjusted close")) {
                 std::string closeStr = kv["5. adjusted close"];
                 sortedData[date] = std::stod(closeStr);
            } else if (kv.contains("4. close")) { // Fallback
                 std::string closeStr = kv["4. close"];
                 sortedData[date] = std::stod(closeStr);
            }
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
