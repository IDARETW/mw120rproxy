// zone_source.cpp — implementation of the frozen zone_source.h contract.
#include "zone_source.h"
#include "../common/fs_util.h"
#include "../common/log.h"
#include <cstring>
#include <sstream>

namespace iw3sr {

using zt::path_join;
using zt::mkdirs;
using zt::write_file;
using zt::read_file;
using zt::write_file_str;
using zt::read_file_str;
using zt::file_exists;

ZoneSource::ZoneSource(const std::string& assetDir, const std::string& mapName, bool create)
    : dir_(assetDir), map_(mapName) {
    if (create) {
        mkdirs(dir_);
        mkdirs(abs("images"));
        mkdirs(abs("materials"));
        mkdirs(abs("xmodel"));
        mkdirs(abs("xsurface"));
        mkdirs(abs("maps/mp"));
    }
}

std::string ZoneSource::abs(const std::string& rel) const {
    return path_join(dir_, rel);
}
std::string ZoneSource::manifestPath() const {
    return abs(map_ + ".csv");
}

// ---- manifest ------------------------------------------------------------------------------------
void ZoneSource::manifestAdd(const std::string& iw8Type, const std::string& name) {
    entries_.push_back({iw8Type, name});
}

bool ZoneSource::manifestWrite() {
    std::ostringstream os;
    for (const auto& e : entries_)
        os << e.type << "," << e.name << "\n";
    std::string s = os.str();
    if (!write_file_str(manifestPath(), s)) {
        zt::err("zonesrc: cannot write manifest %s", manifestPath().c_str());
        return false;
    }
    zt::info("zonesrc: wrote manifest %s (%zu assets)", manifestPath().c_str(), entries_.size());
    return true;
}

bool ZoneSource::manifestRead() {
    std::string s;
    if (!read_file_str(manifestPath(), s)) {
        zt::err("zonesrc: cannot read manifest %s", manifestPath().c_str());
        return false;
    }
    entries_.clear();
    std::istringstream is(s);
    std::string line;
    while (std::getline(is, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            continue;
        size_t c = line.find(',');
        if (c == std::string::npos) {
            zt::warn("zonesrc: skipping malformed manifest line '%s'", line.c_str());
            continue;
        }
        entries_.push_back({line.substr(0, c), line.substr(c + 1)});
    }
    zt::info("zonesrc: read manifest %s (%zu assets)", manifestPath().c_str(), entries_.size());
    return true;
}

// ---- raw payloads --------------------------------------------------------------------------------
bool ZoneSource::writeBlob(const std::string& relPath, const void* data, size_t n) {
    return write_file(abs(relPath), data, n);
}
bool ZoneSource::writeBlob(const std::string& relPath, const std::vector<uint8_t>& data) {
    return write_file(abs(relPath), data);
}
bool ZoneSource::readBlob(const std::string& relPath, std::vector<uint8_t>& out) const {
    return read_file(abs(relPath), out);
}
bool ZoneSource::writeText(const std::string& relPath, const std::string& text) {
    return write_file_str(abs(relPath), text);
}
bool ZoneSource::readText(const std::string& relPath, std::string& out) const {
    return read_file_str(abs(relPath), out);
}
bool ZoneSource::blobExists(const std::string& relPath) const {
    return file_exists(abs(relPath));
}

// ---- typed paths ---------------------------------------------------------------------------------
std::string ZoneSource::imagePath(const std::string& name) const {
    return "images/" + name + ".iwi";
}
std::string ZoneSource::materialPath(const std::string& name) const {
    return "materials/" + name + ".json";
}
std::string ZoneSource::xmodelPath(const std::string& name) const {
    return "xmodel/" + name + ".json";
}
std::string ZoneSource::xsurfacePath(const std::string& name) const {
    return "xsurface/" + name + ".xsurf_bin";
}
std::string ZoneSource::mapSubPath(const std::string& sub) const {
    return "maps/mp/" + map_ + ".d3dbsp." + sub;
}
std::string ZoneSource::dynentPath() const {
    return "dynentitylist0.dynent.data";
}

// ---- typed convenience ---------------------------------------------------------------------------
bool ZoneSource::addImage(const std::string& name, const std::vector<uint8_t>& iwiBytes) {
    manifestAdd("image", name);
    return writeBlob(imagePath(name), iwiBytes);
}
bool ZoneSource::getImage(const std::string& name, std::vector<uint8_t>& out) const {
    return readBlob(imagePath(name), out);
}
bool ZoneSource::addMaterial(const std::string& name, const std::string& json) {
    manifestAdd("material", name);
    return writeText(materialPath(name), json);
}
bool ZoneSource::getMaterial(const std::string& name, std::string& out) const {
    return readText(materialPath(name), out);
}
bool ZoneSource::addXModel(const std::string& name, const std::string& json) {
    manifestAdd("xmodel", name);
    return writeText(xmodelPath(name), json);
}
bool ZoneSource::getXModel(const std::string& name, std::string& out) const {
    return readText(xmodelPath(name), out);
}
bool ZoneSource::addXSurface(const std::string& name, const std::vector<uint8_t>& binBytes) {
    manifestAdd("xmodelsurfs", name);
    return writeBlob(xsurfacePath(name), binBytes);
}
bool ZoneSource::getXSurface(const std::string& name, std::vector<uint8_t>& out) const {
    return readBlob(xsurfacePath(name), out);
}

bool ZoneSource::addMapSubText(const std::string& iw8Type,
                               const std::string& sub,
                               const std::string& text) {
    if (!iw8Type.empty()) {
        std::string assetName = "maps/mp/" + map_ + ".d3dbsp";
        manifestAdd(iw8Type, assetName);
    }
    return writeText(mapSubPath(sub), text);
}
bool ZoneSource::getMapSubText(const std::string& sub, std::string& out) const {
    return readText(mapSubPath(sub), out);
}

bool ZoneSource::writeDynents(const std::vector<uint8_t>& data) {
    return writeBlob(dynentPath(), data);
}
bool ZoneSource::readDynents(std::vector<uint8_t>& out) const {
    return readBlob(dynentPath(), out);
}

// ---- self-describing blob header -----------------------------------------------------------------
// layout: char magic[8] (NUL-padded) | u32 version | u32 payloadLen | payload[payloadLen]
std::vector<uint8_t>
ZoneSource::wrapBlob(const char* magic, uint32_t version, const void* payload, size_t n) {
    std::vector<uint8_t> b;
    b.resize(16 + n);
    std::memset(b.data(), 0, 8);
    std::strncpy(reinterpret_cast<char*>(b.data()), magic, 7);
    std::memcpy(b.data() + 8, &version, 4);
    uint32_t len = static_cast<uint32_t>(n);
    std::memcpy(b.data() + 12, &len, 4);
    if (n)
        std::memcpy(b.data() + 16, payload, n);
    return b;
}

bool ZoneSource::unwrapBlob(const std::vector<uint8_t>& blob,
                            std::string& magicOut,
                            uint32_t& versionOut,
                            std::vector<uint8_t>& payloadOut) {
    if (blob.size() < 16)
        return false;
    char m[9] = {0};
    std::memcpy(m, blob.data(), 8);
    magicOut = m;
    std::memcpy(&versionOut, blob.data() + 8, 4);
    uint32_t len = 0;
    std::memcpy(&len, blob.data() + 12, 4);
    if (16 + static_cast<size_t>(len) > blob.size())
        return false;
    payloadOut.assign(blob.begin() + 16, blob.begin() + 16 + len);
    return true;
}

} // namespace iw3sr
