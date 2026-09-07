// win32_media.c - os_media_decoder_* over Media Foundation (T-040, ADR-007).
//
// AAC/M4A, ALAC and WMA are decoded by Windows, so not one byte of their code
// ships in the exe. mfplat.dll and mfreadwrite.dll are loaded on the first open
// and never unloaded, which keeps the import table at kernel32 + user32.
//
// The interfaces are declared here as plain vtables, the same way win32_image.c
// and win32_dialog.c do it (P-006): the SDK headers would drag in mfuuid.lib for
// the GUID constants, and only the *order* of the slots is the ABI. Methods we
// never call stay opaque pointers.
//
// IMFSourceReader is built with MFCreateSourceReaderFromURL, not from an
// IMFByteStream over our own reader. Authoring an IMFByteStream in C means a
// hand written vtable of fourteen methods plus an IMFAsyncCallback for the
// asynchronous half, roughly 250 lines and 3 KB of code, to replace a file
// reader that Windows already implements and that reads the file lazily anyway.
// The 256 KB windowing rule of T-040 is about *our* decoders holding a whole
// file in memory; the Media Foundation source does its own paged I/O.
#include <windows.h>

#include "../platform.h"

// --- the slice of Media Foundation we speak --------------------------------

typedef struct MfPropVariant {
    u16 vt;
    u16 reserved[3];
    union {
        i64 as_i64;
        u64 as_u64;
        u8 raw[16];
    } value;
} MfPropVariant;

#define MF_VT_EMPTY 0
#define MF_VT_I8    20
#define MF_VT_UI8   21

typedef struct MfUnknown MfUnknown;
typedef struct MfUnknownVtbl {
    HRESULT(WINAPI *QueryInterface)(MfUnknown *self, const GUID *iid, void **out);
    ULONG(WINAPI *AddRef)(MfUnknown *self);
    ULONG(WINAPI *Release)(MfUnknown *self);
} MfUnknownVtbl;
struct MfUnknown { MfUnknownVtbl *lpVtbl; };

md_inline void mf_release(void *object) {
    if (object) {
        MfUnknown *unknown = (MfUnknown *)object;
        unknown->lpVtbl->Release(unknown);
    }
}

// IMFMediaType, which is an IMFAttributes with five extra slots. We only ever
// read two integers and write two GUIDs.
typedef struct MfMediaType MfMediaType;
typedef struct MfMediaTypeVtbl {
    void *slots_unknown[3];
    void *slots_get[4];  // GetItem, GetItemType, CompareItem, Compare
    HRESULT(WINAPI *GetUINT32)(MfMediaType *self, const GUID *key, u32 *value);
    void *slots_get_2[13];  // GetUINT64 .. DeleteAllItems
    void *slots_set_u32;    // SetUINT32
    void *slots_set_2[2];   // SetUINT64, SetDouble
    HRESULT(WINAPI *SetGUID)(MfMediaType *self, const GUID *key, const GUID *value);
} MfMediaTypeVtbl;
struct MfMediaType { MfMediaTypeVtbl *lpVtbl; };

typedef struct MfMediaBuffer MfMediaBuffer;
typedef struct MfMediaBufferVtbl {
    void *slots_unknown[3];
    HRESULT(WINAPI *Lock)(MfMediaBuffer *self, u8 **data, DWORD *max_length, DWORD *length);
    HRESULT(WINAPI *Unlock)(MfMediaBuffer *self);
    HRESULT(WINAPI *GetCurrentLength)(MfMediaBuffer *self, DWORD *length);
} MfMediaBufferVtbl;
struct MfMediaBuffer { MfMediaBufferVtbl *lpVtbl; };

// IMFSample: IMFAttributes plus the sample slots; ConvertToContiguousBuffer is
// the ninth of them.
typedef struct MfSample MfSample;
typedef struct MfSampleVtbl {
    void *slots_unknown[3];
    void *slots_attributes[30];  // GetItem .. CopyAllItems
    void *slots_sample[8];       // GetSampleFlags .. GetBufferByIndex
    HRESULT(WINAPI *ConvertToContiguousBuffer)(MfSample *self, MfMediaBuffer **out);
} MfSampleVtbl;
struct MfSample { MfSampleVtbl *lpVtbl; };

