#include "utils.h"

#include <windows.h>
#include <string>
#include <filesystem>
#include <wincodec.h>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "Ole32.lib")

// Out-of-line definition of the header's TempBmp::cleanup (avoids the previous duplicate-struct ODR
// hazard where utils.cpp defined its own TempBmp and never included utils.h).
void TempBmp::cleanup() const
{
    if (!path.empty()) DeleteFileA(path.c_str());
}

TempBmp convertedBMP(LPCSTR imagePath)
{
    TempBmp result;

    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    IWICBitmapEncoder* encoder = nullptr;
    IWICBitmapFrameEncode* outFrame = nullptr;
    IWICStream* stream = nullptr;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    bool initializedCOM =
        SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;

    if (!initializedCOM)
        return result;

    hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));

    if (FAILED(hr))
    {
        CoUninitialize();
        return result;
    }

    wchar_t tempPath[MAX_PATH];
    wchar_t tempFile[MAX_PATH];

    GetTempPathW(MAX_PATH, tempPath);
    GetTempFileNameW(tempPath, L"wic", 0, tempFile);

    std::filesystem::path bmpPath = tempFile;
    bmpPath.replace_extension(L".bmp");

    DeleteFileW(bmpPath.c_str());

    result.path = bmpPath.string();

    int len = MultiByteToWideChar(
        CP_UTF8,
        0,
        imagePath,
        -1,
        nullptr,
        0);

    std::wstring wide(len, L'\0');

    MultiByteToWideChar(
        CP_UTF8,
        0,
        imagePath,
        -1,
        wide.data(),
        len);

    hr = factory->CreateDecoderFromFilename(
        wide.c_str(),
        nullptr,
        GENERIC_READ,
        WICDecodeMetadataCacheOnDemand,
        &decoder);

    if (SUCCEEDED(hr))
        hr = decoder->GetFrame(0, &frame);

    if (SUCCEEDED(hr))
        hr = factory->CreateFormatConverter(&converter);

    if (SUCCEEDED(hr))
    {
        hr = converter->Initialize(
            frame,
            GUID_WICPixelFormat32bppBGRA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeMedianCut);
    }

    if (SUCCEEDED(hr))
        hr = factory->CreateStream(&stream);

    if (SUCCEEDED(hr))
    {
        hr = stream->InitializeFromFilename(
            bmpPath.c_str(),
            GENERIC_WRITE);
    }

    if (SUCCEEDED(hr))
    {
        hr = factory->CreateEncoder(
            GUID_ContainerFormatBmp,
            nullptr,
            &encoder);
    }

    if (SUCCEEDED(hr))
        hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);

    if (SUCCEEDED(hr))
        hr = encoder->CreateNewFrame(&outFrame, nullptr);

    if (SUCCEEDED(hr))
        hr = outFrame->Initialize(nullptr);

    UINT width = 0;
    UINT height = 0;

    if (SUCCEEDED(hr))
        hr = frame->GetSize(&width, &height);

    if (SUCCEEDED(hr))
        hr = outFrame->SetSize(width, height);

    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;

    if (SUCCEEDED(hr))
        hr = outFrame->SetPixelFormat(&format);

    if (SUCCEEDED(hr))
        hr = outFrame->WriteSource(converter, nullptr);

    if (SUCCEEDED(hr))
        hr = outFrame->Commit();

    if (SUCCEEDED(hr))
        hr = encoder->Commit();

    if (outFrame) outFrame->Release();
    if (encoder) encoder->Release();
    if (stream) stream->Release();
    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (factory) factory->Release();

    CoUninitialize();

    if (FAILED(hr))
    {
        DeleteFileA(result.path.c_str());
        result.path.clear();
    }

    return result;
}
