#include "analysis_engine.h"
#include "dsp_library.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <omp.h>

// ... (Previous content)

// Helper for "Fred" filter
static bool ContainsFred(const std::string& path) {
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return lower.find("fred") != std::string::npos;
}

size_t AnalysisEngine::LoadLibrary(const std::string& rootPath) {
    if (m_Loaded) return m_Cache.size();

    std::vector<DspFileEntry> entries = DspLibrary::Scan(rootPath);
    std::cout << "AnalysisEngine: Scanned " << entries.size() << " candidates." << std::endl;

    m_Cache.reserve(entries.size());
    std::mutex cacheMutex;

    #pragma omp parallel for schedule(dynamic)
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        const auto& entry = entries[i];
        if (ContainsFred(entry.fullPath)) continue;

        try {
            DspData data = DspReader::Load(entry.fullPath);
            if (data.values.size() < 400) continue;

            CachedStock stock;
            stock.symbol = entry.displayName; 
            stock.fullPath = entry.fullPath;
            stock.data = std::move(data.values);

            std::lock_guard<std::mutex> lock(cacheMutex);
            m_Cache.push_back(std::move(stock));
        } catch (...) { }
    }

    m_Loaded = true;
    std::cout << "AnalysisEngine: Loaded " << m_Cache.size() << " valid stocks." << std::endl;
    return m_Cache.size();
}

double AnalysisEngine::CalculatePearson(const double* a, const double* b, size_t size) {
    if (size == 0) return 0.0;
    double sum_a = 0.0, sum_b = 0.0, sum_ab = 0.0;
    double sum_sq_a = 0.0, sum_sq_b = 0.0;
    for (size_t i = 0; i < size; ++i) {
        double val_a = a[i];
        double val_b = b[i];
        sum_a += val_a; sum_b += val_b;
        sum_ab += val_a * val_b;
        sum_sq_a += val_a * val_a; sum_sq_b += val_b * val_b;
    }
    double num = (static_cast<double>(size) * sum_ab) - (sum_a * sum_b);
    double den = std::sqrt((static_cast<double>(size) * sum_sq_a - sum_a * sum_a) * 
                           (static_cast<double>(size) * sum_sq_b - sum_b * sum_b));
    if (den == 0.0) return 0.0;
    return num / den;
}

double AnalysisEngine::CalculateHyperspherical(const double* a, const double* b, size_t size) {
    if (size == 0) return 3.14159; 
    double dot = 0.0, norm_a = 0.0, norm_b = 0.0;
    for (size_t i = 0; i < size; ++i) {
        double val_a = a[i];
        double val_b = b[i];
        dot += val_a * val_b;
        norm_a += val_a * val_a;
        norm_b += val_b * val_b;
    }
    if (norm_a == 0.0 || norm_b == 0.0) return 1.570796;
    double cosine = dot / (std::sqrt(norm_a) * std::sqrt(norm_b));
    if (cosine > 1.0) cosine = 1.0;
    if (cosine < -1.0) cosine = -1.0;
    return std::acos(cosine);
}

std::vector<SearchResult> AnalysisEngine::Search(const std::vector<double>& query, int topK) {
    std::vector<SearchResult> results;
    std::cout << "AnalysisEngine: Starting search on " << m_Cache.size() << " stocks. Query size: " << query.size() << std::endl;

    // Use first 300 points of query as the pattern
    const size_t patternSize = 300;
    const std::vector<double> pattern(query.begin(), query.begin() + patternSize);

    // Thread-local storage for gathering results
    std::vector<std::vector<SearchResult>> threadResults(omp_get_max_threads());

    #pragma omp parallel for schedule(dynamic)
    for (int i = 0; i < static_cast<int>(m_Cache.size()); ++i) {
        // ... (loop content same) ...
        const auto& stock = m_Cache[i];
        const int searchLimit = static_cast<int>(stock.data.size()) - 100 - static_cast<int>(patternSize);

        if (searchLimit < 0) continue; 

        double bestPearson = -1.0;
        int bestOffset = -1;

        for (int j = 0; j <= searchLimit; ++j) {
            double p = CalculatePearson(pattern.data(), stock.data.data() + j, patternSize);
            if (p > bestPearson) {
                bestPearson = p;
                bestOffset = j;
            }
        }

        if (bestOffset != -1) {
            // Optimization: Only store if pearson is somewhat decent? e.g. > 0.0
            // But user wants Top 10, even if bad.
            double dist = CalculateHyperspherical(pattern.data(), stock.data.data() + bestOffset, patternSize);
            
            SearchResult res;
            res.symbol = stock.symbol;
            res.offset = bestOffset;
            res.pearson = bestPearson;
            res.distance = dist;
            res.stockPtr = &stock;

            int tid = omp_get_thread_num();
            threadResults[tid].push_back(res);
        }
    }

    // Merge results
    for (const auto& local : threadResults) {
        results.insert(results.end(), local.begin(), local.end());
    }
    
    std::cout << "AnalysisEngine: Merged " << results.size() << " results." << std::endl;

    // Sort by Pearson (Descending)
    std::sort(results.begin(), results.end(), [](const SearchResult& a, const SearchResult& b) {
        return a.pearson > b.pearson;
    });

    // Keep Top K
    if (results.size() > static_cast<size_t>(topK)) {
        results.resize(topK);
    }
    
    if (!results.empty()) {
        std::cout << "AnalysisEngine: Top Match: " << results[0].symbol << " (Pearson: " << results[0].pearson << ")" << std::endl;
    }

    return results;
}
