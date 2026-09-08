#include "import_cli.h"
#include <Windows.h>
#include <process.h>
#include <filesystem>
#include <string>
#include <vector>
#include <cstdio>

namespace {
// Windows CRT spawn concatenates argv into a command line; encode each argument
// using the inverse CommandLineToArgvW rules (including trailing backslashes).
std::wstring quoteArgument(const std::wstring& input) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (const wchar_t ch : input) {
        if (ch == L'\\') {
            ++slashes;
            continue;
        }
        result.append(slashes * (ch == L'\"' ? 2 : 1), L'\\');
        slashes = 0;
        if (ch == L'\"')
            result.push_back(L'\\');
        result.push_back(ch);
    }
    result.append(slashes * 2, L'\\');
    result.push_back(L'\"');
    return result;
}
}

int runImportCli(int argc, wchar_t** argv) {
    wchar_t module[32768]{};
    const DWORD size = GetModuleFileNameW(nullptr, module, 32768);
    if (!size || size == 32768) {
        std::fputs("Cannot locate native writer\n", stderr);
        return 1;
    }
    const std::filesystem::path executable(module);
    auto root = executable.parent_path();
    std::filesystem::path script;
    for (int i = 0; i < 6 && !root.empty(); ++i, root = root.parent_path()) {
        const auto candidate = root / "tools" / "import_map.py";
        if (std::filesystem::is_regular_file(candidate)) {
            script = candidate;
            break;
        }
    }
    if (script.empty()) {
        std::fputs("Missing tools/import_map.py beside the iw8-zonetool distribution\n", stderr);
        return 1;
    }
    const wchar_t* configured = _wgetenv(L"IW8_ZONETOOL_PYTHON");
    std::vector<std::wstring> words{configured && *configured ? configured : L"python",
                                    script.wstring()};
    for (int i = 1; i < argc; ++i)
        words.emplace_back(argv[i]);
    if (argc > 1 && std::wstring(argv[1]) == L"import") {
        words.emplace_back(L"--writer");
        words.emplace_back(executable.wstring());
    }
    std::vector<std::wstring> quoted;
    for (const auto& word : words)
        quoted.push_back(quoteArgument(word));
    std::vector<const wchar_t*> pointers;
    for (const auto& word : quoted)
        pointers.push_back(word.c_str());
    pointers.push_back(nullptr);
    const intptr_t result = _wspawnvp(_P_WAIT, words.front().c_str(), pointers.data());
    if (result == -1) {
        std::fputs("Cannot start Python 3.10+; install Pillow or set IW8_ZONETOOL_PYTHON\n",
                   stderr);
        return 1;
    }
    return static_cast<int>(result);
}
