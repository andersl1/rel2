#pragma once

#include <string>
#include <vector>
#include <filesystem>

struct DspFileEntry {
    std::string fullPath;
    std::string displayName; // e.g. "REL2/src/save_files/A/AAPL/AAPL20(S1).dsp" or just "AAPL20(S1).dsp"
};

class DspLibrary {
public:
    // Recursively finds all .dsp files in the given directory
    static std::vector<DspFileEntry> Scan(const std::string& rootPath);

    // Attempts to find a directory named 'target' by checking current path and parents
    static std::string FindRoot(const std::string& target = "src/save_files");
};
