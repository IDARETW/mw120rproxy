#pragma once

#include "replay_bounds.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace replaysunshadow
{
struct Vertex
{
    std::array<float, 3> position{};
    std::array<float, 2> uv{};
    float alpha{1.0f};
};

struct Material
{
    unsigned cullMode{1}; // Replay: none=0, back=1, front=2.
    unsigned alphaTest{}; // none=0, >=0.5=1, >0=2, <0.5=3.
    unsigned width{}, height{};
    bool atlasTile{}; // Generated world pass samples a clamped, repeating atlas tile.
    std::vector<std::uint8_t> alpha;
};

struct Surface
{
    unsigned material{};
    std::vector<Vertex> vertices; // Expanded triangle list, in world space.
};

struct Scene
{
    std::vector<Material> materials;
    std::vector<Surface> surfaces;
};

// Replay GfxWorld +0x3860; checked loader and projection-builder layout.
struct Parameters
{
    std::array<float, 3> sunDirection{};
    std::uint32_t resolution{};
    float centerX{}, centerY{}, sampleSize{};
    float nearPlane{}, farPlane{};
    std::uint32_t forestSize{}, flags{}, reserved{};
};
static_assert(sizeof(Parameters) == 48);
static_assert(offsetof(Parameters, resolution) == 12);
static_assert(offsetof(Parameters, centerX) == 16);
static_assert(offsetof(Parameters, centerY) == 20);
static_assert(offsetof(Parameters, sampleSize) == 24);
static_assert(offsetof(Parameters, nearPlane) == 28);
static_assert(offsetof(Parameters, farPlane) == 32);
static_assert(offsetof(Parameters, forestSize) == 36);
static_assert(offsetof(Parameters, flags) == 40);
static_assert(offsetof(Parameters, reserved) == 44);

struct Data
{
    Parameters parameters;
    std::vector<std::uint8_t> bytes;
};

Data Bake(const Scene &scene, const std::array<float, 3> &sunDirection,
          const replaybounds::Bounds &receiverBounds);

// A mini tile stores normalized occluder depth. Positive zero means no caster.
// Replay compares (projectedReceiver + origin) * inverseSpan with this depth.
std::vector<std::uint8_t> EncodeTile(std::span<const float> depths,
                                    float origin, float inverseSpan);

// Tiles are in row-major order; an empty vector is an unoccupied forest cell.
// The full logical resolution can exceed the cropped forest's dimensions.
std::vector<std::uint8_t> EncodeForest(
    std::uint32_t resolution, std::uint16_t width, std::uint16_t height,
    float origin, float inverseSpan,
    std::span<const std::vector<std::uint8_t>> tiles);
} // namespace replaysunshadow
