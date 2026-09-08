#include "collision_file.h"
#include <iostream>

// Exercise the actual proxy-side disk parser without linking or starting the game.
int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::cerr << "usage: collision-validate <collision.bin> [...]\n";
        return 2;
    }
    for (int i = 1; i < argc; ++i) {
        std::vector<collisionfile::Brush> hulls;
        if (!collisionfile::Load(std::filesystem::path(argv[i]), hulls)) {
            std::wcerr << L"Rejected collision file: " << argv[i] << L'\n';
            return 1;
        }
        std::wcout << argv[i] << L": " << hulls.size() << L" hulls accepted\n";
    }
    return 0;
}
