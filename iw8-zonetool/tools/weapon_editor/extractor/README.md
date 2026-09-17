# Local Replay reader

Stock previews and library setup use the open-source
[ACTS Replay reader](https://github.com/IDARETW/atian-cod-tools), pinned to
`d10b9cca0785f5d5e338eec3be2e170599f66d6c`. `replay-skin.patch` adds native vertex
weights to geometry exports, keeps sound banks on the structured schema reader,
and normalizes asset paths for Windows long-path writes.
Editor setup applies the Replay attachment layout corrections from `layout.py` to
a private copy of the schema beside a workspace-local reader.
It contains source changes only. The MIT license for ACTS is included here.

Install Git, CMake 3.20 or newer, and Visual Studio with Desktop development with C++
and a Windows SDK. Use a short path with several GB free for the source and build.
From the mw120rproxy repository root:

```powershell
.\iw8-zonetool\tools\weapon_editor\extractor\build.ps1 `
  -SourceDir C:\Tools\replay-reader-src -OutputDir C:\Tools\replay-reader
```

The script clones the pinned public source and its dependencies, applies the source
patch, and builds the command-line target without Qt or OpenCL. Re-running uses the
same checkout and recognizes an already applied patch. It never resets an existing
checkout. Choose another SourceDir if an existing directory has unrelated changes.
The first build can take a while; submodules and C++ objects are kept for subsequent builds.

The output must contain `acts.exe`, `acts-common.dll`, and `data/mw19/schema.json`.
Keep them together. Pass the executable to the editor's `setup.py --exporter` option.
An arbitrary upstream ACTS release is not interchangeable with this pinned reader.
No game executable, Oodle DLL, fastfile, XPak archive, model, animation, texture, or
sound is supplied by this script. Those come from the user's own Replay installation.
