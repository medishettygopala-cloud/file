#include "extraction/TextExtractor.h"
#include "explorer/FileExplorer.h"
#include "explorer/FolderScanner.h"
#include "models/FileInfo.h"
#include "search/SearchAlgorithms.h"
#include "utils/Json.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <random>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#endif

namespace fs = std::filesystem;

struct Request {
    std::string method;
    std::string path;
    std::map<std::string, std::string> headers;
    std::string body;
};

struct Response {
    int status = 200;
    std::string contentType = "application/json";
    std::string body;
    std::map<std::string, std::string> headers;
};

struct UploadPart {
    std::string filename;
    std::string contentType;
    std::string data;
};

static fs::path root = fs::current_path();
static fs::path uploadDir;
static fs::path extractedDir;
static fs::path dataDir;
static std::string workspaceId = "default";
static std::vector<FileInfo> files;
static std::vector<std::string> recentSearches;
static int totalSearches = 0;
static size_t totalMatches = 0;
static std::string selectedFolder;
static std::vector<std::string> selectedFiles;
static std::vector<LocalDocument> localDocuments;
static std::vector<std::string> localWarnings;
static bool includeSubfolders = true;

// Forward declarations for helper functions
std::string randomId();
std::string nowIso();
std::string getJsonString(const std::string& json, const std::string& key, const std::string& fallback = "");

// ---------------- Auth (login/signup, per-user storage) ----------------
struct User {
    std::string id;
    std::string email;  // normalized lowercase
    std::string salt;
    std::string hash;
    std::string createdAt;
};

static std::vector<User> users;
static std::map<std::string, std::string> sessions;  // token -> userId
static User* authUser = nullptr;
static std::string authToken;

// Minimal SHA-256 (public domain style) for salted password hashing.
namespace sha256impl {
inline uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }
inline std::string hashHex(const std::string& input) {
    static const uint32_t K[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    std::string msg = input;
    uint64_t bitLen = static_cast<uint64_t>(msg.size()) * 8;
    msg.push_back(static_cast<char>(0x80));
    while (msg.size() % 64 != 56) msg.push_back(static_cast<char>(0x00));
    for (int i = 7; i >= 0; --i) msg.push_back(static_cast<char>((bitLen >> (i * 8)) & 0xff));
    for (size_t off = 0; off < msg.size(); off += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(static_cast<unsigned char>(msg[off + i * 4])) << 24) |
                   (static_cast<uint32_t>(static_cast<unsigned char>(msg[off + i * 4 + 1])) << 16) |
                   (static_cast<uint32_t>(static_cast<unsigned char>(msg[off + i * 4 + 2])) << 8) |
                   static_cast<uint32_t>(static_cast<unsigned char>(msg[off + i * 4 + 3]));
        }
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i-15],7) ^ rotr(w[i-15],18) ^ (w[i-15] >> 3);
            uint32_t s1 = rotr(w[i-2],17) ^ rotr(w[i-2],19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = rotr(e,6) ^ rotr(e,11) ^ rotr(e,25);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            uint32_t S0 = rotr(a,2) ^ rotr(a,13) ^ rotr(a,22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + maj;
            hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
    }
    std::ostringstream out;
    out << std::hex;
    for (int i = 0; i < 8; ++i) { out.width(8); out.fill('0'); out << h[i]; }
    return out.str();
}
}

