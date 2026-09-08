// fs_util.cpp — implementation of fs_util.h using <filesystem> + <cstdio>.
#include "fs_util.h"
#include "log.h"
#include <cstdio>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace zt {

bool read_file(const std::string& path, std::vector<uint8_t>& out) {
    out.clear();
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n < 0) { std::fclose(f); return false; }
    out.resize(static_cast<size_t>(n));
    size_t r = (n > 0) ? std::fread(out.data(), 1, static_cast<size_t>(n), f) : 0;
    out.resize(r);
    std::fclose(f);
    return true;
}

bool read_file_str(const std::string& path, std::string& out) {
    std::vector<uint8_t> b;
    if (!read_file(path, b)) { out.clear(); return false; }
    out.assign(reinterpret_cast<const char*>(b.data()), b.size());
    return true;
}

bool write_file(const std::string& path, const void* data, size_t n) {
    std::string dir = path_dir(path);
    if (!dir.empty()) mkdirs(dir);
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    size_t w = (n > 0) ? std::fwrite(data, 1, n, f) : 0;
    std::fclose(f);
    return w == n;
}

bool write_file(const std::string& path, const std::vector<uint8_t>& data) {
    return write_file(path, data.data(), data.size());
}

bool write_file_str(const std::string& path, const std::string& data) {
    return write_file(path, data.data(), data.size());
}

bool mkdirs(const std::string& dir) {
    if (dir.empty()) return true;
    std::error_code ec;
    fs::path p = fs::path(dir);
    fs::create_directories(p, ec);
    if (ec) {
        // create_directories returns false (no error) when the dir already exists; only ec is fatal.
        std::error_code ec2;
        return fs::exists(p, ec2);
    }
    return true;
}

std::string path_join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    char last = a.back();
    bool aSep = (last == '/' || last == '\\');
    bool bSep = (b.front() == '/' || b.front() == '\\');
    if (aSep && bSep) return a + b.substr(1);
    if (!aSep && !bSep) return a + "/" + b;
    return a + b;
}

static size_t last_sep(const std::string& p) {
    size_t s = p.find_last_of("/\\");
    return s;
}

std::string path_dir(const std::string& path) {
    size_t s = last_sep(path);
    return (s == std::string::npos) ? std::string() : path.substr(0, s);
}

std::string path_filename(const std::string& path) {
    size_t s = last_sep(path);
    return (s == std::string::npos) ? path : path.substr(s + 1);
}

std::string path_stem(const std::string& path) {
    std::string fn = path_filename(path);
    size_t d = fn.find_last_of('.');
    return (d == std::string::npos) ? fn : fn.substr(0, d);
}

bool file_exists(const std::string& path) {
    std::error_code ec;
    return fs::is_regular_file(fs::path(path), ec);
}

bool list_dir(const std::string& dir, std::vector<std::string>& out) {
    out.clear();
    std::error_code ec;
    fs::directory_iterator it(fs::path(dir), ec);
    if (ec) return false;
    for (const auto& e : it) {
        out.push_back(e.path().filename().string());
    }
    return true;
}

} // namespace zt
