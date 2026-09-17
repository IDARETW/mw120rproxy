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
    add_includedirs("src/thirdparty")
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
    end

    add_files("src/zonetool/**.cpp")
    add_files("src/zonetool/**.rc")
    add_files("src/common/**.cpp")
    add_files("src/thirdparty/lz4/lz4.c")
    add_files("src/thirdparty/directxtex/BC6HBC7.cpp")

    -- xmake's Windows resource rule tracks the .rc file but not the RCDATA
    -- files named inside it. Drop only the derived resource object when an
    -- embedded shader/template is newer so an ordinary incremental build
    -- cannot silently ship stale converter resources.
    on_load(function (target)
        local rcfile = path.join(os.projectdir(), "src", "zonetool", "iw3", "resources.rc")
        local objectfile = path.join(target:objectdir(), "src", "zonetool", "iw3", "resources.rc.obj")
        if not os.isfile(objectfile) then
            return
        end
        local object_mtime = os.mtime(objectfile)
        local resources = {
            "material_template.json",
            "techset_template.json",
            "map_surface_realtime.hlsl",
            "replay_world_vertex.hlsl",
            "replay_model_vertex.hlsl"
        }
        for _, name in ipairs(resources) do
            local resource = path.join(os.projectdir(), "src", "zonetool", "iw3", "resources", name)
            if os.mtime(resource) > object_mtime then
                os.rm(objectfile)
                os.rm(target:dependfile(objectfile))
                break
            end
        end
    end)

target("vfx-smoke")
    set_kind("binary")
    set_default(false)
    set_targetdir(path.join(os.projectdir(), "xmake-out", "x64", is_mode("debug") and "Debug" or "Release"))
    add_defines("WIN32", "NOMINMAX", "WIN32_LEAN_AND_MEAN", "_CRT_SECURE_NO_WARNINGS")
    add_includedirs("src", "src/common", "src/zonetool")
    add_syslinks("kernel32")
    add_files("tests/replay_vfx_smoke.cpp", "src/zonetool/iw8/iw8_zone.cpp",
              "src/zonetool/iw8/replay_vfx.cpp", "src/common/ff_io.cpp", "src/common/fs_util.cpp")

target("rawfile-smoke")
    set_kind("binary")
    set_default(false)
    set_targetdir(path.join(os.projectdir(), "xmake-out", "x64", is_mode("debug") and "Debug" or "Release"))
    add_defines("WIN32", "NOMINMAX", "WIN32_LEAN_AND_MEAN", "_CRT_SECURE_NO_WARNINGS")
    add_includedirs("src", "src/common", "src/zonetool")
    add_syslinks("kernel32")
    add_files("tests/replay_rawfile_smoke.cpp", "src/zonetool/iw8/iw8_zone.cpp",
              "src/zonetool/iw8/replay_rawfile.cpp", "src/zonetool/iw8/replay_script.cpp",
              "src/common/ff_io.cpp", "src/common/fs_util.cpp")
