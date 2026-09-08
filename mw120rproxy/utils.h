#pragma once
#include <string>
#include <windows.h>

// Splash-image helper: convert any image (.png/.jpg/…) to a temp .bmp for LoadImageA(LR_LOADFROMFILE).
struct TempBmp
{
    std::string path;      // temp .bmp path; empty on conversion failure
    void cleanup() const;  // delete the temp file (defined in utils.cpp)
};

TempBmp convertedBMP(LPCSTR imagePath);
