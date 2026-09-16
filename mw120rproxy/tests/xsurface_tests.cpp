#include "../../iw8-zonetool/src/zonetool/convert/xsurface_convert.h"
#include "../../iw8-zonetool/src/zonetool/convert/conv_xsurface.h"
#include "../../iw8-zonetool/src/zonetool/dumpsrc/xse_dump.h"
#include "../../iw8-zonetool/src/zonetool/dumpsrc/xmodel_dump.h"
#include "../../iw8-zonetool/src/zonetool/iw8/iw8_zone.h"
#include "../../iw8-zonetool/src/zonetool/iw8/replay_render.h"
#include "../../iw8-zonetool/src/zonetool/iw8/write_xsurface.h"
#include "../../iw8-zonetool/src/common/ff_io.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <filesystem>
#include <limits>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

std::array<float, 4> unpackFrame(uint32_t packed) {
    const float values[]{(packed & 1023) * 0.001382418f - 0.70710677f,
                         ((packed >> 10) & 1023) * 0.001382418f - 0.70710677f,
                         ((packed >> 20) & 511) * 0.0027675412f - 0.70710677f};
    const float sum = values[0] * values[0] + values[1] * values[1] + values[2] * values[2];
    check(sum <= 1.00001f, "Packed quaternion is outside the unit sphere");
    std::array<float, 4> q{};
    size_t next = 0;
    for (size_t component = 0; component < q.size(); ++component)
        q[component] =
            component == (packed >> 30) ? std::sqrt(std::max(0.0f, 1.0f - sum)) : values[next++];
    return q;
}

void basis(const std::array<float, 4>& q, float* normal, float* tangent) {
    const auto [x, y, z, w] = q;
    normal[0] = 2 * (y * w + x * z);
    normal[1] = 2 * (y * z - x * w);
    normal[2] = 1 - 2 * (x * x + y * y);
    tangent[0] = 1 - 2 * (y * y + z * z);
    tangent[1] = 2 * (x * y + z * w);
    tangent[2] = 2 * (x * z - y * w);
}
}