typedef struct MfSourceReader MfSourceReader;
typedef struct MfSourceReaderVtbl {
    void *slots_unknown[3];
    void *slots_get_selection;  // GetStreamSelection
    HRESULT(WINAPI *SetStreamSelection)(MfSourceReader *self, DWORD stream, BOOL selected);
    void *slots_native_type;  // GetNativeMediaType
    HRESULT(WINAPI *GetCurrentMediaType)(MfSourceReader *self, DWORD stream, MfMediaType **out);
    HRESULT(WINAPI *SetCurrentMediaType)(MfSourceReader *self, DWORD stream, DWORD *reserved,
                                         MfMediaType *type);
    HRESULT(WINAPI *SetCurrentPosition)(MfSourceReader *self, const GUID *time_format,
                                        const MfPropVariant *position);
    HRESULT(WINAPI *ReadSample)(MfSourceReader *self, DWORD stream, DWORD control_flags,
                                DWORD *actual_stream, DWORD *stream_flags, i64 *timestamp,
                                MfSample **sample);
    HRESULT(WINAPI *Flush)(MfSourceReader *self, DWORD stream);
    void *slots_service;  // GetServiceForStream
    HRESULT(WINAPI *GetPresentationAttribute)(MfSourceReader *self, DWORD stream, const GUID *key,
                                              MfPropVariant *value);
} MfSourceReaderVtbl;
struct MfSourceReader { MfSourceReaderVtbl *lpVtbl; };

#define MF_STREAM_FIRST_AUDIO  0xFFFFFFFDu
#define MF_STREAM_ALL          0xFFFFFFFEu
#define MF_STREAM_MEDIASOURCE  0xFFFFFFFFu
#define MF_READERF_ENDOFSTREAM 0x00000002u
#define MF_STARTUP_LITE        1u
#define MF_VERSION             0x00020070u  // MF_SDK_VERSION 2, MF_API_VERSION 0x70

// The GUIDs, written out rather than linked from mfuuid.lib.
static const GUID mf_guid_null = {0, 0, 0, {0, 0, 0, 0, 0, 0, 0, 0}};
static const GUID mf_mt_major_type = {
    0x48eba18e, 0xf8c9, 0x4687, {0xbf, 0x11, 0x0a, 0x74, 0xc9, 0xf9, 0x6a, 0x8f}};
static const GUID mf_mt_subtype = {
    0xf7e34c9a, 0x42e8, 0x4714, {0xb7, 0x4b, 0xcb, 0x29, 0xd7, 0x2c, 0x35, 0xe5}};
