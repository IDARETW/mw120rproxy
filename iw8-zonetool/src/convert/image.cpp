// image.cpp — IW3->IW8 IMAGE-family conversion helpers (non-inline TU).
// Most of the conversion is small enough to live inline in convert/image_fmt.h (format mapping, block
// geometry); this TU exists so the build's src/**/*.cpp glob has an image-convert object and to host
// the heavier validation helper used by both sides.
#include "image_fmt.h"
#include "../common/log.h"

namespace cvtimg {

uint32_t validate_pixel_size(const ImageBlobHeader& h, uint32_t availableBytes) {
    uint32_t computed = iw3_total_size(h.iw3Format, h.width, h.height, h.levelCount);

    if (h.pixelSize != 0 && h.pixelSize <= availableBytes) {
        if (computed != 0 && h.pixelSize != computed) {
            zt::debug("image: stored pixelSize %u != computed mip-chain %u (fmt=%d %ux%u L%u) — "
                      "using stored",
                      h.pixelSize, computed, h.iw3Format, h.width, h.height, h.levelCount);
        }
        return h.pixelSize;
    }

    uint32_t use = computed;
    if (use == 0 || use > availableBytes)
        use = availableBytes;
    zt::warn("image: pixelSize unusable (stored=%u avail=%u) — using %u", h.pixelSize,
             availableBytes, use);
    return use;
}

} // namespace cvtimg
