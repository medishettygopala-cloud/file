#pragma once

#include <algorithm>
#include <chrono>
#include <cctype>
#include <functional>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

struct MatchSet {
    std::vector<size_t> positions;
    double milliseconds = 0.0;
};

inline std::string toLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

inline std::string wildcardToRegex(const std::string& pattern) {
    std::string out;
    out.reserve(pattern.size() * 2);
    for (char c : pattern) {
        switch (c) {
            case '*': out += ".*"; break;
            case '?': out += "."; break;
            case '.': case '+': case '(': case ')': case '[': case ']':
            case '{': case '}': case '^': case '$': case '|': case '\\':
                out += '\\';
                out += c;
                break;
            default: out += c;
        }
    }
    return out;
}

inline MatchSet timed(std::function<std::vector<size_t>()> fn) {
    auto start = std::chrono::high_resolution_clock::now();
    MatchSet result;
    result.positions = fn();
    auto stop = std::chrono::high_resolution_clock::now();
    result.milliseconds = std::chrono::duration<double, std::milli>(stop - start).count();
    return result;
}

inline MatchSet naiveSearch(const std::string& text, const std::string& pattern) {
    return timed([&] {
        std::vector<size_t> matches;
        if (pattern.empty() || text.size() < pattern.size()) return matches;
        for (size_t i = 0; i <= text.size() - pattern.size(); ++i) {
            size_t j = 0;
            while (j < pattern.size() && text[i + j] == pattern[j]) ++j;
            if (j == pattern.size()) matches.push_back(i);
        }
        return matches;
    });
}

inline std::vector<int> kmpPrefix(const std::string& pattern) {
    std::vector<int> lps(pattern.size(), 0);
    for (size_t i = 1, len = 0; i < pattern.size();) {
        if (pattern[i] == pattern[len]) {
            lps[i++] = static_cast<int>(++len);
        } else if (len) {
            len = static_cast<size_t>(lps[len - 1]);
        } else {
            lps[i++] = 0;
        }
    }
    return lps;
}

inline MatchSet kmpSearch(const std::string& text, const std::string& pattern) {
    return timed([&] {
        std::vector<size_t> matches;
        if (pattern.empty() || text.size() < pattern.size()) return matches;
        auto lps = kmpPrefix(pattern);
        for (size_t i = 0, j = 0; i < text.size();) {
            if (text[i] == pattern[j]) {
                ++i;
                ++j;
                if (j == pattern.size()) {
                    matches.push_back(i - j);
                    j = static_cast<size_t>(lps[j - 1]);
                }
            } else if (j) {
                j = static_cast<size_t>(lps[j - 1]);
            } else {
                ++i;
            }
        }
        return matches;
    });
}

inline MatchSet rabinKarpSearch(const std::string& text, const std::string& pattern) {
    return timed([&] {
        std::vector<size_t> matches;
        if (pattern.empty() || text.size() < pattern.size()) return matches;
        const long long base = 256;
        const long long mod = 1000000007;
        long long high = 1, pHash = 0, tHash = 0;
        for (size_t i = 0; i + 1 < pattern.size(); ++i) high = (high * base) % mod;
        for (size_t i = 0; i < pattern.size(); ++i) {
            pHash = (base * pHash + static_cast<unsigned char>(pattern[i])) % mod;
            tHash = (base * tHash + static_cast<unsigned char>(text[i])) % mod;
        }
        for (size_t i = 0; i <= text.size() - pattern.size(); ++i) {
            if (pHash == tHash && text.compare(i, pattern.size(), pattern) == 0) matches.push_back(i);
            if (i < text.size() - pattern.size()) {
                tHash = (base * (tHash - static_cast<unsigned char>(text[i]) * high % mod + mod)
                       + static_cast<unsigned char>(text[i + pattern.size()])) % mod;
            }
        }
        return matches;
    });
}

inline MatchSet boyerMooreSearch(const std::string& text, const std::string& pattern) {
    return timed([&] {
        std::vector<size_t> matches;
        if (pattern.empty() || text.size() < pattern.size()) return matches;
        std::unordered_map<char, size_t> bad;
        for (size_t i = 0; i < pattern.size(); ++i) bad[pattern[i]] = i;
        size_t shift = 0;
        while (shift <= text.size() - pattern.size()) {
            int j = static_cast<int>(pattern.size()) - 1;
            while (j >= 0 && pattern[static_cast<size_t>(j)] == text[shift + static_cast<size_t>(j)]) --j;
            if (j < 0) {
                matches.push_back(shift);
                shift += 1;
            } else {
                auto it = bad.find(text[shift + static_cast<size_t>(j)]);
                size_t last = it == bad.end() ? static_cast<size_t>(-1) : it->second;
                shift += std::max<size_t>(1, static_cast<size_t>(j) - last);
            }
        }
        return matches;
    });
}

inline MatchSet regexSearch(const std::string& text, const std::string& pattern, bool caseSensitive) {
    return timed([&] {
        std::vector<size_t> matches;
        std::regex::flag_type flags = std::regex::ECMAScript;
        if (!caseSensitive) flags |= std::regex::icase;
        std::regex re(pattern, flags);
        for (auto it = std::sregex_iterator(text.begin(), text.end(), re); it != std::sregex_iterator(); ++it) {
            matches.push_back(static_cast<size_t>(it->position()));
        }
        return matches;
    });
}

