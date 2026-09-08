set_project("iw8-import-offline-tests")
set_languages("cxx23")
set_allowedplats("windows")
set_allowedarchs("x64")
set_defaultmode("release")

option("oat")
    set_default(path.absolute("../../external/OpenAssetTools", os.projectdir()))
    set_showmenu(true)
    set_description("Pinned OpenAssetTools source directory for native exporter tests")
option_end()

local oat = get_config("oat")
if oat and os.isfile(path.join(oat, "src/Common/Game/IW4/IW4.h")) then
    target("oat-export-fixture")
        set_kind("binary")
        set_targetdir("bin")
        add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN", "_CRT_SECURE_NO_WARNINGS", "ARCH_x64")
        add_includedirs("../tools/oat", path.join(oat, "src/ObjWriting"),
            path.join(oat, "src/ObjCommon"), path.join(oat, "src/ZoneCommon"),
            path.join(oat, "src/Common"), path.join(oat, "src/Utils"),
            path.join(oat, "thirdparty/json/single_include"), path.join(oat, "build/premake"))
        add_files("oat_export_fixture.cpp", path.join(oat, "src/Common/Game/IW4/CommonIW4.cpp"),
            path.join(oat, "src/Common/Game/IW5/CommonIW5.cpp"),
            path.join(oat, "src/Common/Utils/Pack.cpp"),
            path.join(oat, "src/Common/Utils/HalfFloat.cpp"))
        add_cxflags("/EHsc", "/Gy", {tools = "cl"})
        add_ldflags("/OPT:REF", {tools = "link"})
end

target("collision-validate")
    set_kind("binary")
    set_targetdir("bin")
    add_includedirs("../../mw120rproxy")
    add_files("collision_validate.cpp")
    add_cxflags("/EHsc", {tools = "cl"})
