// fs_util.h — minimal filesystem helpers (binary file IO, dir creation, path join, listing).
// Self-contained; uses <filesystem> under the hood. All paths are std::string (UTF-8/ANSI on disk).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace zt {

// Read an entire file as raw bytes. Returns false (and clears out) if the file cannot be opened.
bool read_file(const std::string& path, std::vector<uint8_t>& out);
// Convenience: read into a std::string (binary). Empty + false on failure.
bool read_file_str(const std::string& path, std::string& out);

// Write raw bytes to a file (binary, truncating). Creates parent dirs. Returns false on failure.
bool write_file(const std::string& path, const void* data, size_t n);
bool write_file(const std::string& path, const std::vector<uint8_t>& data);
bool write_file_str(const std::string& path, const std::string& data);

// Recursively create a directory (and parents). No-op if it already exists. Returns success.
bool mkdirs(const std::string& dir);

// Join two path components with the platform separator (handles trailing/leading separators).
std::string path_join(const std::string& a, const std::string& b);

// Directory of a path (everything before the last separator), or "" if none.
std::string path_dir(const std::string& path);
// Filename (everything after the last separator).
std::string path_filename(const std::string& path);
// Filename without its final extension.
std::string path_stem(const std::string& path);

// True if a regular file exists at path.
bool file_exists(const std::string& path);

// List entries directly under dir (names only, not recursive). Returns false if dir is unreadable.
bool list_dir(const std::string& dir, std::vector<std::string>& out);

} // namespace zt
