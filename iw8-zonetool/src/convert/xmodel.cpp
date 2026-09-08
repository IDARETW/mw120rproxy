

#include "xmodel_convert.h"

namespace convert::xmodel_cv {

// Validate/normalize a Record after load (writer-side guard): clamp array lengths to the declared
// counts so a hand-edited or partially-dumped JSON can never make the writer over-read. Returns the
// number of fields it had to repair (0 = clean).
int normalize(Record& r) {
    int repaired = 0;
    const size_t animBones =
        (r.numBones > r.numRootBones) ? static_cast<size_t>(r.numBones - r.numRootBones) : 0;

    if (r.boneNames.size() != r.numBones) {
        r.boneNames.resize(r.numBones);
        ++repaired;
    }
    if (r.parentList.size() != animBones) {
        r.parentList.resize(animBones);
        ++repaired;
    }
    if (r.quats.size() != animBones * 4) {
        r.quats.resize(animBones * 4);
        ++repaired;
    }
    if (r.trans.size() != animBones * 3) {
        r.trans.resize(animBones * 3);
        ++repaired;
    }
    // partClassification is optional; only clamp if non-empty and wrong.
    if (!r.partClassification.empty() && r.partClassification.size() != r.numBones) {
        r.partClassification.resize(r.numBones);
        ++repaired;
    }
    if (r.materials.size() != r.numsurfs) {
        r.materials.resize(r.numsurfs);
        ++repaired;
    }
    if (r.lods.size() != r.numLods) {
        r.lods.resize(r.numLods);
        ++repaired;
    }
    return repaired;
}

} // namespace convert::xmodel_cv
