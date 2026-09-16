#ifndef _WINNETWK_STUB_
#define _WINNETWK_STUB_
// ---------------------------------------------------------------------------
// LOCAL SHIM — not the real header.
//
// This machine's Windows SDK (10.0.26100.0) is missing <winnetwk.h> (every other
// um header is present; this single file vanished mid-session, see the note the
// prior session left in ..\..\min_xinput\winnetwk.h). windows.h includes
// <winnetwk.h> unconditionally (line 190, gated by _MAC — NOT by
// WIN32_LEAN_AND_MEAN), so a fresh compile cannot proceed without it.
//
// mw164proxy uses no WNet*/mpr APIs, and the project defines WIN32_LEAN_AND_MEAN,
// so nothing windows.h pulls in references winnetwk's types after this point — an
// empty stub is enough to let the translation unit compile. This dir is on the
// include path AHEAD of the SDK um dir, so if the real header is ever restored it
// still takes precedence for `""` includes; for the `<winnetwk.h>` angled include
// windows.h uses, this shim is found.
//
// PROPER FIX: repair the SDK (Visual Studio Installer -> Modify -> repair the
// "Windows 10/11 SDK" component), then this shim can be deleted and the
// sdk_shim include dir + the shim's use removed.
// ---------------------------------------------------------------------------
#endif // _WINNETWK_STUB_
