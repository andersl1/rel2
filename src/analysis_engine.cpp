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


        try {
            DspData data = DspReader::Load(entry.fullPath);
            if (data.values.size() < 400) continue;

            CachedStock stock;
            stock.symbol = entry.displayName; 
            stock.fullPath = entry.fullPath;
            stock.data = std::move(data.values);
            stock.isFred = ContainsFred(entry.fullPath);

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

    // Pass 1: Means
    double mean_a = 0.0;
    double mean_b = 0.0;
    for (size_t i = 0; i < size; ++i) {
        mean_a += a[i];
        mean_b += b[i];
    }
    mean_a /= size;
    mean_b /= size;

    // Pass 2: Covariance and Variances (Centered)
    double num = 0.0;
    double sum_sq_a = 0.0;
    double sum_sq_b = 0.0;

    for (size_t i = 0; i < size; ++i) {
        double diff_a = a[i] - mean_a;
        double diff_b = b[i] - mean_b;
        num += diff_a * diff_b;
        sum_sq_a += diff_a * diff_a;
        sum_sq_b += diff_b * diff_b;
    }

    double den = std::sqrt(sum_sq_a * sum_sq_b);
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

// Helper to downsample by 2 (averaging)
std::vector<double> AnalysisEngine::Downsample(const std::vector<double>& in) {
    std::vector<double> out;
    out.reserve(in.size() / 2);
    for (size_t i = 0; i + 1 < in.size(); i += 2) {
        out.push_back((in[i] + in[i+1]) * 0.5);
    }
    return out;
}

std::vector<SearchResult> AnalysisEngine::Search(const std::vector<double>& query, bool useFred, int topK, int lookahead) {
    std::vector<SearchResult> results;
    
    // Use entire query as pattern
    const size_t patternSize = query.size();
    if (patternSize < 10) return results; // Minimum safety checks

    // Log if verbose? 
    // std::cout << "AnalysisEngine: Starting search. Query=" << patternSize << ", Lookahead=" << lookahead << std::endl;

    const std::vector<double>& pattern = query;

    // Thread-local storage for gathering results
    std::vector<std::vector<SearchResult>> threadResults(omp_get_max_threads());

    #pragma omp parallel for schedule(dynamic)
    for (int i = 0; i < static_cast<int>(m_Cache.size()); ++i) {
        const auto& stock = m_Cache[i];
        if (!useFred && stock.isFred) continue;
        
        // Multi-Scale Search Variables
        double globalBestPearson = -1.0;
        int globalBestOffset = -1;
        int globalBestScale = 1;
        
        // Create a copy for downsampling
        std::vector<double> currentData = stock.data;
        int currentScale = 1;

        // Loop through scales
        // Condition: we need patternSize + lookahead points.
        while (currentData.size() >= patternSize + lookahead) {
            
            const int searchLimit = static_cast<int>(currentData.size()) - lookahead - static_cast<int>(patternSize);

            if (searchLimit >= 0) {
                double localBestPearson = -1.0;
                int localBestOffset = -1;

                // Search at this scale
                for (int j = 0; j <= searchLimit; ++j) {
                    double p = CalculatePearson(pattern.data(), currentData.data() + j, patternSize);
                    if (p > localBestPearson) {
                        localBestPearson = p;
                        localBestOffset = j;
                    }
                }

                if (localBestPearson > globalBestPearson) {
                    globalBestPearson = localBestPearson;
                    globalBestOffset = localBestOffset;
                    globalBestScale = currentScale;
                }
            }

            // Prepare next scale
            currentData = Downsample(currentData);
            currentScale *= 2;
        }

        // Check Threshold logic (User requirement: discard if < 0.7)
        if (globalBestOffset != -1 && globalBestPearson >= 0.7) {
            
            // "Invariant to Y stretching" Distance is simply derived from Pearson.
            // Pearson = Cosine of Centered Vectors.
            // Distance = acos(Pearson).
            double dist = std::acos(std::max(-1.0, std::min(1.0, globalBestPearson)));
            
            SearchResult res;
            res.symbol = stock.symbol;
            res.offset = globalBestOffset;
            res.scale = globalBestScale;
            res.pearson = globalBestPearson;
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

    // Sort by Hyperspherical Distance (Ascending: 0 is best)
    // Note: Since Distance = acos(Pearson), Sorting by Distance Ascending is IDENTICAL to Pearson Descending.
    std::sort(results.begin(), results.end(), [](const SearchResult& a, const SearchResult& b) {
        return a.distance < b.distance;
    });

    // Keep Top K
    if (results.size() > static_cast<size_t>(topK)) {
        results.resize(topK);
    }
    
    if (!results.empty()) {
        std::cout << "AnalysisEngine: Top Match: " << results[0].symbol << " (Dist: " << results[0].distance << ", Pearson: " << results[0].pearson << ")" << std::endl;
    }

    return results;
}
