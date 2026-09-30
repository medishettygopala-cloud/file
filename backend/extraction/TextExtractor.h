#pragma once

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <set>
#include <string>

namespace fs = std::filesystem;

inline bool isImageType(const std::string& type) {
    static const std::set<std::string> types{"jpg", "jpeg", "png", "gif", "bmp", "webp"};
    return types.count(type) != 0;
}

inline bool isPlainTextType(const std::string& type) {
    static const std::set<std::string> types{"txt", "csv", "md", "json", "log", "rtf"};
    return types.count(type) != 0;
}

inline bool isSupportedType(const std::string& type) {
    static const std::set<std::string> types{"pdf", "docx", "pptx"};
    return types.count(type) != 0 || isImageType(type) || isPlainTextType(type);
}

// Best-effort cleanup: never throw. Windows/OneDrive/AV can lock
// extracted files (e.g. word/media), so Access Denied must not fail uploads.
inline void safeRemoveAll(const fs::path& path) {
    try {
        std::error_code ec;
        // Clear read-only flags first (Expand-Archive often sets them).
        fs::permissions(path,
            fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write,
            fs::perm_options::add, ec);
        fs::remove_all(path, ec);
        // One retry after releasing возможным locks.
        if (ec && fs::exists(path, ec)) {
            fs::permissions(path,
                fs::perms::owner_all | fs::perms::group_all | fs::perms::others_all,
                fs::perm_options::add, ec);
            ec.clear();
            fs::remove_all(path, ec);
        }
    } catch (...) {}
}

inline void safeRemove(const fs::path& path) {
    try {
        std::error_code ec;
        fs::remove(path, ec);
    } catch (...) {}
}

inline std::string uniqueSuffix() {
    static uint64_t counter = 0;
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::ostringstream out;
    out << std::hex << now << "_" << (++counter) << "_" << (std::rand() & 0xffff);
    return out.str();
}

inline std::string readBinaryFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

inline void writeTextFile(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
}

inline std::string normalizeWhitespace(std::string text) {
    for (char& c : text) {
        unsigned char u = static_cast<unsigned char>(c);
        if (u < 32 || std::isspace(u)) c = ' ';
    }
    std::string out;
    out.reserve(text.size());
    bool previousSpace = true;
    for (char c : text) {
        if (c == ' ') {
            if (!previousSpace) out += c;
            previousSpace = true;
        } else {
            out += c;
            previousSpace = false;
        }
    }
    return out;
}

inline std::string stripXmlTags(std::string xml) {
    xml = std::regex_replace(xml, std::regex("<[^>]+>"), " ");
    const std::pair<const char*, const char*> entities[] = {
        {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"},
        {"&quot;", "\""}, {"&apos;", "'"}
    };
    for (auto [from, to] : entities) {
        size_t pos = 0;
        while ((pos = xml.find(from, pos)) != std::string::npos) {
            xml.replace(pos, std::strlen(from), to);
            pos += std::strlen(to);
        }
    }
    return normalizeWhitespace(xml);
}

inline std::string printableByteFallback(const fs::path& path) {
    std::string bytes = readBinaryFile(path);
    std::string out;
    out.reserve(bytes.size());
    for (unsigned char c : bytes) {
        if (std::isprint(c) || std::isspace(c)) out += static_cast<char>(c);
        else out += ' ';
    }
    return normalizeWhitespace(out);
}

inline std::string shellQuote(const fs::path& path) {
    std::string s = path.string();
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "''";
        else out += c;
    }
    out += "'";
    return out;
}

inline bool runCommand(const std::string& command) {
    int code = std::system(command.c_str());
    return code == 0;
}

inline std::string extractPdfText(const fs::path& input, const fs::path& tempDir) {
    try {
        std::error_code ec;
        fs::create_directories(tempDir, ec);
        fs::path out = tempDir / (input.stem().string() + "_" + uniqueSuffix() + ".pdf.txt");
#ifdef _WIN32
        std::string cmd = "pdftotext -layout " + shellQuote(input) + " " + shellQuote(out) + " >NUL 2>NUL";
#else
        std::string cmd = "pdftotext -layout " + shellQuote(input) + " " + shellQuote(out) + " >/dev/null 2>/dev/null";
#endif
        if (runCommand(cmd) && fs::exists(out, ec)) {
            std::string text;
            try { text = readBinaryFile(out); } catch (...) {}
            safeRemove(out);
            if (!text.empty()) return normalizeWhitespace(text);
        }
        return printableByteFallback(input);
    } catch (...) {
        return printableByteFallback(input);
    }
}

