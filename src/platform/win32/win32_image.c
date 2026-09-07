// win32_image.c - os_image_decode over the Windows Imaging Component (ADR-007).
//
// windowscodecs.dll is loaded on the first decode, so the import table stays
// kernel32 + user32 and the exe carries no decoder of its own. wincodec.h is
// C++ only in practice, so the four interfaces we touch are declared here as
// plain vtables, exactly like win32_dialog.c and win32_font_dwrite.c do (P-006).
// Only the *order* of the slots is the ABI: the methods we never call stay
// opaque pointers.
#include <windows.h>

#include "../platform.h"

typedef struct WicUnknown WicUnknown;
typedef struct WicUnknownVtbl {
    HRESULT(WINAPI *QueryInterface)(WicUnknown *self, const GUID *iid, void **out);
    ULONG(WINAPI *AddRef)(WicUnknown *self);
    ULONG(WINAPI *Release)(WicUnknown *self);
} WicUnknownVtbl;
struct WicUnknown { WicUnknownVtbl *lpVtbl; };

md_inline void wic_release(void *object) {
    if (object) {
        WicUnknown *unknown = (WicUnknown *)object;
        unknown->lpVtbl->Release(unknown);
    }
}

// IWICBitmapSource, and every interface that extends it (frame, converter,
// scaler) with its own tail. One declaration, three uses.
typedef struct WicSource WicSource;
typedef struct WicSourceVtbl {
    void *slots_unknown[3];
    HRESULT(WINAPI *GetSize)(WicSource *self, u32 *width, u32 *height);
    void *slots_1[3];  // GetPixelFormat, GetResolution, CopyPalette
    HRESULT(WINAPI *CopyPixels)(WicSource *self, const void *rect, u32 stride, u32 buffer_size,
                                u8 *buffer);
    // IWICFormatConverter::Initialize, and IWICBitmapScaler::Initialize: the
    // same slot, two signatures, which is why each has its own typedef below.
    void *initialize;
} WicSourceVtbl;
struct WicSource { WicSourceVtbl *lpVtbl; };

typedef HRESULT(WINAPI *WicConverterInitialize)(WicSource *self, WicSource *source,
                                                const GUID *format, u32 dither, void *palette,
                                                double alpha_threshold, u32 palette_type);
typedef HRESULT(WINAPI *WicScalerInitialize)(WicSource *self, WicSource *source, u32 width,
                                             u32 height, u32 interpolation);

typedef struct WicDecoder WicDecoder;
typedef struct WicDecoderVtbl {
    void *slots_unknown[3];
    void *slots_1[10];  // QueryCapability .. GetFrameCount
    HRESULT(WINAPI *GetFrame)(WicDecoder *self, u32 index, WicSource **out);
} WicDecoderVtbl;
struct WicDecoder { WicDecoderVtbl *lpVtbl; };

typedef struct WicStream WicStream;
typedef struct WicStreamVtbl {
    void *slots_unknown[3];
    void *slots_istream[11];  // Read .. Clone
    void *slots_1[1];         // InitializeFromIStream
    void *slots_2[1];         // InitializeFromFilename
    HRESULT(WINAPI *InitializeFromMemory)(WicStream *self, u8 *buffer, u32 size);
} WicStreamVtbl;
struct WicStream { WicStreamVtbl *lpVtbl; };

typedef struct WicFactory WicFactory;
typedef struct WicFactoryVtbl {
    void *slots_unknown[3];
    void *slots_1[1];  // CreateDecoderFromFilename
    HRESULT(WINAPI *CreateDecoderFromStream)(WicFactory *self, WicStream *stream,
                                             const GUID *vendor, u32 metadata_options,
                                             WicDecoder **out);
    void *slots_2[5];  // CreateDecoderFromFileHandle .. CreatePalette
    HRESULT(WINAPI *CreateFormatConverter)(WicFactory *self, WicSource **out);
    HRESULT(WINAPI *CreateBitmapScaler)(WicFactory *self, WicSource **out);
    void *slots_3[2];  // CreateBitmapClipper, CreateBitmapFlipRotator
    HRESULT(WINAPI *CreateStream)(WicFactory *self, WicStream **out);
} WicFactoryVtbl;
struct WicFactory { WicFactoryVtbl *lpVtbl; };

// CLSID_WICImagingFactory1: present since Vista, and the one the "2" CLSID
// falls back to anyway. IID_IWICImagingFactory is the same on both.
static const GUID win32_clsid_wic_factory = {
    0xCACAF262, 0x9370, 0x4615, {0xA1, 0x3B, 0x9F, 0x55, 0x39, 0xDA, 0x4C, 0x0A}};
static const GUID win32_iid_wic_factory = {
    0xEC5EC8A9, 0xC395, 0x4314, {0x9C, 0x77, 0x54, 0xD7, 0xA9, 0x35, 0xFF, 0x70}};
// 32bppPBGRA: premultiplied, which is exactly what the renderer blends with.
static const GUID win32_wic_format_32bpp_pbgra = {
    0x6FDDC324, 0x4E03, 0x4BFE, {0xB1, 0x85, 0x3D, 0x77, 0x76, 0x8D, 0xC9, 0x10}};

#define WIC_DECODE_METADATA_ON_DEMAND 0u
#define WIC_DITHER_NONE               0u
#define WIC_PALETTE_CUSTOM            0u
#define WIC_INTERPOLATION_FANT        3u  // the box/bicubic hybrid, best for downscales
#define COINIT_MULTITHREADED_         0x0u
#define WIN32_CLSCTX_INPROC           0x1u  // CLSCTX_INPROC_SERVER

// A cover bigger than this is a scan of a booklet, not album art: decoding it
// full size would cost 100 MB of scratch for a 48 px thumbnail.
#define WIN32_IMAGE_MAX_DIM 8192