int wmain(int argc, wchar_t** argv) {
    HMODULE replay = nullptr;
    try {
        check(argc == 2 || argc == 3,
              "Pass the Replay executable and optional fixture fastfile path");
        replay = LoadLibraryExW(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES);
        check(replay != nullptr, "Could not map Replay for offline position decoding");
        const auto* decoder = reinterpret_cast<const uint8_t*>(replay) + 0x1992D20;
        const uint8_t prologue[]{0x48, 0x83, 0xEC, 0x18, 0xF3};
        check(std::memcmp(decoder, prologue, sizeof(prologue)) == 0,
              "Unexpected Replay decoder prologue");
        using Unpack = void (*)(uint64_t, const float*, float*);
        const auto unpack = reinterpret_cast<Unpack>(const_cast<uint8_t*>(decoder));

        const float bounds[][6]{
            {10, 20, 30, 1, 2, 4}, {-200, 90, 2, 300, 0.001f, 4}, {0, 0, 0, 0, 0, 0}};
        unsigned positions = 0;
        for (const auto& bound : bounds) {
            const float scale = std::max({bound[3], bound[4], bound[5]});
            for (int i = 0; i < 257; ++i) {
                float position[3], decoded[3];
                for (size_t axis = 0; axis < 3; ++axis)
                    position[axis] =
                        bound[axis] +
                        bound[axis + 3] * (2.0f * ((i * (axis * 2 + 1)) % 257) / 256.0f - 1.0f);
                const auto packed = xsurf_conv::packPosition(position, bound, bound + 3);
                check((packed >> 63) == 0, "Packed position used its reserved bit");
                unpack(packed, bound, decoded);
                for (size_t axis = 0; axis < 3; ++axis)
                    check(std::abs(position[axis] - decoded[axis]) <= scale * 1.5e-6f + 0.00002f,
                          "Position does not round-trip through the native Replay decoder");
                ++positions;
            }
        }

        unsigned frames = 0, largestMask = 0;
        for (int axis = 0; axis < 3; ++axis)
            for (int step = 0; step <= 360; ++step) {
                const float angle = step * 0.0174532925199433f;
                std::array<float, 4> source{0, 0, 0, std::cos(angle * 0.5f)};
                source[axis] = std::sin(angle * 0.5f);
                float normal[3], tangent[3];
                basis(source, normal, tangent);
                for (const float sign : {-1.0f, 1.0f}) {
                    const auto packed = xsurf_conv::packTangentFrame(normal, tangent, sign);
                    largestMask |= 1u << (packed >> 30);
                    check(((packed >> 29) & 1u) == (sign < 0), "Lost tangent handedness");
                    float resultNormal[3], resultTangent[3];
                    basis(unpackFrame(packed), resultNormal, resultTangent);
                    for (size_t component = 0; component < 3; ++component) {
                        check(std::abs(normal[component] - resultNormal[component]) < 0.008f,
                              "Tangent-frame normal changed during packing");
                        check(std::abs(tangent[component] - resultTangent[component]) < 0.008f,
                              "Tangent direction changed during packing");
                    }
                    ++frames;
                }
            }
        check(largestMask == 15, "Quaternion test did not exercise all omitted components");
        for (uint32_t bits = 0; bits <= 0xFFFF; ++bits)
            if ((bits & 0x7C00) != 0x7C00)
                check(xsurf_conv::floatToHalf(
                          xsurf_conv::halfToFloat(static_cast<uint16_t>(bits))) == bits,
                      "Finite half-float round-trip changed its bits");
        for (const float sign : {-1.0f, 1.0f}) {
            const uint16_t signBit = sign < 0 ? 0x8000 : 0;
            check(xsurf_conv::floatToHalf(sign * (1.0f + std::ldexp(1.0f, -11))) ==
                      (signBit | 0x3C00),
                  "Half-float even tie rounded up");
            check(xsurf_conv::floatToHalf(sign * (1.0f + 3 * std::ldexp(1.0f, -11))) ==
                      (signBit | 0x3C02),
                  "Half-float odd tie rounded down");
            check(xsurf_conv::floatToHalf(sign * std::ldexp(1.0f, -25)) == signBit,
                  "Half-float subnormal even tie rounded up");
        }
        dumpsrc::XseFile source;
        source.loaded = true;
        source.name = "model_packing_fixture";
        source.surfaces.resize(2);
        for (size_t surface = 0; surface < source.surfaces.size(); ++surface) {
            auto& s = source.surfaces[surface];
            s.triIndices = {0, 1, 2};
            s.rigidVertLists.push_back({static_cast<uint16_t>((surface + 1) * 64), 3, 0, 1});
            for (size_t vertex = 0; vertex < 3; ++vertex) {
                dumpsrc::XseVertex v;
                v.xyz[0] = vertex == 1 ? 20.0f : 10.0f;
                v.xyz[1] = vertex == 2 ? 3.0f : 0.0f;
                v.xyz[2] = static_cast<float>(surface) * 7.0f;
                v.normal = 0x00FF7F7F;
                v.tangent = 0x007F7FFF;
                v.binormalSign = 1.0f;
                v.color = 0x80402010u + static_cast<uint32_t>(vertex);
                v.texCoord = 0x34003A00u; // IW5 U=0.25, V=0.75.
                s.verticies.push_back(v);
            }
        }
        auto grouped = source.surfaces[0];
        grouped.verticies.insert(grouped.verticies.end(), source.surfaces[1].verticies.begin(),
                                 source.surfaces[1].verticies.end());
        grouped.triIndices = {0, 1, 2, 3, 4, 5};
        grouped.rigidVertLists = {{64, 3, 0, 1}, {128, 3, 1, 1}};
        source.surfaces.push_back(grouped);
        auto skinned = source.surfaces[0];
        skinned.deformed = 1;
        skinned.rigidVertLists.clear();
        skinned.verticies.push_back(skinned.verticies[0]);
        skinned.verticies.back().xyz[1] = 3.0f;
        skinned.triIndices = {0, 1, 2, 0, 2, 3};
        for (auto& count : skinned.vertBlendCounts)
            count = 1;
        skinned.vertsBlend = {64,    0, 64, 32768, 0,   64,    16384, 128,
                              16384, 0, 64, 8192,  128, 16384, 128,   24576};
        source.surfaces.insert(source.surfaces.begin() + 1, skinned);
        const auto converted = conv_xsurf::convert(source);
        check(converted.ok && converted.surfaces.size() == 4, "Could not convert fixture surfaces");
        check(converted.totalRigidRuns == 4 &&
                  converted.surfaces[0].rigidVertLists[0].boneIndex == 1 &&
                  converted.surfaces[2].rigidVertLists[0].boneIndex == 2,
              "Rigid matrix offsets were not converted into bone indices");
        const std::vector<uint16_t> expectedBlend = {1,     0, 0, 1, 32768, 0, 0,     1, 16384, 2,
                                                     16384, 0, 0, 1, 8192,  2, 16384, 2, 24576, 0};
        check(converted.surfaces[1].flags == 3 &&
                  converted.surfaces[1].blendVerts == expectedBlend &&
                  converted.totalBlendWords == expectedBlend.size(),
              "Replay blend tiers lost indices, weights, or padding");
        check(converted.surfaces[0].flags == 1 && converted.surfaces[2].flags == 1,
              "Rigid model vertex colors are missing their UOB allocation flag");
        const auto hash = [](const conv_xsurf::Iw8Surfs& surfs, size_t index = 0) {
            return conv_xsurf::surfaceHash(surfs, surfs.surfaces[index]);
        };
        check(hash(converted) != hash(converted, 2),
              "Same-count surfaces with different bounds share a UGB key");
        auto identitySource = source;
        const auto originalHash = hash(converted);
        check(hash(conv_xsurf::convert(identitySource)) == originalHash,
              "Model surface hash is not deterministic");
        identitySource.surfaces[0].verticies[0].color ^= 0x01000000;
        check(hash(conv_xsurf::convert(identitySource)) != originalHash,
              "Model color changes reuse the old UGB key");
        identitySource = source;
        identitySource.surfaces[0].verticies[0].texCoord ^= 0x04000000;
        check(hash(conv_xsurf::convert(identitySource)) != originalHash,
              "Model UV changes reuse the old UGB key");
        auto finalAttributes = converted;
        finalAttributes.sharedBlob[finalAttributes.surfaces[0].sharedVertDataOffset + 8] ^= 1;
        check(hash(finalAttributes) != originalHash,
              "Final importer metadata changes reuse the old UGB key");
        finalAttributes = converted;
        finalAttributes.sharedBlob[finalAttributes.surfaces[2].sharedVertDataOffset + 8] ^= 1;
        check(hash(finalAttributes) == originalHash, "Unrelated surfaces change the UGB key");
        for (int failure = 0; failure < 5; ++failure) {
            auto badBlend = source;
            auto& surface = badBlend.surfaces[1];
            switch (failure) {
            case 0:
                surface.vertsBlend.pop_back();
                break;
            case 1:
                surface.vertBlendCounts[0] = -1;
                break;
            case 2:
                surface.vertsBlend[0] = 65;
                break;
            case 3:
                surface.vertsBlend[6] = surface.vertsBlend[8] = 40000;
                break;
            case 4:
                surface.deformed = 0;
                break;
            }
            bool blendRejected = false;
            try {
                (void)conv_xsurf::convert(badBlend);
            } catch (const std::runtime_error&) {
                blendRejected = true;
            }
            check(blendRejected, "Invalid blend data was accepted");
        }
        auto badRigid = source;
        badRigid.surfaces[0].rigidVertLists[0].boneOffset = 65;
        bool badRigidRejected = false;
        try {
            (void)conv_xsurf::convert(badRigid);
        } catch (const std::runtime_error&) {
            badRigidRejected = true;
        }
        check(badRigidRejected, "Misaligned source bone offset was silently truncated");
        for (size_t s = 0; s < converted.surfaces.size(); ++s)
            for (size_t vertex = 0; vertex < 3; ++vertex) {
                uint32_t color;
                std::memcpy(&color,
                            converted.sharedBlob.data() +
                                converted.surfaces[s].sharedColorDataOffset + vertex * 4,
                            4);
                check(color == 0x80102040u + (static_cast<uint32_t>(vertex) << 16),
                      "Model vertex color channels were not converted from BGRA to RGBA");
                uint32_t texCoord;
                std::memcpy(&texCoord,
                            converted.sharedBlob.data() +
                                converted.surfaces[s].sharedVertDataOffset + vertex * 20 + 12,
                            sizeof(texCoord));
                check(texCoord == 0x3A003400u, "Model texture coordinates were transposed");
            }
        auto large = source;
        large.surfaces.resize(1);
        large.surfaces[0].triIndices.resize(65538);
        for (size_t index = 0; index < large.surfaces[0].triIndices.size(); ++index)
            large.surfaces[0].triIndices[index] = static_cast<uint16_t>(index % 3);
        check(conv_xsurf::convert(large).surfaces[0].triCount == 21846,
              "Triangle count was narrowed before division");
        large.surfaces[0].triIndices[0] = 3;
        bool rejected = false;
        try {
            (void)conv_xsurf::convert(large);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        check(rejected, "Out-of-range model triangle was accepted");

        if (argc == 3) {
            iw8::ZoneWriter zone;
            const auto emptyString = zone.internScriptString("");
            check(emptyString != 0 && zone.internScriptString("") == emptyString,
                  "Empty script string was confused with null or not deduplicated");
            constexpr const char* fixtureMaterial = "model_fixture_material";
            zone.add(ASSET_TYPE_MATERIAL, fixtureMaterial,
                     [fixtureMaterial](iw8::ZoneWriter& writer) {
                         std::array<uint8_t, 0x78> material{};
                         const uint64_t follows = iw8::PTR_FOLLOWS;
                         std::memcpy(material.data(), &follows, sizeof(follows));
                         writer.pushStream(iw8::XFILE_BLOCK_TEMP_PRELOAD);
                         writer.align(7);
                         writer.write(material.data(), material.size());
                         writer.pushStream(iw8::XFILE_BLOCK_VIRTUAL);
                         writer.writeStr(fixtureMaterial);
                         writer.popStream();
                         writer.popStream();
                     });
            iw8xs_dump::writeXModelSurfs(zone, source.name, converted);
            iw8xs_dump::writeXModelSurfs(zone, source.name + "_different_alignment", converted);
            dumpsrc::ModelSkeleton skeleton;
            skeleton.parentList = {1, 2};
            skeleton.quats = {{{0, 0, 0, 32767}}, {{0, 0, 23170, 23170}}};
            skeleton.trans = {{{1, 2, 3}}, {{-4, 5, 6}}};
            skeleton.partClassification = {0, 2, 3};
            for (int bone = 0; bone < 3; ++bone) {
                skeleton.baseMat.push_back({{0, 0, 0, 1}, {float(bone), 2, 3}, 2});
                skeleton.boneInfo.push_back({{float(bone), 2, 3}, {4, 5, 6}, 77});
            }
            std::vector<uint8_t> dump;
            const auto array = [&](const void* data, uint32_t count, size_t stride) {
                dump.push_back(3);
                const auto* sizeBytes = reinterpret_cast<const uint8_t*>(&count);
                dump.insert(dump.end(), sizeBytes, sizeBytes + 4);
                if (count) {
                    const auto* bytes = static_cast<const uint8_t*>(data);
                    dump.insert(dump.end(), bytes, bytes + count * stride);
                }
            };
            const auto text = [&](const std::string& value) {
                dump.insert(dump.end(), {1, 1});
                dump.insert(dump.end(), value.begin(), value.end());
                dump.push_back(0);
            };
            std::array<uint8_t, 308> header{};
            header[4] = 3;
            header[5] = 1;
            array(header.data(), 1, header.size());
            text("model_bone_names_fixture");
            const uint16_t handles[]{1, 2, 3};
            array(handles, 3, sizeof(handles[0]));
            for (const auto* name : {"tag_origin", "tag_door", "tag_shared"})
                text(name);
            const auto bones = [&](const auto& values) {
                array(values.data(), static_cast<uint32_t>(values.size()), sizeof(values[0]));
            };
            bones(skeleton.parentList);
            bones(skeleton.quats);
            bones(skeleton.trans);
            bones(skeleton.partClassification);
            bones(skeleton.baseMat);
            bones(skeleton.boneInfo);
            array(nullptr, 0, 4);                  // No material handles.
            array(nullptr, 0, 56);                 // No collision surfaces.
            dump.insert(dump.end(), {2, 0, 2, 0}); // Null physics asset names.
            dumpsrc::XModelDumpFull parsed;
            check(dumpsrc::parseXModel(dump, "fixture", parsed) && parsed.clean,
                  "Could not parse complete model skeleton");
            auto truncated = dump;
            truncated.pop_back();
            dumpsrc::XModelDumpFull invalid;
            check(!dumpsrc::parseXModel(truncated, "truncated", invalid) && !invalid.loaded,
                  "Truncated model was reported as loaded");
            truncated.resize(400);
            check(!dumpsrc::parseXModel(truncated, "truncated_skeleton", invalid) &&
                      !invalid.loaded,
                  "Truncated skeleton was reported as loaded");
            convert::xmodel::Iw8XModelRecord model;
            check(convert::xmodel::toIw8(parsed, model), "Could not convert model skeleton");
            check(model.skeleton.parentList == skeleton.parentList &&
                      model.skeleton.quats == skeleton.quats &&
                      model.skeleton.trans == skeleton.trans,
                  "Skeleton transforms were discarded during conversion");
            auto badParent = model;
            badParent.skeleton.parentList[0] = 255;
            bool invalidParentRejected = false;
            try {
                iw8::writeXModel(zone, badParent);
            } catch (const std::runtime_error&) {
                invalidParentRejected = true;
            }
            check(invalidParentRejected, "Model with an invalid parent reference was accepted");
            model.numsurfs = static_cast<uint16_t>(converted.surfaces.size());
            model.numLods = 1;
            model.lods = {
                {800.0f, static_cast<uint16_t>(converted.surfaces.size()), 0, {}, 0, source.name}};
            model.materials.assign(model.numsurfs, fixtureMaterial);
            auto badLod = model;
            badLod.lods[0].surfIndex = badLod.numsurfs;
            bool invalidLodRejected = false;
            try {
                iw8::writeXModel(zone, badLod);
            } catch (const std::runtime_error&) {
                invalidLodRejected = true;
            }
            check(invalidLodRejected, "Model with an invalid LOD surface range was accepted");
            iw8::writeXModel(zone, model);
            const std::string placedModelName = model.name;
            model.name = "model_bone_names_shared_fixture";
            model.boneNames = {"tag_shared", "tag_origin", "tag_window"};
            iw8::writeXModel(zone, model);
            replayrender::StaticModels staticModels;
            replayrender::StaticModel staticModel;
            staticModel.name = placedModelName;
            staticModel.bounds = {{0, 0, 0}, {4, 5, 6}};
            replayrender::StaticModelLod lod;
            lod.distance = 800;
            for (const auto& surface : converted.surfaces) {
                replayrender::StaticModelSurface placedSurface;
                std::copy_n(surface.boundsMid, 3, placedSurface.bounds.midpoint.begin());
                std::copy_n(surface.boundsHalf, 3, placedSurface.bounds.halfSize.begin());
                placedSurface.material = fixtureMaterial;
                lod.surfaces.push_back(std::move(placedSurface));
            }
            staticModel.lods.push_back(std::move(lod));
            staticModels.models.push_back(std::move(staticModel));
            staticModels.instances.push_back({0, {32, 64, 96}, {0, 0, 0, 1}, 1.25f});
            constexpr const char* fixtureWorld = "maps/mp/model_packing.d3dbsp";
            zone.add(ASSET_TYPE_GFX_MAP, fixtureWorld, [staticModels](iw8::ZoneWriter& writer) {
                std::vector<uint8_t> world(0x4590);
                const uint64_t follows = iw8::PTR_FOLLOWS;
                std::memcpy(world.data(), &follows, sizeof(follows));
                std::memcpy(world.data() + 8, &follows, sizeof(follows));
                replayrender::StampStaticModels(world, staticModels);
                writer.pushStream(iw8::XFILE_BLOCK_TEMP_PRELOAD);
                writer.align(15);
                writer.write(world.data(), world.size());
                writer.pushStream(iw8::XFILE_BLOCK_VIRTUAL);
                writer.writeStr(fixtureWorld);
                writer.writeStr(fixtureWorld);
                replayrender::EmitStaticModels(writer, staticModels);
                writer.popStream();
                writer.popStream();
                writer.pushStream(iw8::XFILE_BLOCK_TEMP_POSTLOAD);
                writer.align(15);
                writer.reserveCalc(world.size());
                writer.popStream();
            });
            zone.build();
            bool lateStringRejected = false;
            try {
                zone.internScriptString("late_string");
            } catch (const std::runtime_error&) {
                lateStringRejected = true;
            }
            check(lateStringRejected, "Script string was registered after its table was written");
            zt::Iw8WriteParams params;
            for (int stream = 0; stream < iw8::IW8_MAX_XFILE_COUNT; ++stream) {
                params.blockSize[stream] = zone.blockSize(stream);
                params.totalDecompressed += params.blockSize[stream];
            }
            params.calcSize = zone.calcSize();
            check(zt::iw8_write(std::filesystem::path(argv[2]).string(), zone.body(), params),
                  "Could not write native model fixture");
        }
        FreeLibrary(replay);
        std::cout << positions << " native position round-trips, " << frames
                  << " tangent frames, all finite half-floats and rounding ties passed\n";
        return 0;
    } catch (const std::exception& error) {
        if (replay)
            FreeLibrary(replay);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
