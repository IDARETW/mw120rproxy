#include "common/ff_io.h"
#include "zonetool/iw8/replay_rawfile.h"

#include <cstdint>
#include <string>
#include <vector>

int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;

    iw8::ZoneWriter writer;
    iw8::rawfile::Register(
        writer, "mw120r/rawfile_contract_probe",
        {'h', 'e', 'l', 'l', 'o', ' ', 'r', 'a', 'w', 'f', 'i', 'l', 'e'});
    iw8::rawfile::Register(writer, "mw120r/rawfile_empty_probe", {});
    writer.build();

    zt::Iw8WriteParams params;
    for (int index = 0; index < iw8::IW8_MAX_XFILE_COUNT; ++index)
    {
        params.blockSize[index] = writer.blockSize(index);
        params.totalDecompressed += params.blockSize[index];
    }
    params.calcSize = writer.calcSize();
    return zt::iw8_write(argv[1], writer.body(), params) ? 0 : 1;
}
