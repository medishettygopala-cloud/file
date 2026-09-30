#pragma once

#include <sstream>
#include <string>
#include <vector>

inline std::string jsonEscape(const std::string& s) {
    std::ostringstream out;
    for (char c : s) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 32) out << ' ';
                else out << c;
        }
    }
    return out.str();
}

inline std::string jsonString(const std::string& s) {
    return "\"" + jsonEscape(s) + "\"";
}

inline std::string jsonArray(const std::vector<std::string>& values) {
    std::ostringstream out;
    out << "[";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i) out << ",";
        out << jsonString(values[i]);
    }
    out << "]";
    return out.str();
}