inline std::string extractZipXmlWindows(const fs::path& input, const std::string& kind, const fs::path& tempDir) {
    try {
        std::error_code ec;
        fs::create_directories(tempDir, ec);
        // Unique dir per extraction: avoids collisions for same stem and
        // avoids deleting another in-flight request's folder.
        fs::path dir = tempDir / (input.stem().string() + "_" + uniqueSuffix() + "_unzipped");
        fs::path zipPath = tempDir / (input.stem().string() + "_" + uniqueSuffix() + ".zip");
        safeRemoveAll(dir);
        safeRemove(zipPath);
        fs::create_directories(dir, ec);
        if (ec) return {};
        fs::copy_file(input, zipPath, fs::copy_options::overwrite_existing, ec);
        if (ec) { safeRemoveAll(dir); safeRemove(zipPath); return {}; }
        std::string cmd = "powershell -NoProfile -ExecutionPolicy Bypass -Command \"Expand-Archive -LiteralPath "
            + shellQuote(zipPath) + " -DestinationPath " + shellQuote(dir) + " -Force\"";
        if (!runCommand(cmd)) {
            safeRemove(zipPath);
            safeRemoveAll(dir);
            return {};
        }
        std::ostringstream xml;
        if (kind == "docx") {
            fs::path doc = dir / "word" / "document.xml";
            if (fs::exists(doc, ec)) xml << readBinaryFile(doc);
        } else {
            fs::path slides = dir / "ppt" / "slides";
            if (fs::exists(slides, ec)) {
                for (const auto& entry : fs::directory_iterator(slides, ec)) {
                    if (ec) break;
                    if (entry.path().extension() == ".xml") {
                        try { xml << ' ' << readBinaryFile(entry.path()); } catch (...) {}
                    }
                }
            }
        }
        safeRemove(zipPath);
        safeRemoveAll(dir);  // best-effort: locked word/media files must not fail the upload
        std::string text = stripXmlTags(xml.str());
        if (!text.empty()) return text;
        return printableByteFallback(input);
    } catch (...) {
        return printableByteFallback(input);
    }
}

inline std::string extractZipXmlUnix(const fs::path& input, const std::string& kind, const fs::path& tempDir) {
    try {
        std::error_code ec;
        fs::create_directories(tempDir, ec);
        fs::path out = tempDir / (input.stem().string() + "_" + uniqueSuffix() + "." + kind + ".xml");
        std::string selector = kind == "docx" ? "word/document.xml" : "ppt/slides/*.xml";
        std::string cmd = "unzip -p " + shellQuote(input) + " '" + selector + "' > " + shellQuote(out) + " 2>/dev/null";
        std::string result;
        if (runCommand(cmd) && fs::exists(out, ec)) {
            try { result = stripXmlTags(readBinaryFile(out)); } catch (...) {}
        }
        safeRemove(out);
        return result;
    } catch (...) {
        return {};
    }
}

inline std::string extractOfficeText(const fs::path& input, const std::string& kind, const fs::path& tempDir) {
#ifdef _WIN32
    std::string extracted = extractZipXmlWindows(input, kind, tempDir);
#else
    std::string extracted = extractZipXmlUnix(input, kind, tempDir);
#endif
    if (!extracted.empty()) return extracted;
    return printableByteFallback(input);
}

inline std::string extractText(const fs::path& input, const std::string& type, const fs::path& tempDir) {
    try {
        if (isImageType(type)) return {};
        if (isPlainTextType(type)) return normalizeWhitespace(readBinaryFile(input));
        if (type == "pdf") return extractPdfText(input, tempDir);
        if (type == "docx" || type == "pptx") return extractOfficeText(input, type, tempDir);
        return {};
    } catch (...) {
        // Extraction must never fail an upload with a filesystem exception.
        try { return printableByteFallback(input); } catch (...) { return {}; }
    }
}

inline size_t countWords(const std::string& text) {
    std::istringstream in(text);
    size_t count = 0;
    std::string word;
    while (in >> word) ++count;
    return count;
}
