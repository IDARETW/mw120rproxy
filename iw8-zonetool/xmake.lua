-- XMake build for the offline IW3 dump-to-IW8 package converter.
-- Run from this directory:
--   xmake f -m release -a x64 -y && xmake

set_project("iw8-zonetool")
set_xmakever("2.8.0")

set_allowedplats("windows")
set_allowedarchs("x64")
set_allowedmodes("debug", "release")
set_defaultmode("release")
set_languages("cxx20", "c17")

local MODE_DEBUG = is_mode("debug")
local CFG = MODE_DEBUG and "Debug" or "Release"

target("iw8-zonetool")
    set_kind("binary")
    set_basename("iw8-zonetool")
    set_targetdir(path.join(os.projectdir(), "xmake-out", "x64", CFG))
    set_objectdir(path.join(os.projectdir(), ".xmake", "obj", CFG))

    add_defines("WIN32", "NOMINMAX", "WIN32_LEAN_AND_MEAN", "_CRT_SECURE_NO_WARNINGS")
    add_includedirs("src", "src/common", "src/common/zlib")
    add_syslinks("kernel32")
    add_cxflags("/permissive-", "/EHsc", "/W3", {tools = "cl"})

    if MODE_DEBUG then
        set_runtimes("MTd")
        add_defines("_DEBUG")
        set_symbols("debug")
    else
        set_runtimes("MT")
        add_defines("NDEBUG")
        add_cxflags("/Gy", "/Oi", {tools = "cl"})
        add_ldflags("/OPT:REF", "/OPT:ICF", "/Brepro", "/emittoolversioninfo:no", {force = true})
        set_symbols("debug")
    end

    add_files("src/**.cpp")
    add_files("src/common/zlib/**.c", {warnings = "none"})
