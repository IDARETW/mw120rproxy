#include "iw8_zone.h"
#include "../../common/log.h"

namespace iw8
{

void ZoneWriter::build()
{
    const uint32_t n = static_cast<uint32_t>(entries_.size());

    // 1) XAssetList root -> TEMP(0). DB_ReadXFile(.., 0x20) reads it first from block 0.
    zb_.pushStream(XFILE_BLOCK_TEMP);
    IW8_XAssetList al{};
    al.stringList.count = 0;
    al.stringList.loaded = 0;
    al.stringList.strings = PTR_NULL;
    al.assetCount = n;
    al.assetReadPos = 0;
    al.assets = (n > 0) ? PTR_FOLLOWS : PTR_NULL;
    zb_.writeT(al);
    zb_.popStream();

    if (n == 0)
    {
        zt::info("iw8 zone: 0 assets (valid-empty)");
        return;
    }

    // 2) XAsset[n] -> VIRTUAL(8). header=-3 (insert: registers the asset pointer).
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
    for (const auto &e : entries_)
    {
        zt::debug("iw8 zone: asset %s '%s'", iw8sz::type_name(e.type), e.name.c_str());
        // DB_InsertPointer RVA 0x11B2390 reserves an aligned VIRTUAL slot.
        // It consumes no fastfile bytes.
        zb_.align(7);
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
