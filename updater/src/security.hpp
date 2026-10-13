#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <regex>
#include <vector>
#include <stdexcept>
#include <optional>
#include <algorithm>
#include <cstdint>
#include "../vendor/json.hpp"

namespace qip {
namespace fs = std::filesystem;
using json = nlohmann::json;
using Bytes = std::vector<unsigned char>;
inline void require(bool condition, const char* error) { if (!condition) throw std::runtime_error(error); }
inline std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0);
    require(n > 0, "invalid_utf8"); std::wstring r(n, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), r.data(), n); return r;
}
inline std::string utf8(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    require(n > 0, "invalid_unicode"); std::string r(n, 0);
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), (int)s.size(), r.data(), n, nullptr, nullptr); return r;
}
inline std::string read(const fs::path& p, size_t limit = 1024 * 1024) {
    require(fs::is_regular_file(p) && fs::file_size(p) <= limit, "file_size");
    std::ifstream f(p, std::ios::binary); require(bool(f), "file_read");
    std::string r((std::istreambuf_iterator<char>(f)), {}); require(!f.bad(), "file_read"); return r;
}
inline std::string hex(const Bytes& b) {
    const char* digits = "0123456789abcdef"; std::string r;
    for (auto c : b) { r += digits[c >> 4]; r += digits[c & 15]; } return r;
}
inline Bytes unhex(const std::string& s) {
    require(s.size() % 2 == 0, "hex"); Bytes b;
    auto digit = [](char c) { if (c >= '0' && c <= '9') return c - '0'; if (c >= 'a' && c <= 'f') return c - 'a' + 10; if (c >= 'A' && c <= 'F') return c - 'A' + 10; throw std::runtime_error("hex"); };
    for (size_t i = 0; i < s.size(); i += 2) b.push_back((unsigned char)((digit(s[i]) << 4) | digit(s[i+1]))); return b;
}
class Sha256 {
    BCRYPT_ALG_HANDLE algorithm = nullptr; BCRYPT_HASH_HANDLE hash = nullptr; Bytes object;
public:
    Sha256() {
        require(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0, "hash_init");
        DWORD length = 0, count = 0;
        require(BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, (PUCHAR)&length, sizeof(length), &count, 0) == 0, "hash_init"); object.resize(length);
        require(BCryptCreateHash(algorithm, &hash, object.data(), length, nullptr, 0, 0) == 0, "hash_init");
    }
    Sha256(const Sha256&) = delete;
    ~Sha256() { if (hash) BCryptDestroyHash(hash); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); }
    void update(const unsigned char* data, size_t size) { require(size <= MAXDWORD && BCryptHashData(hash, const_cast<PUCHAR>(data), (ULONG)size, 0) == 0, "hash_update"); }
    Bytes finish() { Bytes result(32); require(BCryptFinishHash(hash, result.data(), 32, 0) == 0, "hash_finish"); return result; }
};
inline Bytes digest(const std::string& s) { Sha256 h; h.update((const unsigned char*)s.data(), s.size()); return h.finish(); }
inline std::string hashFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary); require(bool(f), "file_hash"); Sha256 h; unsigned char b[65536];
    while (f) { f.read((char*)b, sizeof(b)); auto n = f.gcount(); if (n > 0) h.update(b, (size_t)n); }
    require(!f.bad(), "file_hash"); return hex(h.finish());
}
inline bool verifyRsa(const std::string& data, const std::string& signatureBase64, const std::string& modulusHex) {
    DWORD size = 0;
    if (!CryptStringToBinaryA(signatureBase64.c_str(), (DWORD)signatureBase64.size(), CRYPT_STRING_BASE64, nullptr, &size, nullptr, nullptr)) return false;
    Bytes signature(size);
    if (!CryptStringToBinaryA(signatureBase64.c_str(), (DWORD)signatureBase64.size(), CRYPT_STRING_BASE64, signature.data(), &size, nullptr, nullptr)) return false;
    auto modulus = unhex(modulusHex); require(modulus.size() == 384, "key_size");
    BCRYPT_RSAKEY_BLOB header{BCRYPT_RSAPUBLIC_MAGIC, 3072, 3, 384, 0, 0};
    Bytes blob(sizeof(header)); memcpy(blob.data(), &header, sizeof(header));
    blob.insert(blob.end(), {1, 0, 1}); blob.insert(blob.end(), modulus.begin(), modulus.end());
    BCRYPT_ALG_HANDLE algorithm = nullptr; BCRYPT_KEY_HANDLE key = nullptr;
    require(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_RSA_ALGORITHM, nullptr, 0) == 0, "rsa_init");
    auto status = BCryptImportKeyPair(algorithm, nullptr, BCRYPT_RSAPUBLIC_BLOB, &key, blob.data(), (ULONG)blob.size(), 0);
    bool result = false;
    if (status == 0) { auto hash = digest(data); BCRYPT_PKCS1_PADDING_INFO padding{BCRYPT_SHA256_ALGORITHM}; result = BCryptVerifySignature(key, &padding, hash.data(), (ULONG)hash.size(), signature.data(), size, BCRYPT_PAD_PKCS1) == 0; BCryptDestroyKey(key); }
    BCryptCloseAlgorithmProvider(algorithm, 0); return result;
}
struct SemVer {
    uint64_t major, minor, patch; std::vector<std::string> pre;
    static SemVer parse(const std::string& text) {
        require(text.size() <= 128, "semver");
        static const std::regex pattern(R"(^([0-9]+)\.([0-9]+)\.([0-9]+)(?:-([0-9A-Za-z.-]+))?(?:\+([0-9A-Za-z.-]+))?$)");
        std::smatch m; require(std::regex_match(text, m, pattern), "semver");
        auto number = [](const std::string& s) { require(s.size() < 20 && (s.size() == 1 || s[0] != '0'), "semver_number"); return std::stoull(s); };
        SemVer v{number(m[1]), number(m[2]), number(m[3]), {}};
        auto parts = [](const std::string& s) { std::vector<std::string> r; std::stringstream in(s); std::string p; while (std::getline(in,p,'.')) { require(!p.empty(), "semver_identifier"); r.push_back(p); } require(!s.empty() && s.back() != '.', "semver_identifier"); return r; };
        if (m[4].matched) { v.pre = parts(m[4]); for (auto& p : v.pre) if (p.find_first_not_of("0123456789") == std::string::npos) require(p.size()==1 || p[0]!='0', "semver_identifier"); }
        if (m[5].matched) parts(m[5]); return v;
    }
    int compare(const SemVer& b) const {
        if (major != b.major) return major < b.major ? -1 : 1;
        if (minor != b.minor) return minor < b.minor ? -1 : 1;
        if (patch != b.patch) return patch < b.patch ? -1 : 1;
        if (pre.empty() != b.pre.empty()) return pre.empty() ? 1 : -1;
        for (size_t i=0; i<std::min(pre.size(),b.pre.size()); ++i) {
            auto& a=pre[i]; auto& c=b.pre[i]; if (a==c) continue;
            bool an=a.find_first_not_of("0123456789")==std::string::npos, cn=c.find_first_not_of("0123456789")==std::string::npos;
            if (an != cn) return an ? -1 : 1;
            if (an && a.size()!=c.size()) return a.size()<c.size() ? -1 : 1;
            return a<c ? -1 : 1;
        }
        return pre.size()==b.pre.size() ? 0 : (pre.size()<b.pre.size() ? -1 : 1);
    }
};
inline bool newer(const std::string& a, const std::string& b) { return SemVer::parse(a).compare(SemVer::parse(b)) > 0; }
inline bool safeRelative(const std::string& s) {
    if (s.empty() || s.size() > 220 || s.front()=='/' || s.find('\\')!=std::string::npos || s.find(':')!=std::string::npos || s.back()=='/' || std::any_of(s.begin(),s.end(),[](unsigned char c){return c<32 || c==127;})) return false;
    std::stringstream in(s); std::string p;
    while (std::getline(in,p,'/')) {
        if (p.empty() || p=="." || p==".." || p.back()=='.' || p.back()==' ' || p.find_first_of("<>\"|?*\r\n")!=std::string::npos) return false;
        std::string upper=p; std::transform(upper.begin(),upper.end(),upper.begin(),[](unsigned char c){return (char)std::toupper(c);});
        auto stem=upper.substr(0,upper.find('.'));
        if (stem=="CON" || stem=="PRN" || stem=="AUX" || stem=="NUL" || std::regex_match(stem,std::regex("(COM|LPT)[0-9]"))) return false;
    } return true;
}
inline bool mutablePath(std::string s) {
    std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return (char)std::tolower(c);});
    return s=="users" || s.starts_with("users/") || s=="li/current.cfg" || s=="li/langs.cfg" || s=="skins/current.cfg" || s=="skins/skins.cfg";
}
}
