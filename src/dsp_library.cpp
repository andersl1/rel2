#include "dsp_library.h"
#include <algorithm>
#include <iostream>

namespace fs = std::filesystem;

// Helper to convert path to UTF-8 string safely
std::string PathToUtf8(const fs::path& path) {
    try {
#ifdef _WIN32
        // On Windows, generic_u8string might be available or u8string
        // C++17 standard says u8string returns std::string or char8_t string.
        // MSVC C++17: u8string returns std::string. C++20 returns char8_t.
        // Let's try u8string() and cast if needed. 
        // But simply catching the exception is the priority.
        // A simple fallback is generic_string() which might still fail if not ASCII.
        auto u8 = path.u8string();
        return std::string(reinterpret_cast<const char*>(u8.c_str()));
#else
        return path.string();
#endif
    } catch (...) {
        return path.string(); // Fallback that might throw again, handled by caller
    }
}

std::vector<DspFileEntry> DspLibrary::Scan(const std::string& rootPath) {
    std::vector<DspFileEntry> entries;
    
    if (rootPath.empty() || !fs::exists(rootPath)) {
        return entries;
    }

    // Reserve some space
    entries.reserve(3000);

    // Use a manual iterator loop to handle per-entry exceptions
    try {
        auto it = fs::recursive_directory_iterator(rootPath, fs::directory_options::skip_permission_denied);
        auto end = fs::recursive_directory_iterator();

        while (it != end) {
            try {
                const auto& entry = *it;
                
                if (entry.is_regular_file() && entry.path().extension() == ".dsp") {
                    std::string filename = entry.path().filename().string();
                    
                    // Filter out unwanted prefixes
                    // "daily", "function", "f(x)"
                    // Check exact start match. 
                    if (filename.find("daily") == 0 || 
                        filename.find("function") == 0 || 
                        filename.find("f(x)") == 0) {
                        ++it;
                        continue;
                    }

                    DspFileEntry e;
                    
                    // Try to get UTF-8 string, fallback to sensitive string if fails
                    try {
                        // C++20/C++17 mixed compatibility attempt
                        auto u8Str = entry.path().u8string(); 
                        e.fullPath = std::string(reinterpret_cast<const char*>(u8Str.c_str()));
                    } catch (...) {
                         // Fallback - might throw "No mapping"
                         e.fullPath = entry.path().string();
                    }

                    try {
                        auto rel = fs::relative(entry.path(), rootPath);
                        auto u8Rel = rel.u8string();
                        e.displayName = std::string(reinterpret_cast<const char*>(u8Rel.c_str()));
                    } catch (...) {
                         e.displayName = entry.path().filename().string();
                    }

                    // Normalize slashes
                    std::replace(e.displayName.begin(), e.displayName.end(), '\\', '/');
                    std::replace(e.fullPath.begin(), e.fullPath.end(), '\\', '/');

                    entries.push_back(e);
                }
                
                // Increment iterator
                // If increment throws (e.g. permission denied), we must handle it.
                // However, standard iterator increment can throw.
                // We need to catch exceptions during increment too.
                ++it;

            } catch (const std::exception& e) {
                // Silenced unicode errors to prevent console spam
                // std::cerr << "Skipping entry due to error: " << e.what() << std::endl;
                // If the error was in processing, we need to increment to continue.
                // BUT if increment threw, we are in trouble.
                // Usually skip_permission_denied handles directory access issues.
                // The "unicode mapping" error usually happens on .string(), so inside the loop body.
                // So we are safe to just catch and loop current iterator is valid?
                // If we caught inside the body, 'it' is still valid. We need to increment it.
                // But wait, if we are in the catch block, we haven't incremented.
                // So we should try to increment if possible, or break if iterator is broken.
                
                // For simplicity, if we fail to process an entry, we try to increment.
                // If increment fails, we might get stuck or crash.
                // Let's assume standard increment is safe if we didn't corrupt state.
                // Actually, if 'it' invalidates, we can't increment.
                // simpler approach:
                // recursive_directory_iterator increment can throw.
                // We are inside the while loop.
                try {
                     // If we are here, we processed (or failed to process) the current entry.
                     // The increment was at the end of the try block.
                     // If the exception happened BEFORE increment, we need to increment now.
                     // BUT we don't know if we already incremented? 
                     // No, current logic has ++it at end of try block.
                     // So if exception hit, we did NOT increment.
                     ++it; 
                } catch(...) {
                    // If increment fails, we probably can't continue safely.
                    break; 
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Critical Scan error: " << e.what() << std::endl;
    }
    
    return entries;
}

std::string DspLibrary::FindRoot(const std::string& target) {
    fs::path current = fs::current_path();
    for (int i = 0; i < 5; ++i) {
        fs::path candidate = current / target;
        if (fs::exists(candidate) && fs::is_directory(candidate)) {
            return candidate.string();
        }
        if (current.has_parent_path()) current = current.parent_path();
        else break;
    }
    return "";
}