typedef HRESULT(WINAPI *Win32CoInitializeEx2)(void *reserved, DWORD flags);
typedef HRESULT(WINAPI *Win32CoCreateInstance2)(const GUID *clsid, void *outer, DWORD context,
                                                const GUID *iid, void **out);

typedef struct Win32ImageApi {
    volatile LONG state;  // 0 idle, 1 loading, 2 ready or failed
    Win32CoInitializeEx2 co_initialize;
    Win32CoCreateInstance2 co_create;
    b32 available;
} Win32ImageApi;

global Win32ImageApi win32_image;

static b32 win32_image_load(void) {
    // Decoding runs on job threads: the load happens once, whoever gets there
    // first, and the others spin on the state word rather than loading twice.
    LONG state = InterlockedCompareExchange(&win32_image.state, 1, 0);
    if (state == 0) {
        HMODULE ole = LoadLibraryW(L"ole32.dll");
        HMODULE wic = LoadLibraryW(L"windowscodecs.dll");
        if (ole && wic) {
            win32_image.co_initialize = (Win32CoInitializeEx2)GetProcAddress(ole, "CoInitializeEx");
            win32_image.co_create =
                    (Win32CoCreateInstance2)GetProcAddress(ole, "CoCreateInstance");
            win32_image.available = win32_image.co_create != 0;
        }
        InterlockedExchange(&win32_image.state, 2);
        return win32_image.available;
    }
    while (InterlockedCompareExchange(&win32_image.state, 2, 2) != 2) { os_cpu_pause(); }
    return win32_image.available;
}

// The apartment of this thread. Every thread that decodes joins one; we never
// leave it, because a job thread will be back within the millisecond. WIC
// objects are free threaded, so the mode we get does not matter.
static b32 win32_image_thread_init(void) {
    static __declspec(thread) b32 joined;
    if (joined) { return 1; }
    win32_image.co_initialize(0, COINIT_MULTITHREADED_);
    joined = 1;
    return 1;
}

// BGRA -> RGBA in place: the red and blue lanes swap, alpha and green stay.
static void win32_image_bgra_to_rgba(u8 *pixels, u64 count) {
    for (u64 i = 0; i < count; i += 1) {
        u8 blue = pixels[i * 4 + 0];
        pixels[i * 4 + 0] = pixels[i * 4 + 2];
        pixels[i * 4 + 2] = blue;
    }
}

b32 os_image_decode(Arena *arena, String8 bytes, u32 size, OsImage *out) {
    out->pixels = 0;
    out->width = 0;
    out->height = 0;
    // The boundary: the bytes came off a disk, and an empty or absurd buffer is
    // an answer, not a crash.
    if (bytes.size < 16 || bytes.size > U32_MAX) { return 0; }
    if (!win32_image_load()) { return 0; }
    win32_image_thread_init();

    WicFactory *factory = 0;
    if (win32_image.co_create(&win32_clsid_wic_factory, 0, WIN32_CLSCTX_INPROC,
                              &win32_iid_wic_factory, (void **)&factory) != S_OK) {
        return 0;
    }

    b32 ok = 0;
    WicStream *stream = 0;
    WicDecoder *decoder = 0;
    WicSource *frame = 0;
    WicSource *converter = 0;
    WicSource *scaler = 0;
    WicSource *source = 0;
    u32 width = 0, height = 0;

    if (factory->lpVtbl->CreateStream(factory, &stream) != S_OK) { goto done; }
    if (stream->lpVtbl->InitializeFromMemory(stream, bytes.str, (u32)bytes.size) != S_OK) {
        goto done;
    }
    if (factory->lpVtbl->CreateDecoderFromStream(factory, stream, 0,
                                                 WIC_DECODE_METADATA_ON_DEMAND,
                                                 &decoder) != S_OK) {
        goto done;
    }
    if (decoder->lpVtbl->GetFrame(decoder, 0, &frame) != S_OK) { goto done; }
    if (frame->lpVtbl->GetSize(frame, &width, &height) != S_OK) { goto done; }
    if (width == 0 || height == 0 || width > WIN32_IMAGE_MAX_DIM ||
        height > WIN32_IMAGE_MAX_DIM) {
        goto done;
    }

    if (factory->lpVtbl->CreateFormatConverter(factory, &converter) != S_OK) { goto done; }
    if (((WicConverterInitialize)converter->lpVtbl->initialize)(
                converter, frame, &win32_wic_format_32bpp_pbgra, WIC_DITHER_NONE, 0, 0.0,
                WIC_PALETTE_CUSTOM) != S_OK) {
        goto done;
    }
    source = converter;

    if (size != 0 && (width != size || height != size)) {
        if (factory->lpVtbl->CreateBitmapScaler(factory, &scaler) != S_OK) { goto done; }
        if (((WicScalerInitialize)scaler->lpVtbl->initialize)(scaler, converter, size, size,
                                                              WIC_INTERPOLATION_FANT) != S_OK) {
            goto done;
        }
        source = scaler;
        width = size;
        height = size;
    }

    {
        u64 stride = (u64)width * 4;
        u64 total = stride * height;
        u8 *pixels = push_array(arena, u8, total);
        if (source->lpVtbl->CopyPixels(source, 0, (u32)stride, (u32)total, pixels) != S_OK) {
            goto done;
        }
        win32_image_bgra_to_rgba(pixels, (u64)width * height);
        out->pixels = pixels;
        out->width = width;
        out->height = height;
        ok = 1;
    }

done:
    wic_release(scaler);
    wic_release(converter);
    wic_release(frame);
    wic_release(decoder);
    wic_release(stream);
    wic_release(factory);
    return ok;
}
