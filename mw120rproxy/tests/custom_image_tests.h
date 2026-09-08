#include "replay_image_fixture.h"
#include "replay_localize_fixture.h"
unsigned imageUploads = 0, imagePassthrough = 0;
unsigned ImageLevelCount(int width, int height, int depth) {
    unsigned n = 0;
    for (unsigned size = unsigned((std::max)({width, height, depth})); size; size >>= 1)
        ++n;
    return n;
}
unsigned ImageCreateTexture(void* descriptor) {
    auto* words = static_cast<unsigned*>(descriptor);
    Check(words[0] == 4 && words[1] == 2 && words[2] == 1 && words[3] == 1 && words[4] == 1 &&
              words[5] == 3 && words[6] == 7,
          "actual Replay image setup emits correct resident GPU descriptor");
    const char* name = nullptr;
    const unsigned char* pixels = nullptr;
    memcpy(&name, (char*)descriptor + 0x38, 8);
    memcpy(&pixels, (char*)descriptor + 0x40, 8);
    Check(std::string(name) == "mw120r/" + id && pixels && pixels[0] == 21 && pixels[31] == 21,
          "native upload consumes owned image name and complete pixel buffer");
    ++imageUploads;
    return 0x8000007B;
}
std::array<unsigned char, 0x78> stockLoadingMaterial{};
std::array<unsigned char, 16> stockLoadingTexture{};
struct MapInfoTable {
    const char* name;
    int columns, rows, unique, padding;
    uint16_t* indices;
    uint32_t* hashes;
    const char** strings;
};
#include "replay_mapinfo_fixture.h"
MapInfoTable* stockMapInfo = nullptr;
void* ImageStockFind(int type, const char* name, int) {
    if (type == 54 && !_stricmp(name, "mp/mapInfo.csv"))
        return stockMapInfo;
    ++imagePassthrough;
    if (type == 11 && !strcmp(name, "loadscreen_mp_hackney"))
        return stockLoadingMaterial.data();
    return reinterpret_cast<void*>(123);
}
void CustomImageTests() {
    stockLoadingMaterial.fill(37);
    stockLoadingMaterial[0x1C] = 1;
    auto* table = stockLoadingTexture.data();
    memcpy(stockLoadingMaterial.data() + 0x48, &table, 8);
    const auto original = stockLoadingMaterial;
    const auto file = packageRoot / id / "preview.rgba";
    {
        std::ofstream out(file, std::ios::binary);
        unsigned header[]{0x4952574D, 1, 4, 2};
        out.write((char*)header, 16);
        std::array<char, 32> pixels;
        pixels.fill(21);
        out.write(pixels.data(), 32);
    }
    Commit(ImagePixelsRva);
    memcpy(image + ImagePixelsRva, ImagePixels, sizeof(ImagePixels));
    Commit(ImageDescriptorRva);
    memcpy(image + ImageDescriptorRva, ImageDescriptor, sizeof(ImageDescriptor));
    Commit(0x193B920);
    Jump(image + 0x193B920, reinterpret_cast<void*>(&ImageLevelCount));
    Commit(0xEE0D40);
    Jump(image + 0xEE0D40, reinterpret_cast<void*>(&ImageCreateTexture));
    Commit(replay::FindAsset.rva);
    PrologueFixture(replay::FindAsset, {}, reinterpret_cast<void*>(&ImageStockFind));
    Check(customimages::Install(reinterpret_cast<uintptr_t>(image)) == hook::Status::Installed,
          "custom artwork validates native lookup and upload bindings");
    auto find = reinterpret_cast<void* (*)(int, const char*, int)>(image + replay::FindAsset.rva);
    auto* result = find(19, ("mw120r/" + id).c_str(), 1);
    Check(result && imageUploads == 1, "custom preview creates a native resident texture");
    unsigned texture = 0;
    memcpy(&texture, (char*)result + 0x10, 4);
    Check(texture == 0x8000007B, "actual native image loader publishes returned texture ID");
    Check(find(19, ("loadscreen_" + id).c_str(), 1) == result && imageUploads == 1,
          "loadscreen and lobby share a stable image without duplicate uploads");
    Check(find(19, "stock_image", 1) == reinterpret_cast<void*>(123) &&
              find(11, "mw120r/material", 0) == reinterpret_cast<void*>(123) &&
              imagePassthrough == 2,
          "stock images and all other asset types pass through unchanged");
    auto* material = find(11, ("loadscreen_" + id).c_str(), 1);
    Check(material && material != stockLoadingMaterial.data(),
          "loading screen resolves a dedicated native material");
    const unsigned char* copiedTable = nullptr;
    void* materialImage = nullptr;
    memcpy(&copiedTable, (char*)material + 0x48, 8);
    memcpy(&materialImage, copiedTable + 8, 8);
    Check(materialImage == result && copiedTable != table && stockLoadingMaterial == original,
          "loadscreen clone uses artwork and leaves stock material/table unchanged");
    Check(find(11, ("loadscreen_" + id).c_str(), 1) == material,
          "loadscreen material cache remains stable");
    Check(custommaps::Select(id.c_str()), "select custom loading-screen fixture");
    Check(find(19, "loadscreen_mp_hackney", 1) == result,
          "LUI IMAGE template resolves selected custom artwork");
    Check(find(11, "loadscreen_mp_hackney", 1) == material,
          "native MATERIAL template resolves selected custom artwork");
    custommaps::ClearSelection();
    Check(find(19, "loadscreen_mp_hackney", 1) == reinterpret_cast<void*>(123) &&
              find(11, "loadscreen_mp_hackney", 1) == stockLoadingMaterial.data(),
          "stock selection restores both image and material templates");

    auto hash = [](const std::string& text) {
        uint32_t h = 0;
        for (unsigned char c : text)
            h = h * 31 + (c >= 'A' && c <= 'Z' ? c + 32 : c);
        return h;
    };
    MapInfoTable stock{"mp/mapinfo.csv",  46, 121, 480, 0, capturedMapIndices, capturedMapHashes,
                       capturedMapStrings};
    stockMapInfo = &stock;
    custommaps::ClearSelection();
    auto* info = static_cast<MapInfoTable*>(find(54, "mp/mapinfo.csv", 1));
    Check(info != &stock && info->rows > 121 && info->columns == 46,
          "captured Replay compressed mapInfo table extends successfully");
    auto value = [&](const std::string& map, int column) {
        for (int row = 0; row < info->rows; ++row)
            if (map == info->strings[info->indices[row * info->columns]])
                return std::string(info->strings[info->indices[row * info->columns + column]]);
        return std::string();
    };
    Check(value(id, 21) == "mw120r/" + id && value(id, 22) == "mw120r/" + id,
          "hover and loading columns resolve custom artwork before selection");
    Check(find(19, value(id, 22).c_str(), 1) == result,
          "captured table loading lookup resolves uploaded image");
    for (int row = 0; row < 121; ++row)
        for (int col = 0; col < 46; ++col)
            Check(std::string(info->strings[info->indices[row * 46 + col]]) ==
                      capturedMapStrings[capturedMapIndices[row * 46 + col]],
                  "all captured stock CSV cells unchanged");
    for (const auto& package : custommaps::List())
        if (package.valid) {
            Check(value(package.id, 22) == "mw120r/" + package.id,
                  "hover uses map ID independently of active selection");
            std::string title = "MW120R/MAP_" + package.id;
            for (auto& c : title)
                if (c >= 'a' && c <= 'z')
                    c -= 32;
            Check(value(package.id, 1) == title && value(package.id, 24) == title,
                  "lobby name and base name use package title rather than template");
            std::string caps = package.title;
            for (auto& c : caps)
                if (c >= 'a' && c <= 'z')
                    c -= 32;
            Check(value(package.id, 2) == title + "_CAPS" && value(package.id, 3).empty(),
                  "loading title uses package caps and does not inherit night flag");
        }
    for (int i = 0; i < info->unique; ++i)
        Check(info->hashes[i] == hash(info->strings[i]),
              "captured and generated hashes agree with native hash");
    Check(find(54, "mp/mapInfo.csv", 1) == info, "case-insensitive lookup keeps stable cache");
    Check(stock.rows == 121 && stock.unique == 480, "original compressed header unchanged");
    Commit(0x13CC2A0);
    memcpy(image + 0x13CC2A0, ReplayLocalizeCode, sizeof(ReplayLocalizeCode));
    Commit(0xE32E278);
    std::array<unsigned char, 0x30> locDvar{};
    locDvar[0x28] = 1;
    auto* locPtr = locDvar.data();
    memcpy(image + 0xE32E278, &locPtr, 8);
    auto localize = reinterpret_cast<const char* (*)(const char*)>(image + 0x13CC2A0);
    for (const auto& package : custommaps::List())
        if (package.valid) {
            for (int column : {1, 2, 24}) {
                const auto key = value(package.id, column);
                Check(!key.empty() && key[0] != char(31),
                      "CSV names meet Replay Engine.Localize first-argument contract");
                auto expected = package.title;
                if (column == 2)
                    for (auto& c : expected)
                        if (c >= 'a' && c <= 'z')
                            c -= 32;
                Check(
                    std::string(localize(key.c_str())) == expected,
                    "actual Replay localization machine code resolves custom map title with correct asset ABI");
                Check(find(41, key.c_str(), 0) == find(41, key.c_str(), 0),
                      "custom localization asset has stable cache lifetime");
            }
        }
    puts(
        "PASS: actual Replay SEH localization executes custom type41 lookup, value offset +8, display/caps/base title; no forbidden literal prefix");

    const char* old = capturedMapStrings[0];
    capturedMapStrings[0] = "unloaded";
    Check(std::string(info->strings[0]) == old,
          "owned dictionary survives original strings changing");
    capturedMapStrings[0] = old;
    MapInfoTable incompatible = stock;
    incompatible.columns = 22;
    stockMapInfo = &incompatible;
    Check(find(54, "mp/mapinfo.csv", 1) == &incompatible,
          "incompatible table safely passes through");
    stockMapInfo = nullptr;
    puts(
        "PASS: live-captured Replay compressed table, all stock cells, custom hover/loading columns, dictionary hashes and lifetime");
    fs::remove(file);
    Check(find(19, ("mw120r/" + id).c_str(), 1) == result && imageUploads == 1,
          "cached UI image survives package file removal and zone transitions");
    puts(
        "PASS: custom artwork lookup, loading-screen alias, stable cache and stock passthrough; actual Replay image-loader/descriptor instructions executed (GPU creation mocked)");
}