std::string normalizeEmail(const std::string& raw) {
    size_t a = 0, b = raw.size();
    while (a < b && std::isspace(static_cast<unsigned char>(raw[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(raw[b-1]))) --b;
    std::string out = raw.substr(a, b - a);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return out;
}

bool validEmail(const std::string& email) {
    if (email.size() < 5 || email.size() > 254) return false;
    if (email.find(' ') != std::string::npos) return false;
    size_t at = email.find('@');
    if (at == std::string::npos || email.find('@', at + 1) != std::string::npos) return false;
    size_t dot = email.find('.', at);
    if (dot == std::string::npos || dot + 1 >= email.size()) return false;
    if (at == 0 || dot <= at + 1) return false;
    return true;
}

std::string hashPassword(const std::string& salt, const std::string& password) {
    return sha256impl::hashHex(salt + ":" + password);
}

fs::path authDbPath() { return root / "data" / "users.db"; }
fs::path sessionDbPath() { return root / "data" / "sessions.db"; }

void loadUsers() {
    users.clear();
    std::ifstream in(authDbPath(), std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::vector<std::string> cols;
        std::stringstream ss(line);
        std::string col;
        while (std::getline(ss, col, '\t')) cols.push_back(col);
        if (cols.size() < 5) continue;
        users.push_back({cols[0], cols[1], cols[2], cols[3], cols[4]});
    }
}

void saveUsers() {
    fs::create_directories(root / "data");
    std::ofstream out(authDbPath(), std::ios::binary | std::ios::trunc);
    for (const auto& u : users) out << u.id << '\t' << u.email << '\t' << u.salt << '\t' << u.hash << '\t' << u.createdAt << '\n';
}

void loadSessions() {
    sessions.clear();
    std::ifstream in(sessionDbPath(), std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        sessions[line.substr(0, tab)] = line.substr(tab + 1);
    }
}

void saveSessions() {
    fs::create_directories(root / "data");
    std::ofstream out(sessionDbPath(), std::ios::binary | std::ios::trunc);
    for (const auto& [tok, uid] : sessions) out << tok << '\t' << uid << '\n';
}

User* findUserByEmail(const std::string& email) {
    for (auto& u : users) if (u.email == email) return &u;
    return nullptr;
}

User* findUserById(const std::string& id) {
    for (auto& u : users) if (u.id == id) return &u;
    return nullptr;
}

std::string newToken() { return randomId() + randomId(); }

std::string bearerToken(const Request& req) {
    auto it = req.headers.find("authorization");
    if (it != req.headers.end()) {
        std::string v = it->second;
        if (v.rfind("Bearer ", 0) == 0) return v.substr(7);
        return v;
    }
    it = req.headers.find("x-auth-token");
    if (it != req.headers.end()) return it->second;
    return "";
}

void resolveAuth(const Request& req) {
    authUser = nullptr;
    authToken.clear();
    std::string tok = bearerToken(req);
    if (tok.empty()) return;
    auto it = sessions.find(tok);
    if (it == sessions.end()) return;
    User* u = findUserById(it->second);
    if (!u) return;
    authUser = u;
    authToken = tok;
}

std::string userJson(const User& u) {
    return std::string("{\"id\":") + jsonString(u.id) + ",\"email\":" + jsonString(u.email) + "}";
}

Response requireAuth() {
    return {401, "application/json", "{\"error\":\"Please login to access your files\"}"};
}

Response apiSignup(const Request& req) {
    std::string email = normalizeEmail(getJsonString(req.body, "email"));
    std::string password = getJsonString(req.body, "password");
    if (!validEmail(email)) return {400, "application/json", "{\"error\":\"Enter a valid email address\"}"};
    if (password.size() < 6) return {400, "application/json", "{\"error\":\"Password must be at least 6 characters\"}"};
    if (password.size() > 128) return {400, "application/json", "{\"error\":\"Password is too long\"}"};
    if (findUserByEmail(email)) return {409, "application/json", "{\"error\":\"Account already exists. Please login.\"}"};
    User u;
    u.id = randomId() + randomId();
    u.email = email;
    u.salt = randomId() + randomId();
    u.hash = hashPassword(u.salt, password);
    u.createdAt = nowIso();
    users.push_back(u);
    saveUsers();
    std::string tok = newToken();
    sessions[tok] = u.id;
    saveSessions();
    User* stored = findUserById(u.id);
    std::string body = std::string("{\"token\":") + jsonString(tok) + ",\"user\":" + userJson(*stored) + "}";
    return {200, "application/json", body};
}

Response apiLogin(const Request& req) {
    std::string email = normalizeEmail(getJsonString(req.body, "email"));
    std::string password = getJsonString(req.body, "password");
    if (!validEmail(email)) return {400, "application/json", "{\"error\":\"Enter a valid email address\"}"};
    if (password.empty()) return {400, "application/json", "{\"error\":\"Enter your password\"}"};
    User* u = findUserByEmail(email);
    if (!u) return {401, "application/json", "{\"error\":\"No account found for this email. Please sign up.\"}"};
    if (hashPassword(u->salt, password) != u->hash) return {401, "application/json", "{\"error\":\"Incorrect password\"}"};
    std::string tok = newToken();
    sessions[tok] = u->id;
    saveSessions();
    std::string body = std::string("{\"token\":") + jsonString(tok) + ",\"user\":" + userJson(*u) + "}";
    return {200, "application/json", body};
}

Response apiLogout() {
    if (!authToken.empty()) { sessions.erase(authToken); saveSessions(); }
    authUser = nullptr;
    authToken.clear();
    return {200, "application/json", "{\"ok\":true}"};
}

Response apiMe() {
    if (!authUser) return requireAuth();
    return {200, "application/json", std::string("{\"user\":") + userJson(*authUser) + "}"};
}

bool validWorkspaceId(const std::string& value) {
    if (value.size() < 8 || value.size() > 64) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '-' || c == '_';
    });
}

std::string nowIso() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
    return buf;
}

std::string randomId() {
    static std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<unsigned long long> dist;
    std::ostringstream out;
    out << std::hex << dist(rng);
    return out.str();
}

std::string fingerprintOf(const std::string& data) {
    uint64_t hash = 1469598103934665603ULL;
    for (unsigned char value : data) {
        hash ^= value;
        hash *= 1099511628211ULL;
    }
    std::ostringstream out;
    out << std::hex << hash;
    return out.str();
}

std::string extensionOf(const std::string& name) {
    fs::path p(name);
    std::string ext = p.extension().string();
    if (!ext.empty() && ext[0] == '.') ext.erase(ext.begin());
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

std::string safeName(const std::string& original) {
    std::string out;
    for (char c : fs::path(original).filename().string()) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-') out += c;
        else out += '_';
    }
    return out.empty() ? "upload.bin" : out;
}

std::string originalFileName(const std::string& name) {
    std::string filename = fs::path(name).filename().string();
    return filename.empty() ? "upload.bin" : filename;
}

bool endsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string urlDecode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            std::string hex = s.substr(i + 1, 2);
            out += static_cast<char>(std::stoi(hex, nullptr, 16));
            i += 2;
        } else if (s[i] == '+') out += ' ';
        else out += s[i];
    }
    return out;
}