static const GUID mf_media_type_audio = {
    0x73647561, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
static const GUID mf_audio_format_float = {
    0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
static const GUID mf_mt_audio_num_channels = {
    0x37e48bf5, 0x645e, 0x4c5b, {0x89, 0xde, 0xad, 0xa9, 0xe2, 0x9b, 0x69, 0x6a}};
static const GUID mf_mt_audio_samples_per_second = {
    0x5faeeae7, 0x0290, 0x4c31, {0x9e, 0x8a, 0xc5, 0x34, 0xf6, 0x8d, 0x9d, 0xba}};
static const GUID mf_pd_duration = {
    0x6c990d33, 0xbb8e, 0x477a, {0x85, 0x98, 0x0d, 0x5d, 0x96, 0xfc, 0xd8, 0x8a}};

typedef HRESULT(WINAPI *MfStartupProc)(ULONG version, DWORD flags);
typedef HRESULT(WINAPI *MfCreateMediaTypeProc)(MfMediaType **out);
typedef HRESULT(WINAPI *MfCreateSourceReaderFromUrlProc)(const WCHAR *url, void *attributes,
                                                         MfSourceReader **out);
typedef HRESULT(WINAPI *MfCoInitializeExProc)(void *reserved, DWORD flags);

typedef struct Win32MediaApi {
    b32 tried;
    b32 ready;
    MfCreateMediaTypeProc create_media_type;
    MfCreateSourceReaderFromUrlProc create_reader_from_url;
} Win32MediaApi;

global Win32MediaApi win32_media_api;
global SRWLOCK win32_media_api_lock = SRWLOCK_INIT;

// One decoder: the reader, plus the buffer of the sample we are draining. MF
// hands out samples of whatever length it likes, so a read that asks for 4096
// frames is served from the sample still in hand before another is pulled.
typedef struct Win32MediaDecoder {
    MfSourceReader *reader;
    MfSample *sample;
    MfMediaBuffer *buffer;
    const u8 *locked;
    u64 frames_held;
    u64 frames_taken;
    u32 channels;
    u32 sample_rate;
    b32 eos;
} Win32MediaDecoder;

// Media Foundation is initialised once per process; both DLLs stay loaded for
// the lifetime of the program, which is what keeps them out of the imports.
static b32 win32_media_api_load(void) {
    AcquireSRWLockExclusive(&win32_media_api_lock);
    if (!win32_media_api.tried) {
        win32_media_api.tried = 1;
        HMODULE ole = LoadLibraryW(L"ole32.dll");
        if (ole) {
            MfCoInitializeExProc co_initialize =
                (MfCoInitializeExProc)GetProcAddress(ole, "CoInitializeEx");
            if (co_initialize) { co_initialize(0, 0x2 /* COINIT_APARTMENTTHREADED */); }
        }
        HMODULE mfplat = LoadLibraryW(L"mfplat.dll");
        HMODULE mfreadwrite = LoadLibraryW(L"mfreadwrite.dll");
        if (mfplat && mfreadwrite) {
            MfStartupProc startup = (MfStartupProc)GetProcAddress(mfplat, "MFStartup");
            win32_media_api.create_media_type =
                (MfCreateMediaTypeProc)GetProcAddress(mfplat, "MFCreateMediaType");
            win32_media_api.create_reader_from_url = (MfCreateSourceReaderFromUrlProc)
                GetProcAddress(mfreadwrite, "MFCreateSourceReaderFromURL");
            if (startup && win32_media_api.create_media_type &&
                win32_media_api.create_reader_from_url && SUCCEEDED(startup(MF_VERSION,
                                                                           MF_STARTUP_LITE))) {
                win32_media_api.ready = 1;
            }
        }
    }
    b32 ready = win32_media_api.ready;
    ReleaseSRWLockExclusive(&win32_media_api_lock);
    return ready;
}

static void win32_media_drop_sample(Win32MediaDecoder *decoder) {
    if (decoder->buffer) {
        decoder->buffer->lpVtbl->Unlock(decoder->buffer);
        mf_release(decoder->buffer);
        decoder->buffer = 0;
    }
    mf_release(decoder->sample);
    decoder->sample = 0;
    decoder->locked = 0;
    decoder->frames_held = 0;
    decoder->frames_taken = 0;
}

b32 os_media_decoder_open(OsMediaDecoder *out, String8 path, OsMediaInfo *info) {
    out->v = 0;
    if (!win32_media_api_load()) { return 0; }

    ArenaTemp scratch = scratch_begin(0, 0);
    String16 path16 = str16_from_str8(scratch.arena, path);
    MfSourceReader *reader = 0;
    HRESULT hr = win32_media_api.create_reader_from_url((const WCHAR *)path16.str, 0, &reader);
    scratch_end(scratch);
    if (FAILED(hr) || !reader) { return 0; }

    // Force PCM float 32 bit out of whatever the file holds: the decoder that
    // Windows picks converts for us, and the pipeline of ADR-007 starts in f32.
    MfMediaType *type = 0;
    if (FAILED(win32_media_api.create_media_type(&type))) {
        mf_release(reader);
        return 0;
    }
    b32 configured =
        SUCCEEDED(type->lpVtbl->SetGUID(type, &mf_mt_major_type, &mf_media_type_audio)) &&
        SUCCEEDED(type->lpVtbl->SetGUID(type, &mf_mt_subtype, &mf_audio_format_float)) &&
        SUCCEEDED(reader->lpVtbl->SetStreamSelection(reader, MF_STREAM_ALL, FALSE)) &&
        SUCCEEDED(reader->lpVtbl->SetStreamSelection(reader, MF_STREAM_FIRST_AUDIO, TRUE)) &&
        SUCCEEDED(reader->lpVtbl->SetCurrentMediaType(reader, MF_STREAM_FIRST_AUDIO, 0, type));
    mf_release(type);
    if (!configured) {
        mf_release(reader);
        return 0;
    }

    MfMediaType *actual = 0;
    u32 channels = 0;
    u32 sample_rate = 0;
    if (SUCCEEDED(reader->lpVtbl->GetCurrentMediaType(reader, MF_STREAM_FIRST_AUDIO, &actual)) &&
        actual) {
        actual->lpVtbl->GetUINT32(actual, &mf_mt_audio_num_channels, &channels);
        actual->lpVtbl->GetUINT32(actual, &mf_mt_audio_samples_per_second, &sample_rate);
        mf_release(actual);
    }
    if (channels == 0 || sample_rate == 0) {
        mf_release(reader);
        return 0;
    }

    u64 total_frames = 0;
    MfPropVariant duration;
    mem_zero(&duration, sizeof(duration));
    if (SUCCEEDED(reader->lpVtbl->GetPresentationAttribute(reader, MF_STREAM_MEDIASOURCE,
                                                           &mf_pd_duration, &duration)) &&
        (duration.vt == MF_VT_UI8 || duration.vt == MF_VT_I8)) {
        // 100 ns units. No PropVariantClear: an integer owns nothing.
        total_frames = (duration.value.as_u64 * sample_rate) / 10000000ull;
    }

    Win32MediaDecoder *decoder = (Win32MediaDecoder *)os_memory_reserve(sizeof(*decoder));
    if (!decoder || !os_memory_commit(decoder, sizeof(*decoder))) {
        mf_release(reader);
        return 0;
    }
    mem_zero(decoder, sizeof(*decoder));
    decoder->reader = reader;
    decoder->channels = channels;
    decoder->sample_rate = sample_rate;

    info->channels = channels;
    info->sample_rate = sample_rate;
    info->total_frames = total_frames;
    out->v = decoder;
    return 1;
}

u64 os_media_decoder_read(OsMediaDecoder handle, f32 *dst, u64 frames) {
    Win32MediaDecoder *decoder = (Win32MediaDecoder *)handle.v;
    u64 produced = 0;
    while (produced < frames) {
        if (decoder->frames_taken == decoder->frames_held) {
            win32_media_drop_sample(decoder);
            if (decoder->eos) { break; }
            DWORD flags = 0;
            i64 timestamp = 0;
            MfSample *sample = 0;
            if (FAILED(decoder->reader->lpVtbl->ReadSample(decoder->reader, MF_STREAM_FIRST_AUDIO,
                                                           0, 0, &flags, &timestamp, &sample))) {
                break;
            }
            if (flags & MF_READERF_ENDOFSTREAM) { decoder->eos = 1; }
            if (!sample) {
                if (decoder->eos) { break; }
                continue;  // a gap or a format change, keep pulling
            }
            MfMediaBuffer *buffer = 0;
            if (FAILED(sample->lpVtbl->ConvertToContiguousBuffer(sample, &buffer)) || !buffer) {
                mf_release(sample);
                break;
            }
            u8 *data = 0;
            DWORD length = 0;
            if (FAILED(buffer->lpVtbl->Lock(buffer, &data, 0, &length))) {
                mf_release(buffer);
                mf_release(sample);
                break;
            }
            decoder->sample = sample;
            decoder->buffer = buffer;
            decoder->locked = data;
            decoder->frames_held = length / (sizeof(f32) * decoder->channels);
            decoder->frames_taken = 0;
            if (decoder->frames_held == 0) { continue; }
        }
        u64 take = decoder->frames_held - decoder->frames_taken;
        if (take > frames - produced) { take = frames - produced; }
        u64 sample_bytes = sizeof(f32) * decoder->channels;
        mem_copy(dst + produced * decoder->channels,
                 decoder->locked + decoder->frames_taken * sample_bytes, take * sample_bytes);
        decoder->frames_taken += take;
        produced += take;
    }
    return produced;
}

b32 os_media_decoder_seek(OsMediaDecoder handle, u64 frame) {
    Win32MediaDecoder *decoder = (Win32MediaDecoder *)handle.v;
    win32_media_drop_sample(decoder);
    decoder->eos = 0;
    MfPropVariant position;
    mem_zero(&position, sizeof(position));
    position.vt = MF_VT_I8;
    position.value.as_i64 = (i64)((frame * 10000000ull) / decoder->sample_rate);
    if (FAILED(decoder->reader->lpVtbl->Flush(decoder->reader, MF_STREAM_FIRST_AUDIO))) {
        return 0;
    }
    return SUCCEEDED(
        decoder->reader->lpVtbl->SetCurrentPosition(decoder->reader, &mf_guid_null, &position));
}

void os_media_decoder_close(OsMediaDecoder handle) {
    Win32MediaDecoder *decoder = (Win32MediaDecoder *)handle.v;
    if (!decoder) { return; }
    win32_media_drop_sample(decoder);
    mf_release(decoder->reader);
    os_memory_release(decoder, sizeof(*decoder));
}
