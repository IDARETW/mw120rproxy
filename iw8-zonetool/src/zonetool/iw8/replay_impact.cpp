#include "replay_impact.h"

#include "replay_impact_data.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>

namespace iw8::impact
{
namespace
{

constexpr size_t kImpactTableSize = 40;
constexpr size_t kImpactEntrySize = 16;
constexpr size_t kParticleSystemSize = 128;
constexpr size_t kRegisteredImpactTypeCount = 33;
constexpr std::string_view kStockSmallGlass =
    ",vfx/iw8/weap/_impact/glass/vfx_imp_glass_sml";

template <typename T> void Store(void *destination, size_t offset, T value)
{
    std::memcpy(static_cast<uint8_t *>(destination) + offset, &value, sizeof(value));
}

size_t EffectOffset(size_t impactType, size_t direction, size_t pack, size_t effect)
{
    return (((impactType * 2 + direction) * impact_data::kPackCount + pack) *
            impact_data::kEffectsPerPack) +
           effect;
}

void WriteParticleReference(ZoneWriter &writer, std::string_view name)
{
    writer.pushStream(XFILE_BLOCK_TEMP_PRELOAD);
    writer.align(15);

    std::array<uint8_t, kParticleSystemSize> particle{};
    Store(particle.data(), 0, PTR_FOLLOWS);
    writer.write(particle.data(), particle.size());

    writer.pushStream(XFILE_BLOCK_VIRTUAL);
    writer.writeStr(std::string(name));
    writer.popStream();
    writer.popStream();
}

std::string_view EffectName(const size_t impactType, const size_t direction,
                            const size_t pack, const size_t effect,
                            const std::string_view smallGlassEffect,
                            const EffectOverrides &overrides)
{
    const bool flesh = effect >= 64;
    const size_t index = flesh ? effect - 64 : effect;
    const auto overridden = std::find_if(
        overrides.begin(), overrides.end(), [&](const EffectOverride &candidate) {
            return candidate.impactType == impactType && candidate.direction == direction &&
                   pack == 0 && candidate.flesh == flesh && candidate.index == index;
        });
    if (overridden != overrides.end())
        return overridden->effect;

    const uint16_t nameIndex =
        impact_data::kEffectIndices[EffectOffset(impactType, direction, pack, effect)];
    if (nameIndex == 0)
        return {};
    if (nameIndex > impact_data::kEffectNames.size())
        throw std::runtime_error("impact effect template contains an invalid name index");
    const std::string_view stockName = impact_data::kEffectNames[nameIndex - 1];
    return !smallGlassEffect.empty() && stockName == kStockSmallGlass ? smallGlassEffect
                                                                      : stockName;
}

size_t WritePacks(ZoneWriter &writer, size_t impactType, size_t direction,
                  const std::string_view smallGlassEffect,
                  const EffectOverrides &overrides)
{
    writer.align(7);

    std::array<uint64_t, impact_data::kPackCount * impact_data::kEffectsPerPack> packs{};
    size_t referenceCount = 0;
    for (size_t pack = 0; pack < impact_data::kPackCount; ++pack)
    {
        for (size_t effect = 0; effect < impact_data::kEffectsPerPack; ++effect)
        {
            if (!EffectName(impactType, direction, pack, effect, smallGlassEffect,
                            overrides)
                     .empty())
            {
                packs[pack * impact_data::kEffectsPerPack + effect] = PTR_FOLLOWS;
                ++referenceCount;
            }
        }
    }
    writer.write(packs.data(), sizeof(packs));

    for (size_t pack = 0; pack < impact_data::kPackCount; ++pack)
    {
        for (size_t effect = 0; effect < impact_data::kEffectsPerPack; ++effect)
        {
            const std::string_view name =
                EffectName(impactType, direction, pack, effect, smallGlassEffect, overrides);
            if (name.empty())
                continue;
            WriteParticleReference(writer, name);
        }
    }
    return referenceCount;
}

void WriteBody(ZoneWriter &writer, const std::string &mapName,
               const std::string_view smallGlassEffect,
               const EffectOverrides &overrides)
{
    writer.pushStream(XFILE_BLOCK_TEMP_PRELOAD);
    writer.align(7);

    std::array<uint8_t, kImpactTableSize> table{};
    Store(table.data(), 0, PTR_FOLLOWS);
    Store(table.data(), 8, static_cast<uint32_t>(impact_data::kImpactTypeCount));
    Store(table.data(), 16, PTR_FOLLOWS);
    Store(table.data(), 24, PTR_FOLLOWS);
    Store(table.data(), 32, PTR_FOLLOWS);
    writer.write(table.data(), table.size());

    writer.pushStream(XFILE_BLOCK_VIRTUAL);
    writer.writeStr(mapName);
    writer.align(7);

    std::array<uint64_t, impact_data::kImpactTypeCount * 2> entries{};
    for (size_t impactType = 0; impactType < impact_data::kImpactTypeCount; ++impactType)
    {
        entries[impactType * 2] = PTR_FOLLOWS;
        if ((impact_data::kExitMask & (uint64_t{1} << impactType)) != 0)
        {
            entries[impactType * 2 + 1] = PTR_FOLLOWS;
        }
    }
    static_assert(sizeof(entries) == impact_data::kImpactTypeCount * kImpactEntrySize);
    writer.write(entries.data(), sizeof(entries));

    size_t particleReferenceCount = 0;
    for (size_t impactType = 0; impactType < impact_data::kImpactTypeCount; ++impactType)
    {
        particleReferenceCount +=
            WritePacks(writer, impactType, 0, smallGlassEffect, overrides);
        if ((impact_data::kExitMask & (uint64_t{1} << impactType)) != 0)
        {
            particleReferenceCount +=
                WritePacks(writer, impactType, 1, smallGlassEffect, overrides);
        }
    }

    writer.align(3);
    for (size_t impactType = 0; impactType < impact_data::kImpactTypeCount; ++impactType)
    {
        const uint32_t registered = impactType < kRegisteredImpactTypeCount
                                        ? static_cast<uint32_t>(impactType)
                                        : UINT32_MAX;
        writer.writeT(registered);
    }

    writer.align(3);
    for (size_t impactType = 0; impactType < kRegisteredImpactTypeCount; ++impactType)
    {
        writer.writeT(static_cast<uint32_t>(impactType));
    }

    writer.popStream();
    writer.popStream();

    writer.pushStream(XFILE_BLOCK_TEMP_POSTLOAD);
    writer.align(7);
    writer.reserveCalc(kImpactTableSize);
    for (size_t index = 0; index < particleReferenceCount; ++index)
    {
        writer.align(15);
        writer.reserveCalc(kParticleSystemSize);
    }
    writer.popStream();
}

} // namespace

void Register(ZoneWriter &writer, const std::string &mapName,
              const std::string &smallGlassEffect,
              const EffectOverrides &overrides)
{
    const std::string referenceName =
        smallGlassEffect.empty() || smallGlassEffect.front() == ','
            ? smallGlassEffect
            : "," + smallGlassEffect;
    writer.add(ASSET_TYPE_IMPACT_FX, mapName,
               [mapName, referenceName, overrides](ZoneWriter &output) {
                   WriteBody(output, mapName, referenceName, overrides);
               });
}

} // namespace iw8::impact