std::string getJsonString(const std::string& json, const std::string& key, const std::string& fallback) {
    std::regex re("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
    std::smatch m;
    if (std::regex_search(json, m, re)) {
        std::string value = m[1].str();
        std::string out;
        for (size_t i = 0; i < value.size(); ++i) {
            if (value[i] == '\\' && i + 1 < value.size()) {
                char next = value[++i];
                if (next == 'n') out += '\n';
                else if (next == 'r') out += '\r';
                else if (next == 't') out += '\t';
                else out += next;
            } else out += value[i];
        }
        return out;
    }
    return fallback;
}

bool getJsonBool(const std::string& json, const std::string& key, bool fallback = false) {
    std::regex re("\"" + key + "\"\\s*:\\s*(true|false)");
    std::smatch m;
    if (std::regex_search(json, m, re)) return m[1].str() == "true";
    return fallback;
}

std::vector<std::string> getJsonArray(const std::string& json, const std::string& key) {
    std::vector<std::string> values;
    std::regex re("\"" + key + "\"\\s*:\\s*\\[(.*?)\\]");
    std::smatch m;
    if (!std::regex_search(json, m, re)) return values;
    std::string body = m[1].str();
    std::regex item("\"([^\"]*)\"");
    for (auto it = std::sregex_iterator(body.begin(), body.end(), item); it != std::sregex_iterator(); ++it) {
        values.push_back((*it)[1].str());
    }
    return values;
}

std::string fileJson(const FileInfo& f) {
    std::ostringstream out;
    out << "{"
        << "\"id\":" << jsonString(f.id) << ","
        << "\"originalName\":" << jsonString(f.originalName) << ","
        << "\"storedName\":" << jsonString(f.storedName) << ","
        << "\"type\":" << jsonString(f.type) << ","
        << "\"size\":" << f.size << ","
        << "\"uploadedAt\":" << jsonString(f.uploadedAt) << ","
        << "\"status\":" << jsonString(f.status) << ","
        << "\"wordCount\":" << f.wordCount << ","
        << "\"folder\":" << jsonString(f.folder) << ","
        << "\"tags\":" << jsonString(f.tags) << ","
        << "\"deleted\":" << (f.deleted ? "true" : "false")
        << "}";
    return out.str();
}

void saveDatabase() {
    fs::create_directories(dataDir);
    std::ofstream out(dataDir / "files.db", std::ios::binary);
    for (const auto& f : files) {
        out << f.id << '\t' << f.originalName << '\t' << f.storedName << '\t' << f.type << '\t'
            << f.size << '\t' << f.uploadedAt << '\t' << f.status << '\t' << f.wordCount << '\t'
            << f.extractedPath << '\t' << f.folder << '\t' << f.tags << '\t' << f.fingerprint << '\t'
            << (f.deleted ? "1" : "0") << '\n';
    }
}

void loadDatabase() {
    files.clear();
    std::ifstream in(dataDir / "files.db", std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        std::vector<std::string> cols;
        std::stringstream ss(line);
        std::string col;
        while (std::getline(ss, col, '\t')) cols.push_back(col);
        if (cols.size() < 9) continue;
        FileInfo f;
        f.id = cols[0]; f.originalName = cols[1]; f.storedName = cols[2]; f.type = cols[3];
        f.size = static_cast<size_t>(std::stoull(cols[4]));
        f.uploadedAt = cols[5]; f.status = cols[6];
        f.wordCount = static_cast<size_t>(std::stoull(cols[7]));
        f.extractedPath = cols[8];
        if (cols.size() > 9) f.folder = cols[9];
        if (cols.size() > 10) f.tags = cols[10];
        if (cols.size() > 11) f.fingerprint = cols[11];
        if (cols.size() > 12) f.deleted = cols[12] == "1";
        files.push_back(f);
    }
}

void selectWorkspace(const Request& req) {
    if (authUser) {
        // Per-user storage: each login gets a private workspace so old
        // users see their own files/resources after login.
        std::string safe;
        for (char c : authUser->id) {
            if (std::isalnum(static_cast<unsigned char>(c))) safe += static_cast<char>(std::tolower(c));
        }
        if (safe.size() < 8) safe = "user_default";
        workspaceId = "u_" + safe.substr(0, 48);
    } else {
        auto it = req.headers.find("x-workspace-id");
        workspaceId = it != req.headers.end() && validWorkspaceId(it->second) ? it->second : "default";
    }
    fs::path workspaceRoot = root / "workspaces" / workspaceId;
    uploadDir = workspaceRoot / "uploads";
    extractedDir = workspaceRoot / "extracted_text";
    dataDir = workspaceRoot / "data";
    fs::create_directories(uploadDir);
    fs::create_directories(extractedDir);
    fs::create_directories(dataDir);
    loadDatabase();
}

std::vector<UploadPart> parseMultipart(const Request& req) {
    std::vector<UploadPart> parts;
    auto it = req.headers.find("content-type");
    if (it == req.headers.end()) return parts;
    std::string marker = "boundary=";
    size_t b = it->second.find(marker);
    if (b == std::string::npos) return parts;
    std::string rawBoundary = it->second.substr(b + marker.size());
    size_t semi = rawBoundary.find(';');
    if (semi != std::string::npos) rawBoundary = rawBoundary.substr(0, semi);
    while (!rawBoundary.empty() && std::isspace(static_cast<unsigned char>(rawBoundary.front()))) rawBoundary.erase(rawBoundary.begin());
    while (!rawBoundary.empty() && std::isspace(static_cast<unsigned char>(rawBoundary.back()))) rawBoundary.pop_back();
    if (rawBoundary.size() >= 2 && rawBoundary.front() == '"' && rawBoundary.back() == '"') rawBoundary = rawBoundary.substr(1, rawBoundary.size() - 2);
    std::string boundary = "--" + rawBoundary;
    size_t pos = 0;
    while ((pos = req.body.find(boundary, pos)) != std::string::npos) {
        pos += boundary.size();
        if (req.body.compare(pos, 2, "--") == 0) break;
        if (req.body.compare(pos, 2, "\r\n") == 0) pos += 2;
        size_t headerEnd = req.body.find("\r\n\r\n", pos);
        if (headerEnd == std::string::npos) break;
        std::string headers = req.body.substr(pos, headerEnd - pos);
        size_t dataStart = headerEnd + 4;
        size_t next = req.body.find(boundary, dataStart);
        if (next == std::string::npos) break;
        std::string data = req.body.substr(dataStart, next - dataStart);
        if (data.size() >= 2 && data.substr(data.size() - 2) == "\r\n") data.resize(data.size() - 2);
        std::regex nameRe("filename=\"([^\"]*)\"");
        std::smatch m;
        if (std::regex_search(headers, m, nameRe) && !m[1].str().empty()) {
            UploadPart part;
            part.filename = m[1].str();
            part.data = std::move(data);
            std::regex typeRe("Content-Type:\\s*([^\\r\\n]+)", std::regex::icase);
            if (std::regex_search(headers, m, typeRe)) part.contentType = m[1].str();
            parts.push_back(std::move(part));
        }
        pos = next;
    }
    return parts;
}

std::string contextFor(const std::string& originalText, size_t pos, size_t length) {
    size_t start = pos > 80 ? pos - 80 : 0;
    size_t end = std::min(originalText.size(), pos + length + 80);
    std::string chunk = originalText.substr(start, end - start);
    return (start ? "..." : "") + chunk + (end < originalText.size() ? "..." : "");
}

std::string highlightContext(const std::string& ctx, const std::string& query, bool caseSensitive) {
    if (query.empty()) return jsonEscape(ctx);
    std::string hay = caseSensitive ? ctx : toLowerCopy(ctx);
    std::string needle = caseSensitive ? query : toLowerCopy(query);
    size_t p = hay.find(needle);
    if (p == std::string::npos) return jsonEscape(ctx);
    return jsonEscape(ctx.substr(0, p)) + "<mark>" + jsonEscape(ctx.substr(p, query.size())) + "</mark>" + jsonEscape(ctx.substr(p + query.size()));
}

MatchSet runAlgorithm(const std::string& text, const std::string& pattern, const std::string& algorithm, bool caseSensitive) {
    std::string source = caseSensitive ? text : toLowerCopy(text);
    std::string query = caseSensitive ? pattern : toLowerCopy(pattern);
    if (algorithm == "naive") return naiveSearch(source, query);
    if (algorithm == "rabin-karp") return rabinKarpSearch(source, query);
    if (algorithm == "boyer-moore") return boyerMooreSearch(source, query);
    return kmpSearch(source, query);
}

Response apiFiles() {
    std::ostringstream out;
    out << "{\"files\":[";
    bool first = true;
    for (const auto& file : files) {
        if (file.deleted) continue;
        if (!first) out << ",";
        first = false;
        out << fileJson(file);
    }
    out << "]}";
    return {200, "application/json", out.str()};
}

Response apiTrash() {
    std::ostringstream out;
    out << "{\"files\":[";
    bool first = true;
    for (const auto& file : files) {
        if (!file.deleted) continue;
        if (!first) out << ",";
        first = false;
        out << fileJson(file);
    }
    out << "]}";
    return {200, "application/json", out.str()};
}

Response apiRestore(const std::string& id) {
    for (auto& file : files) {
        if (file.id == id && file.deleted) {
            file.deleted = false;
            file.status = "Processed";
            saveDatabase();
            return {200, "application/json", fileJson(file)};
        }
    }
    return {404, "application/json", "{\"error\":\"File not found in recycle bin\"}"};
}

Response apiStats() {
    std::map<std::string, int> byType{{"pdf",0},{"docx",0},{"pptx",0},{"txt",0}};
    size_t words = 0;
    for (const auto& f : files) {
        if (f.deleted) continue;
        byType[f.type]++;
        words += f.wordCount;
    }
    std::ostringstream out;
    size_t visibleFiles = 0;
    for (const auto& file : files) if (!file.deleted) ++visibleFiles;
    out << "{\"totalFiles\":" << visibleFiles
        << ",\"pdfFiles\":" << byType["pdf"]
        << ",\"docxFiles\":" << byType["docx"]
        << ",\"pptxFiles\":" << byType["pptx"]
        << ",\"txtFiles\":" << byType["txt"]
        << ",\"totalWords\":" << words
        << ",\"totalSearches\":" << totalSearches
        << ",\"totalMatches\":" << totalMatches
        << ",\"recentSearches\":" << jsonArray(recentSearches) << "}";
    return {200, "application/json", out.str()};
}

std::string localDocumentJson(const LocalDocument& document) {
    std::ostringstream out;
    out << "{\"path\":" << jsonString(document.path)
        << ",\"name\":" << jsonString(document.name)
        << ",\"type\":" << jsonString(document.type)
        << ",\"status\":" << jsonString(document.status)
        << ",\"size\":" << document.size << "}";
    return out.str();
}

std::string localWarningsJson() {
    return jsonArray(localWarnings);
}

bool isAllowedLocalPath(const std::string& path) {
    fs::path requested(path);
    std::error_code error;
    fs::path canonical = fs::weakly_canonical(requested, error);
    if (error) return false;
    for (const auto& document : localDocuments) {
        fs::path allowed = fs::weakly_canonical(fs::path(document.path), error);
        if (!error && canonical == allowed) return true;
    }
    return false;
}

Response scanSelectedFolder(bool recursive) {
    if (selectedFolder.empty()) return {400, "application/json", "{\"error\":\"No folder selected\"}"};
    includeSubfolders = recursive;
    ScanResult scan = scanLocalPaths({selectedFolder}, includeSubfolders, (dataDir / "tmp").string());
    localDocuments = std::move(scan.documents);
    localWarnings = std::move(scan.warnings);
    std::ostringstream out;
    out << "{\"success\":true,\"path\":" << jsonString(selectedFolder)
        << ",\"filesFound\":" << localDocuments.size()
        << ",\"filesDiscovered\":" << scan.discovered
        << ",\"warnings\":" << localWarningsJson() << "}";
    return {200, "application/json", out.str()};
}

Response apiSelectFolder() {
    std::string path = fileExplorer::selectFolder();
    if (path.empty()) {
        std::string error = fileExplorer::lastError();
        if (!error.empty()) return {500, "application/json", "{\"success\":false,\"error\":" + jsonString(error) + "}"};
        return {200, "application/json", "{\"success\":false,\"cancelled\":true}"};
    }
    selectedFolder = path;
    selectedFiles.clear();
    localDocuments.clear();
    localWarnings.clear();
    return {200, "application/json", "{\"success\":true,\"path\":" + jsonString(path) + "}"};
}

Response apiSelectFiles() {
    selectedFiles = fileExplorer::selectFiles();
    if (selectedFiles.empty()) {
        std::string error = fileExplorer::lastError();
        if (!error.empty()) return {500, "application/json", "{\"success\":false,\"error\":" + jsonString(error) + "}"};
        return {200, "application/json", "{\"success\":false,\"cancelled\":true}"};
    }
    selectedFolder.clear();
    ScanResult scan = scanLocalPaths(selectedFiles, true, (dataDir / "tmp").string());
    localDocuments = std::move(scan.documents);
    localWarnings = std::move(scan.warnings);
    std::ostringstream out;
    out << "{\"success\":true,\"files\":[";
    for (size_t i = 0; i < selectedFiles.size(); ++i) {
        if (i) out << ",";
        out << jsonString(selectedFiles[i]);
    }
    out << "],\"filesFound\":" << localDocuments.size() << ",\"warnings\":" << localWarningsJson() << "}";
    return {200, "application/json", out.str()};
}

Response apiScan(const Request& req) {
    return scanSelectedFolder(getJsonBool(req.body, "includeSubfolders", true));
}

Response apiLocalSearch(const Request& req) {
    std::string originalQuery = getJsonString(req.body, "query");
    std::string query = originalQuery;
    std::string algorithm = getJsonString(req.body, "algorithm", "kmp");
    std::string mode = getJsonString(req.body, "mode", "simple");
    bool caseSensitive = getJsonBool(req.body, "caseSensitive", false);
    if (query.empty()) return {400, "application/json", "{\"error\":\"Search query cannot be empty\"}"};
    if (localDocuments.empty()) return {400, "application/json", "{\"error\":\"Select files or scan a folder first\"}"};
    if (mode == "pattern") { algorithm = "regex"; query = wildcardToRegex(query); }

    std::ostringstream results;
    size_t matches = 0;
    double elapsed = 0;
    results << "[";
    bool first = true;
    for (const auto& document : localDocuments) {
        MatchSet content;
        MatchSet name;
        try {
            content = algorithm == "regex" ? regexSearch(document.text, query, caseSensitive) : runAlgorithm(document.text, query, algorithm, caseSensitive);
            name = algorithm == "regex" ? regexSearch(document.name, query, caseSensitive) : runAlgorithm(document.name, originalQuery, algorithm, caseSensitive);
        } catch (...) {
            return {400, "application/json", "{\"error\":\"Invalid regex or search pattern\"}"};
        }
        if (content.positions.empty() && name.positions.empty()) continue;
        matches += content.positions.size() + name.positions.size();
        elapsed += content.milliseconds + name.milliseconds;
        if (!first) results << ",";
        first = false;
        results << "{\"fileId\":" << jsonString(document.path)
                << ",\"fileName\":" << jsonString(document.name)
                << ",\"filePath\":" << jsonString(document.path)
                << ",\"type\":" << jsonString(document.type)
                << ",\"matches\":" << content.positions.size() + name.positions.size()
                << ",\"nameMatch\":" << (name.positions.empty() ? "false" : "true")
                << ",\"positions\":[],\"contexts\":[";
        size_t limit = std::min<size_t>(8, content.positions.size());
        for (size_t i = 0; i < limit; ++i) {
            if (i) results << ",";
            std::string context = contextFor(document.text, content.positions[i], query.size());
            results << jsonString(highlightContext(context, mode == "pattern" || algorithm == "regex" ? "" : originalQuery, caseSensitive));
        }
        if (!name.positions.empty()) {
            if (limit) results << ",";
            results << jsonString("File name: " + document.name);
        }
        results << "]}";
    }
    results << "]";
    ++totalSearches;
    totalMatches += matches;
    recentSearches.insert(recentSearches.begin(), originalQuery);
    if (recentSearches.size() > 8) recentSearches.pop_back();
    std::ostringstream out;
    out << "{\"query\":" << jsonString(originalQuery)
        << ",\"algorithm\":" << jsonString(algorithm)
        << ",\"location\":" << jsonString(selectedFolder.empty() ? "Selected files" : selectedFolder)
        << ",\"filesSearched\":" << localDocuments.size()
        << ",\"totalMatches\":" << matches
        << ",\"executionTime\":" << elapsed
        << ",\"warnings\":" << localWarningsJson()
        << ",\"results\":" << results.str() << "}";
    return {200, "application/json", out.str()};
}

Response apiLocalExplorerAction(const Request& req, bool show) {
    std::string path = getJsonString(req.body, "path");
    if (!isAllowedLocalPath(path)) return {403, "application/json", "{\"error\":\"File was not selected by the user\"}"};
    bool success = show ? fileExplorer::showInExplorer(path) : fileExplorer::openFile(path);
    return {success ? 200 : 400, "application/json", std::string("{\"success\":") + (success ? "true" : "false") + "}"};
}

Response apiUpload(const Request& req) {
    // Upload limit: 5 GB per file. Files within the limit are saved.
    const size_t maxSize = 5ULL * 1024ULL * 1024ULL * 1024ULL;
    auto parts = parseMultipart(req);
    std::vector<std::string> uploaded;
    std::vector<std::string> errors;
    for (const auto& part : parts) {
        std::string type = extensionOf(part.filename);
        if (!isSupportedType(type)) {
            errors.push_back(part.filename + ": unsupported file type");
            continue;
        }
        if (part.data.size() > maxSize) {
            errors.push_back(part.filename + ": file exceeds 5 GB");
            continue;
        }
        FileInfo f;
        f.id = randomId();
        f.originalName = originalFileName(part.filename);
        f.type = type;
        f.size = part.data.size();
        f.uploadedAt = nowIso();
        f.status = "Processing";
        f.storedName = f.id + "_" + f.originalName;
        f.fingerprint = fingerprintOf(part.data);
        bool duplicate = false;
        for (const auto& existing : files) {
            if (!existing.deleted && existing.fingerprint == f.fingerprint && existing.size == f.size) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            errors.push_back(part.filename + ": duplicate file already exists");
            continue;
        }
        fs::path stored = uploadDir / f.storedName;
        std::error_code directoryError;
        fs::create_directories(uploadDir, directoryError);
        fs::create_directories(extractedDir, directoryError);
        if (directoryError) {
            errors.push_back(part.filename + ": could not create the backend storage directory");
            continue;
        }
        std::ofstream out(stored, std::ios::binary);
        if (!out) {
            errors.push_back(part.filename + ": could not save the file in the backend storage directory");
            continue;
        }
        out.write(part.data.data(), static_cast<std::streamsize>(part.data.size()));
        out.close();
        if (!out) {
            fs::remove(stored);
            errors.push_back(part.filename + ": could not finish saving the file in the backend storage directory");
            continue;
        }
        std::string text = extractText(stored, type, dataDir / "tmp");
        f.wordCount = countWords(text);
        f.extractedPath = (extractedDir / (f.id + ".txt")).string();
        writeTextFile(f.extractedPath, text);
        f.status = isImageType(type) ? "Indexed (filename only)" : (text.empty() ? "No readable text" : "Processed");
        files.push_back(f);
        uploaded.push_back(fileJson(f));
    }
    saveDatabase();
    std::ostringstream body;
    body << "{\"uploaded\":[";
    for (size_t i = 0; i < uploaded.size(); ++i) {
        if (i) body << ",";
        body << uploaded[i];
    }
    body << "],\"errors\":" << jsonArray(errors) << "}";
    return {200, "application/json", body.str()};
}

Response apiDelete(const std::string& id) {
    for (auto it = files.begin(); it != files.end(); ++it) {
        if (it->id == id) {
            it->deleted = true;
            it->status = "In Recycle Bin";
            saveDatabase();
            return {200, "application/json", "{\"ok\":true}"};
        }
    }
    return {404, "application/json", "{\"error\":\"File not found\"}"};
}

std::string mimeFor(const fs::path& path);

Response apiRename(const std::string& id, const Request& req) {
    std::string requestedName = originalFileName(getJsonString(req.body, "name"));
    if (requestedName.empty() || requestedName == "." || requestedName == ".." || requestedName.find_first_of("\\/\t\r\n") != std::string::npos) {
        return {400, "application/json", "{\"error\":\"Enter a valid file name\"}"};
    }
    for (const auto& file : files) {
        if (file.id != id && file.originalName == requestedName) {
            return {409, "application/json", "{\"error\":\"A file with that name already exists\"}"};
        }
    }
    for (auto& file : files) {
        if (file.id == id) {
            if (extensionOf(requestedName) != file.type) {
                return {400, "application/json", "{\"error\":\"Keep the original file extension\"}"};
            }
            file.originalName = requestedName;
            std::string requestedFolder = getJsonString(req.body, "folder", file.folder);
            std::string requestedTags = getJsonString(req.body, "tags", file.tags);
            if (!requestedFolder.empty()) file.folder = requestedFolder;
            file.tags = requestedTags;
            saveDatabase();
            return {200, "application/json", fileJson(file)};
        }
    }
    return {404, "application/json", "{\"error\":\"File not found\"}"};
}

Response apiOpen(const std::string& id, bool download) {
    for (const auto& file : files) {
        if (file.id == id && !file.deleted) {
            std::string disposition = download ? "attachment" : "inline";
            return {200, mimeFor(fs::path("file." + file.type)), readBinaryFile(uploadDir / file.storedName), {{"Content-Disposition", disposition + "; filename=\"" + jsonEscape(file.originalName) + "\""}}};
        }
    }
    return {404, "application/json", "{\"error\":\"File not found\"}"};
}

Response apiClear() {
    for (auto& f : files) f.deleted = true;
    saveDatabase();
    return {200, "application/json", "{\"ok\":true}"};
}

Response apiSearch(const Request& req, bool comparisonOnly = false) {
    std::string originalQuery = getJsonString(req.body, "query");
    std::string query = originalQuery;
    std::string algorithm = getJsonString(req.body, "algorithm", "kmp");
    std::string mode = getJsonString(req.body, "mode", "simple");
    bool caseSensitive = getJsonBool(req.body, "caseSensitive", false);
    auto fileTypes = getJsonArray(req.body, "fileTypes");
    auto fileIds = getJsonArray(req.body, "fileIds");
    if (query.empty()) return {400, "application/json", "{\"error\":\"Search query cannot be empty\"}"};
    if (mode == "pattern") {
        algorithm = "regex";
        query = wildcardToRegex(query);
    }
    std::set<std::string> typeSet(fileTypes.begin(), fileTypes.end());
    std::set<std::string> idSet(fileIds.begin(), fileIds.end());
    std::ostringstream results;
    size_t searched = 0, chars = 0, matches = 0;
    double elapsed = 0.0;
    results << "[";
    bool firstFile = true;
    for (const auto& f : files) {
        if (f.deleted) continue;
        if (!typeSet.empty() && !typeSet.count(f.type)) continue;
        if (!idSet.empty() && !idSet.count(f.id)) continue;
        std::string text = readBinaryFile(f.extractedPath);
        ++searched;
        chars += text.size();
        MatchSet found;
        MatchSet nameFound;
        try {
            found = algorithm == "regex" ? regexSearch(text, query, caseSensitive) : runAlgorithm(text, query, algorithm, caseSensitive);
            nameFound = algorithm == "regex" ? regexSearch(f.originalName, query, caseSensitive) : runAlgorithm(f.originalName, originalQuery, algorithm, caseSensitive);
        } catch (const std::exception& e) {
            return {400, "application/json", "{\"error\":\"Invalid regex or search pattern\"}"};
        }
        if (found.positions.empty() && nameFound.positions.empty()) {
            elapsed += found.milliseconds;
            continue;
        }
        matches += found.positions.size() + nameFound.positions.size();
        elapsed += found.milliseconds + nameFound.milliseconds;
        if (!firstFile) results << ",";
        firstFile = false;
        results << "{\"fileId\":" << jsonString(f.id)
                << ",\"fileName\":" << jsonString(f.originalName)
                << ",\"type\":" << jsonString(f.type)
                << ",\"matches\":" << found.positions.size() + nameFound.positions.size()
                << ",\"nameMatch\":" << (nameFound.positions.empty() ? "false" : "true")
                << ",\"positions\":[";
        for (size_t i = 0; i < found.positions.size(); ++i) {
            if (i) results << ",";
            results << found.positions[i];
        }
        results << "],\"contexts\":[";
        size_t limit = std::min<size_t>(8, found.positions.size());
        for (size_t i = 0; i < limit; ++i) {
            if (i) results << ",";
            std::string ctx = contextFor(text, found.positions[i], query.size());
            results << jsonString(highlightContext(ctx, mode == "pattern" || algorithm == "regex" ? "" : getJsonString(req.body, "query"), caseSensitive));
        }
        if (!nameFound.positions.empty()) {
            if (found.positions.size() > 0) results << ",";
            results << jsonString("File name: " + f.originalName);
        }
        results << "]}";
    }
    results << "]";
    if (!comparisonOnly) {
        ++totalSearches;
        totalMatches += matches;
        recentSearches.insert(recentSearches.begin(), getJsonString(req.body, "query"));
        if (recentSearches.size() > 8) recentSearches.pop_back();
    }
    std::ostringstream out;
    out << "{\"query\":" << jsonString(getJsonString(req.body, "query"))
        << ",\"algorithm\":" << jsonString(algorithm)
        << ",\"patternLength\":" << query.size()
        << ",\"filesSearched\":" << searched
        << ",\"charactersScanned\":" << chars
        << ",\"totalMatches\":" << matches
        << ",\"executionTime\":" << elapsed
        << ",\"results\":" << results.str() << "}";
    return {200, "application/json", out.str()};
}

Response apiCompare(const Request& req) {
    const std::vector<std::string> algorithms{"naive", "kmp", "rabin-karp", "boyer-moore"};
    std::ostringstream out;
    out << "{\"comparisons\":[";
    for (size_t i = 0; i < algorithms.size(); ++i) {
        Request copy = req;
        std::string json = req.body;
        std::regex algRe("\"algorithm\"\\s*:\\s*\"[^\"]*\"");
        if (std::regex_search(json, algRe)) json = std::regex_replace(json, algRe, "\"algorithm\":\"" + algorithms[i] + "\"");
        else json.insert(json.size() - 1, ",\"algorithm\":\"" + algorithms[i] + "\"");
        copy.body = json;
        Response r = apiSearch(copy, true);
        size_t mPos = r.body.find("\"totalMatches\":");
        size_t ePos = r.body.find("\"executionTime\":");
        std::string matches = "0", time = "0";
        if (mPos != std::string::npos) {
            size_t start = mPos + 15, end = r.body.find(',', start);
            matches = r.body.substr(start, end - start);
        }
        if (ePos != std::string::npos) {
            size_t start = ePos + 16, end = r.body.find(',', start);
            time = r.body.substr(start, end - start);
        }
        if (i) out << ",";
        out << "{\"algorithm\":" << jsonString(algorithms[i]) << ",\"matches\":" << matches << ",\"executionTime\":" << time << "}";
    }
    out << "]}";
    return {200, "application/json", out.str()};
}

std::string mimeFor(const fs::path& path) {
    std::string ext = extensionOf(path.string());
    if (ext == "html") return "text/html";
    if (ext == "css") return "text/css";
    if (ext == "js") return "application/javascript";
    if (ext == "svg") return "image/svg+xml";
    if (ext == "txt") return "text/plain";
    if (ext == "csv" || ext == "md" || ext == "log" || ext == "rtf") return "text/plain";
    if (ext == "json") return "application/json";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "png") return "image/png";
    if (ext == "gif") return "image/gif";
    if (ext == "bmp") return "image/bmp";
    if (ext == "webp") return "image/webp";
    if (ext == "pdf") return "application/pdf";
    if (ext == "docx") return "application/vnd.openxmlformats-officedocument.wordprocessingml.document";
    if (ext == "pptx") return "application/vnd.openxmlformats-officedocument.presentationml.presentation";
    return "application/octet-stream";
}

