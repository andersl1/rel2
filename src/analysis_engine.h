#pragma once

#include <vector>
#include <string>
#include <mutex>
#include "dsp_reader.h"

struct CachedStock {
    std::string symbol;
    std::string fullPath;
    std::vector<double> data;
};

struct SearchResult {
    std::string symbol;
    int offset; // Starting index in the target stock
    double pearson;
    double distance; // Hyperspherical distance
    const CachedStock* stockPtr; // Fast access to data
};

class AnalysisEngine {
public:
    // ... singleton ...
    static AnalysisEngine& GetInstance() {
        static AnalysisEngine instance;
        return instance;
    }

    size_t LoadLibrary(const std::string& rootPath);
    const std::vector<CachedStock>& GetCache() const { return m_Cache; }
    bool IsLoaded() const { return m_Loaded; }

    // Math Kernels
    static double CalculatePearson(const double* a, const double* b, size_t size);
    static double CalculateHyperspherical(const double* a, const double* b, size_t size);

    // Step 3: Search
    // Query: Must be at least 300 points.
    // Returns Top K matches from the library.
    std::vector<SearchResult> Search(const std::vector<double>& query, int topK = 10);

private:
    std::vector<CachedStock> m_Cache;
    bool m_Loaded = false;
};
