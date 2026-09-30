#pragma once

#include <cstddef>
#include <string>
#include <vector>

struct LocalDocument {
    std::string path;
    std::string name;
    std::string type;
    std::string text;
    std::string status;
    size_t size = 0;
    std::string error;
};

struct ScanResult {
    std::vector<LocalDocument> documents;
    size_t discovered = 0;
    size_t skipped = 0;
    std::vector<std::string> warnings;
};

ScanResult scanLocalPaths(const std::vector<std::string>& paths, bool includeSubfolders, const std::string& tempDirectory);
