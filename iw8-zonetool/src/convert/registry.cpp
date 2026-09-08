// registry.cpp — Stage-B dispatch: IW8 type-name string -> the right iw8_write_<fam>.
#include "registry.h"
#include "../common/log.h"
#include <cstring>

namespace convert {

bool dispatchIw8Write(const char* iw8TypeName,
                      iw8::ZoneWriter& zw,
                      iw3sr::ZoneSource& zs,
                      const char* name) {
    if (!iw8TypeName)
        return false;
    if (std::strcmp(iw8TypeName, "material") == 0) {
        iw8_write_material(zw, zs, name);
        return true;
    }
    if (std::strcmp(iw8TypeName, "image") == 0) {
        iw8_write_image(zw, zs, name);
        return true;
    }
    if (std::strcmp(iw8TypeName, "xmodel") == 0) {
        iw8_write_xmodel(zw, zs, name);
        return true;
    }
    if (std::strcmp(iw8TypeName, "xmodelsurfs") == 0) {
        iw8_write_xsurface(zw, zs, name);
        return true;
    }
    if (std::strcmp(iw8TypeName, "map_ents") == 0 || std::strcmp(iw8TypeName, "col_map") == 0 ||
        std::strcmp(iw8TypeName, "com_map") == 0) {
        iw8_write_maps(zw, zs, name);
        return true;
    }
    zt::warn("dispatch: unhandled IW8 type '%s' (asset '%s') — skipped", iw8TypeName,
             name ? name : "?");
    return false;
}

} // namespace convert
