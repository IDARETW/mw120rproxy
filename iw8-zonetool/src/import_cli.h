#pragma once
// Run the Python adapter layer without a shell. All original argument boundaries
// are preserved, including paths containing spaces and shell metacharacters.
int runImportCli(int argc, wchar_t** argv);
