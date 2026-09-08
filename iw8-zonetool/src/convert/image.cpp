// image.cpp — IW3->IW8 IMAGE-family conversion helpers (non-inline TU).
// Most of the conversion is small enough to live inline in convert/image_fmt.h (format mapping, block
// geometry); this TU exists so the build's src/**/*.cpp glob has an image-convert object and to host
// the heavier validation helper used by both sides.
#include "image_fmt.h"
#include "../common/log.h"

namespace cvtimg {

// Validate (and if needed, repair) the pixel size carried in an ImageBlobHeader against the format/dims.
// IW3 GfxImageLoadDef.resourceSize is authoritative when present; but if it is 0 / implausible we
// recompute the full mip-chain size from format+dims so the IW8 writer always emits a non-degenerate
// totalSize and copies a self-consistent pixel count. Returns the size to trust.
uint32_t validate_pixel_size(const ImageBlobHeader& h, uint32_t availableBytes) {
    uint32_t computed = iw3_total_size(h.iw3Format, h.width, h.height, h.levelCount);
    // Trust the stored size when it matches what we actually have on disk.
    if (h.pixelSize != 0 && h.pixelSize <= availableBytes) {
        if (computed != 0 && h.pixelSize != computed) {
            zt::debug("image: stored pixelSize %u != computed mip-chain %u (fmt=%d %ux%u L%u) — "
                      "using stored", h.pixelSize, computed, h.iw3Format, h.width, h.height, h.levelCount);
        }
        return h.pixelSize;
    }
    // Fall back to the smaller of computed / available so we never read past the blob.
    uint32_t use = computed;
    if (use == 0 || use > availableBytes) use = availableBytes;
    zt::warn("image: pixelSize unusable (stored=%u avail=%u) — using %u", h.pixelSize, availableBytes, use);
    return use;
}

} // namespace cvtimg
