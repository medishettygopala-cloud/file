#pragma once

#include <string>

struct FileInfo {
    std::string id;
    std::string originalName;
    std::string storedName;
    std::string type;
    size_t size = 0;
    std::string uploadedAt;
    std::string status;
    size_t wordCount = 0;
    std::string extractedPath;
    std::string folder = "Inbox";
    std::string tags;
    std::string fingerprint;
    bool deleted = false;
};
