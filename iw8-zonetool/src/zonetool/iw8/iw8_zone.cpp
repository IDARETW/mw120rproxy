#include "iw8_zone.h"
#include "../../common/log.h"
#include <algorithm>

namespace iw8
{

std::string ZoneWriter::assetKey(const IW8_XAssetType type, const std::string &name)
{
    return std::to_string(static_cast<unsigned>(type)) + ":" + name;
}

uint64_t ZoneWriter::assetAlias(const IW8_XAssetType type, const std::string &name) const
{
    const auto found = assetAliases_.find(assetKey(type, name));
    if (found == assetAliases_.end())
    {
        throw std::runtime_error("asset dependency was not emitted before its reference: " + name);
    }
    return found->second;
}

uint32_t ZoneWriter::internScriptString(const std::string &text)
{
    if (scriptStringsWritten_)
        throw std::runtime_error("script strings must be registered before building the zone");
    if (text.find('\0') != std::string::npos)
        throw std::runtime_error("script string contains an embedded null");
    const auto found = std::find(scriptStrings_.begin(), scriptStrings_.end(), text);
    if (found != scriptStrings_.end())
        return static_cast<uint32_t>(found - scriptStrings_.begin() + 1);
    if (scriptStrings_.size() >= INT32_MAX - 1)
        throw std::runtime_error("too many script strings for Replay");
    scriptStrings_.push_back(text);
    return static_cast<uint32_t>(scriptStrings_.size());
}

void ZoneWriter::build()
{
    scriptStringsWritten_ = true;
    const uint32_t n = static_cast<uint32_t>(entries_.size());

    // 1) XAssetList root -> TEMP(0). DB_ReadXFile(.., 0x20) reads it first from block 0.
    zb_.pushStream(XFILE_BLOCK_TEMP);
    IW8_XAssetList al{};
    al.stringList.count =
        scriptStrings_.empty() ? 0 : static_cast<int32_t>(scriptStrings_.size() + 1);
    al.stringList.loaded = 0;
    al.stringList.strings = scriptStrings_.empty() ? PTR_NULL : PTR_FOLLOWS;
    al.assetCount = n;
    al.assetReadPos = 0;
    al.assets = (n > 0) ? PTR_FOLLOWS : PTR_NULL;
    zb_.writeT(al);
    zb_.popStream();

    if (!scriptStrings_.empty())
    {
        // Load_ScriptStringList (Replay RVA DD1E90): pointer array, then XStrings.
        zb_.pushStream(XFILE_BLOCK_VIRTUAL);
        zb_.align(7);
        zb_.writeT<uint64_t>(PTR_NULL);
        for (size_t i = 0; i < scriptStrings_.size(); ++i)
            zb_.writeT<uint64_t>(PTR_FOLLOWS);
        for (const auto &text : scriptStrings_)
            zb_.writeStr(text.c_str());
        zb_.popStream();
    }

    if (n == 0)
    {
        zt::info("iw8 zone: 0 assets (valid-empty)");
        return;
    }

    // 2) XAsset[n] -> VIRTUAL(5). header=-3 (insert: registers the asset pointer).
    zb_.pushStream(XFILE_BLOCK_VIRTUAL);
    zb_.align(7);
    for (const auto &e : entries_)
    {
        IW8_XAsset xa{};
        xa.type = e.type;
        xa._pad04 = 0;
        xa.header = PTR_INSERT;
        zb_.writeT(xa);
    }

    // 3) per-asset bodies, in registration order (load-read order).
    assetAliases_.clear();
    for (const auto &e : entries_)
    {
        zt::debug("iw8 zone: asset %s '%s'", iw8sz::type_name(e.type), e.name.c_str());
        // DB_InsertPointer RVA 0x11B2390 reserves an aligned VIRTUAL slot.
        // It consumes no fastfile bytes.
        zb_.align(7);
        const uint64_t insertionOffset = zb_.streamSize(XFILE_BLOCK_VIRTUAL);
        if (insertionOffset >= UINT32_MAX)
            throw std::runtime_error("asset insertion offset exceeds Replay's packed range");
        const uint64_t alias =
            (static_cast<uint64_t>(XFILE_BLOCK_VIRTUAL) << 32) | (insertionOffset + 1);
        if (!assetAliases_.try_emplace(assetKey(e.type, e.name), alias).second)
            throw std::runtime_error("duplicate zone asset " +
                                     std::string(iw8sz::type_name(e.type)) + " '" + e.name + "'");
        zb_.reserveCalc(8);
        if (e.body)
            e.body(*this);
    }

    zb_.popStream(); // VIRTUAL -> TEMP
}

void buildEmptyZone(ZoneBuffer &zb)
{
    zb.pushStream(XFILE_BLOCK_TEMP);
    IW8_XAssetList al{};
    al.stringList.count = 0;
    al.stringList.loaded = 0;
    al.stringList.strings = PTR_NULL;
    al.assetCount = 0;
    al.assetReadPos = 0;
    al.assets = PTR_NULL;
    zb.writeT(al);
    zb.popStream();
}

} // namespace iw8
