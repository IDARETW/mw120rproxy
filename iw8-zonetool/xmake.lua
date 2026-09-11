set_project("iw8-zonetool")
set_xmakever("2.8.0")

set_allowedplats("windows")
set_allowedarchs("x64")
set_allowedmodes("debug", "release")
set_defaultmode("release")
set_languages("cxx20", "c17")

target("iw8-zonetool")
    set_kind("binary")
    set_basename("iw8-zonetool")
    set_targetdir(path.join(os.projectdir(), "xmake-out", "x64", is_mode("debug") and "Debug" or "Release"))
    set_objectdir(path.join(os.projectdir(), ".xmake", "obj", is_mode("debug") and "Debug" or "Release"))

    add_defines("WIN32", "NOMINMAX", "WIN32_LEAN_AND_MEAN", "_CRT_SECURE_NO_WARNINGS")
    add_includedirs("src", "src/common", "src/zonetool")
    add_syslinks("bcrypt", "d3dcompiler", "kernel32")
    add_cxflags("/permissive-", "/EHsc", "/W4", {tools = "cl"})
    add_ldflags("/BASE:0x180000000", "/DYNAMICBASE:NO", {force = true})

    if is_mode("debug") then
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

    add_files("src/zonetool/**.cpp")
    add_files("src/zonetool/**.rc")
    add_files("src/common/**.cpp")
    add_files("src/thirdparty/lz4/lz4.c")