Response staticFile(const std::string& requestPath) {
    std::string rel = requestPath == "/" ? "/index.html" : requestPath;
    if (rel.find("..") != std::string::npos) return {403, "text/plain", "Forbidden"};
    fs::path path = root / "frontend" / rel.substr(1);
    if (!fs::exists(path)) return {404, "text/plain", "Not found"};
    return {200, mimeFor(path), readBinaryFile(path)};
}

Response route(const Request& req) {
    if (req.method == "POST" && req.path == "/api/auth/signup") return apiSignup(req);
    if (req.method == "POST" && req.path == "/api/auth/login") return apiLogin(req);
    if (req.method == "POST" && req.path == "/api/auth/logout") return apiLogout();
    if (req.method == "GET" && req.path == "/api/auth/me") return apiMe();
    // All resource APIs require login. Static frontend stays public.
    bool protectedApi = req.path.rfind("/api/", 0) == 0;
    if (protectedApi && !authUser) return requireAuth();
    if (req.method == "POST" && req.path == "/api/explorer/select-folder") return apiSelectFolder();
    if (req.method == "POST" && req.path == "/api/explorer/select-files") return apiSelectFiles();
    if (req.method == "POST" && req.path == "/api/explorer/scan") return apiScan(req);
    if (req.method == "POST" && req.path == "/api/search/local") return apiLocalSearch(req);
    if (req.method == "POST" && req.path == "/api/explorer/open-file") return apiLocalExplorerAction(req, false);
    if (req.method == "POST" && req.path == "/api/explorer/show-in-explorer") return apiLocalExplorerAction(req, true);
    if (req.method == "GET" && req.path == "/api/files") return apiFiles();
    if (req.method == "GET" && req.path == "/api/trash") return apiTrash();
    if (req.method == "GET" && req.path == "/api/stats") return apiStats();
    if (req.method == "POST" && req.path == "/api/upload") return apiUpload(req);
    if (req.method == "POST" && req.path == "/api/search") return apiSearch(req);
    if (req.method == "POST" && req.path == "/api/compare") return apiCompare(req);
    if (req.method == "PATCH" && req.path.rfind("/api/files/", 0) == 0) return apiRename(req.path.substr(11), req);
    if (req.method == "POST" && req.path.rfind("/api/files/", 0) == 0 && endsWith(req.path, "/restore")) return apiRestore(req.path.substr(11, req.path.size() - 19));
    if (req.method == "GET" && req.path.rfind("/api/files/", 0) == 0 && endsWith(req.path, "/open")) return apiOpen(req.path.substr(11, req.path.size() - 16), false);
    if (req.method == "GET" && req.path.rfind("/api/files/", 0) == 0 && endsWith(req.path, "/download")) return apiOpen(req.path.substr(11, req.path.size() - 20), true);
    if (req.method == "DELETE" && req.path == "/api/files") return apiClear();
    if (req.method == "DELETE" && req.path.rfind("/api/files/", 0) == 0) return apiDelete(req.path.substr(11));
    if (req.method == "GET") return staticFile(req.path);
    return {404, "application/json", "{\"error\":\"Route not found\"}"};
}

