#include "FolderScanner.h"

#include "extraction/TextExtractor.h"

#include <filesystem>
#include <cctype>
#include <cstdint>

namespace fs = std::filesystem;

namespace {
std::string extensionOf(const fs::path& path) {
    std::string value = path.extension().string();
    if (!value.empty() && value.front() == '.') value.erase(value.begin());
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

bool supported(const std::string& type) {
    return isSupportedType(type);
}

void addFile(const fs::path& path, const std::string& tempDirectory, ScanResult& result) {
    std::error_code error;
    if (!fs::is_regular_file(path, error) || error) return;
    ++result.discovered;
    std::string type = extensionOf(path);
    if (!supported(type)) {
        ++result.skipped;
        result.warnings.push_back(path.filename().string() + ": unsupported file type skipped");
        return;
    }
    LocalDocument document;
    document.path = path.string();
    document.name = path.filename().string();
    document.type = type;
    document.size = fs::file_size(path, error);
    // Upload limit: 5 GB per file. Only fail if size can't be read or exceeds 5 GB.
    static const uint64_t maxLocalSize = 5ULL * 1024ULL * 1024ULL * 1024ULL;
    if (error || document.size > maxLocalSize) {
        document.error = error ? "Unable to read file size" : "File exceeds 5 GB";
        result.warnings.push_back(document.name + ": " + document.error);
        ++result.skipped;
        return;
    }
    try {
        document.text = extractText(path, type, tempDirectory);
        document.status = isImageType(type) ? "Indexed (filename only)" : "Processed";
        if (document.text.empty() && !isImageType(type)) document.error = "Unable to extract text";
    } catch (...) {
        document.error = "Unable to extract text";
    }
    if (!document.error.empty()) {
        result.warnings.push_back(document.name + ": " + document.error);
        ++result.skipped;
        return;
    }
    result.documents.push_back(std::move(document));
}
}

ScanResult scanLocalPaths(const std::vector<std::string>& paths, bool includeSubfolders, const std::string& tempDirectory) {
    ScanResult result;
    for (const std::string& value : paths) {
        fs::path path(value);
        std::error_code error;
        if (fs::is_directory(path, error)) {
            if (includeSubfolders) {
                fs::recursive_directory_iterator iterator(path, fs::directory_options::skip_permission_denied, error);
                fs::recursive_directory_iterator end;
                for (; iterator != end; iterator.increment(error)) {
                    if (error) { error.clear(); continue; }
                    addFile(iterator->path(), tempDirectory, result);
                }
            } else {
                fs::directory_iterator iterator(path, fs::directory_options::skip_permission_denied, error);
                fs::directory_iterator end;
                for (; iterator != end; iterator.increment(error)) {
                    if (error) { error.clear(); continue; }
                    addFile(iterator->path(), tempDirectory, result);
                }
            }
        } else {
            addFile(path, tempDirectory, result);
        }
    }
    return result;
}