void closeSocket(socket_t s) {
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
}

Request readRequest(socket_t client) {
    std::string data;
    char buffer[8192];
    while (data.find("\r\n\r\n") == std::string::npos) {
        int n = recv(client, buffer, sizeof(buffer), 0);
        if (n <= 0) break;
        data.append(buffer, n);
    }
    size_t headerEnd = data.find("\r\n\r\n");
    if (headerEnd == std::string::npos) return {};
    Request req;
    std::istringstream head(data.substr(0, headerEnd));
    head >> req.method >> req.path;
    std::string version;
    head >> version;
    std::string line;
    std::getline(head, line);
    while (std::getline(head, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        while (!value.empty() && value.front() == ' ') value.erase(value.begin());
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        req.headers[key] = value;
    }
    size_t contentLength = 0;
    if (req.headers.count("content-length")) contentLength = static_cast<size_t>(std::stoull(req.headers["content-length"]));
    req.body = data.substr(headerEnd + 4);
    while (req.body.size() < contentLength) {
        int n = recv(client, buffer, sizeof(buffer), 0);
        if (n <= 0) break;
        req.body.append(buffer, n);
    }
    size_t q = req.path.find('?');
    if (q != std::string::npos) {
        std::string query = req.path.substr(q + 1);
        size_t workspaceParam = query.find("workspace=");
        if (workspaceParam != std::string::npos) {
            std::string value = query.substr(workspaceParam + 10);
            size_t ampersand = value.find('&');
            if (ampersand != std::string::npos) value = value.substr(0, ampersand);
            value = urlDecode(value);
            if (validWorkspaceId(value)) req.headers["x-workspace-id"] = value;
        }
        // Allow auth token via query so Preview/Download (window.open) works logged-in.
        for (const std::string& key : {"token=", "authToken=", "auth-token="}) {
            size_t p = query.find(key);
            if (p != std::string::npos) {
                std::string value = query.substr(p + key.size());
                size_t ampersand = value.find('&');
                if (ampersand != std::string::npos) value = value.substr(0, ampersand);
                value = urlDecode(value);
                if (!value.empty() && req.headers.find("authorization") == req.headers.end())
                    req.headers["authorization"] = "Bearer " + value;
                break;
            }
        }
        req.path = req.path.substr(0, q);
    }
    req.path = urlDecode(req.path);
    return req;
}

void sendResponse(socket_t client, const Response& res) {
    std::ostringstream head;
    std::string statusText = res.status == 200 ? "OK" : res.status == 400 ? "Bad Request" : res.status == 401 ? "Unauthorized" : res.status == 403 ? "Forbidden" : res.status == 404 ? "Not Found" : res.status == 409 ? "Conflict" : "Server Error";
    head << "HTTP/1.1 " << res.status << " " << statusText << "\r\n"
         << "Content-Type: " << res.contentType << "; charset=utf-8\r\n"
         << "Content-Length: " << res.body.size() << "\r\n"
         << "Access-Control-Allow-Origin: *\r\n"
         << "Access-Control-Allow-Methods: GET,POST,PATCH,DELETE,OPTIONS\r\n"
         << "Access-Control-Allow-Headers: Content-Type, X-Workspace-Id, X-Auth-Token, Authorization\r\n"
            << "Access-Control-Expose-Headers: Content-Disposition\r\n";
        for (const auto& [key, value] : res.headers) head << key << ": " << value << "\r\n";
        head << "Connection: close\r\n\r\n";
        std::string payload = head.str() + res.body;
        size_t sent = 0;
        while (sent < payload.size()) {
        size_t remaining = payload.size() - sent;
        int chunkSize = static_cast<int>(std::min(remaining, static_cast<size_t>(std::numeric_limits<int>::max())));
    #ifdef __linux__
        int count = send(client, payload.data() + sent, chunkSize, MSG_NOSIGNAL);
    #else
        int count = send(client, payload.data() + sent, chunkSize, 0);
    #endif
        if (count <= 0) break;
        sent += static_cast<size_t>(count);
        }
}
    const char* envPort = std::getenv("PORT");
    int port = 8080;
    if (envPort) {
        try {
            size_t parsed = 0;
            port = std::stoi(envPort, &parsed);
            if (parsed != std::string(envPort).size() || port < 1 || port > 65535) {
                throw std::invalid_argument("PORT must be between 1 and 65535");
            }
        } catch (const std::exception&) {
            std::cerr << "Invalid PORT value: " << envPort << " (expected 1-65535)\n";
            return 1;
        }
    }

int main() {
    fs::create_directories(root / "workspaces");
    fs::create_directories(root / "data");
    loadUsers();
    loadSessions();
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    socket_t server = socket(AF_INET, SOCK_STREAM, 0);
    if (server == INVALID_SOCKET) {
        std::cerr << "Unable to create server socket\n";
        return 1;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<unsigned short>(port));
    int yes = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
    if (bind(server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR || listen(server, 16) == SOCKET_ERROR) {
        std::cerr << "Port " << port << " is unavailable\n";
        closeSocket(server);
        return 1;
    }
    std::cout << "FileFind listening on port " << port << " (local: http://localhost:" << port << ")\n";
    while (true) {
        socket_t client = accept(server, nullptr, nullptr);
        if (client == INVALID_SOCKET) continue;
        try {
            Request req = readRequest(client);
            if (req.method == "OPTIONS") sendResponse(client, {200, "text/plain", ""});
            else {
                resolveAuth(req);
                selectWorkspace(req);
                sendResponse(client, route(req));
            }
        } catch (const std::exception& e) {
            sendResponse(client, {500, "application/json", std::string("{\"error\":\"") + jsonEscape(e.what()) + "\"}"});
        }
        closeSocket(client);
    }
}
