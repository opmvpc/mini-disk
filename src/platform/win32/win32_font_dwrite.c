// win32_font_dwrite.c - the DirectWrite rasterizer behind the font part of
// platform.h (ADR-006). dwrite.dll is loaded by hand so the import table stays
// kernel32 + user32, and the SDK's dwrite.h is C++ only, so the handful of
// interfaces we need are declared here as plain vtables. Methods we never call
// are kept as opaque slots: the *order* is the ABI, the signatures are not.
#include <windows.h>

#include "../platform.h"

// --- DirectWrite, declared by hand -----------------------------------------

typedef struct IDWriteFactory IDWriteFactory;
typedef struct IDWriteFactory2 IDWriteFactory2;
typedef struct IDWriteFontCollection IDWriteFontCollection;
typedef struct IDWriteFontFamily IDWriteFontFamily;
typedef struct IDWriteFont IDWriteFont;
typedef struct IDWriteFontFace IDWriteFontFace;
typedef struct IDWriteGlyphRunAnalysis IDWriteGlyphRunAnalysis;
typedef struct IDWriteFontFallback IDWriteFontFallback;
typedef struct IDWriteTextAnalysisSource IDWriteTextAnalysisSource;
typedef struct IDWriteNumberSubstitution IDWriteNumberSubstitution;

typedef struct DWRITE_FONT_METRICS {
    UINT16 design_units_per_em;
    UINT16 ascent;
    UINT16 descent;
    INT16 line_gap;
    UINT16 cap_height;
    UINT16 x_height;
    INT16 underline_position;
    UINT16 underline_thickness;
    INT16 strikethrough_position;
    UINT16 strikethrough_thickness;
} DWRITE_FONT_METRICS;

typedef struct DWRITE_GLYPH_METRICS {
    INT32 left_side_bearing;
    UINT32 advance_width;
    INT32 right_side_bearing;
    INT32 top_side_bearing;
    UINT32 advance_height;
    INT32 bottom_side_bearing;
    INT32 vertical_origin_y;
} DWRITE_GLYPH_METRICS;

typedef struct DWRITE_GLYPH_OFFSET {
    FLOAT advance_offset;
    FLOAT ascender_offset;
} DWRITE_GLYPH_OFFSET;

typedef struct DWRITE_GLYPH_RUN {
    IDWriteFontFace *font_face;
    FLOAT font_em_size;
    UINT32 glyph_count;
    const UINT16 *glyph_indices;
    const FLOAT *glyph_advances;
    const DWRITE_GLYPH_OFFSET *glyph_offsets;
    BOOL is_sideways;
    UINT32 bidi_level;
} DWRITE_GLYPH_RUN;

#define DWRITE_FACTORY_TYPE_SHARED 0
#define DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC 5
#define DWRITE_MEASURING_MODE_NATURAL 0
#define DWRITE_TEXTURE_ALIASED_1x1 0
#define DWRITE_TEXTURE_CLEARTYPE_3x1 1
#define DWRITE_FONT_STYLE_NORMAL 0
#define DWRITE_FONT_STYLE_ITALIC 2
#define DWRITE_FONT_STRETCH_NORMAL 5
#define DWRITE_READING_DIRECTION_LEFT_TO_RIGHT 0

// Every DirectWrite interface opens with IUnknown's three slots. We only ever
// call two of them, and always on an interface we are about to forget, so one
// erased type carries them for all of them.
typedef struct DwUnknown DwUnknown;
typedef struct DwUnknownVtbl {
    HRESULT(WINAPI *QueryInterface)(DwUnknown *self, const GUID *iid, void **out);
    ULONG(WINAPI *AddRef)(DwUnknown *self);
    ULONG(WINAPI *Release)(DwUnknown *self);
} DwUnknownVtbl;
struct DwUnknown { DwUnknownVtbl *lpVtbl; };

typedef struct IDWriteFactoryVtbl {
    void *slots_unknown[3];  // IUnknown
    HRESULT(WINAPI *GetSystemFontCollection)
    (IDWriteFactory *self, IDWriteFontCollection **out, BOOL check_for_updates);
    void *slots_1[19];  // CreateCustomFontCollection .. CreateNumberSubstitution
    HRESULT(WINAPI *CreateGlyphRunAnalysis)
    (IDWriteFactory *self, const DWRITE_GLYPH_RUN *run, FLOAT pixels_per_dip,
     const void *transform, UINT32 rendering_mode, UINT32 measuring_mode, FLOAT baseline_x,
     FLOAT baseline_y, IDWriteGlyphRunAnalysis **out);
} IDWriteFactoryVtbl;
struct IDWriteFactory { IDWriteFactoryVtbl *lpVtbl; };

typedef struct IDWriteFactory2Vtbl {
    void *slots_unknown[3];  // IUnknown
    void *slots_factory[21];   // IDWriteFactory
    void *slots_factory1[2];   // IDWriteFactory1
    HRESULT(WINAPI *GetSystemFontFallback)(IDWriteFactory2 *self, IDWriteFontFallback **out);
} IDWriteFactory2Vtbl;
struct IDWriteFactory2 { IDWriteFactory2Vtbl *lpVtbl; };

typedef struct IDWriteFontCollectionVtbl {
    void *slots_unknown[3];  // IUnknown
    UINT32(WINAPI *GetFontFamilyCount)(IDWriteFontCollection *self);
    HRESULT(WINAPI *GetFontFamily)
    (IDWriteFontCollection *self, UINT32 index, IDWriteFontFamily **out);
    HRESULT(WINAPI *FindFamilyName)
    (IDWriteFontCollection *self, const WCHAR *name, UINT32 *index, BOOL *exists);
    void *slots_1[1];  // GetFontFromFontFace
} IDWriteFontCollectionVtbl;
struct IDWriteFontCollection { IDWriteFontCollectionVtbl *lpVtbl; };

typedef struct IDWriteFontFamilyVtbl {
    void *slots_unknown[3];  // IUnknown
    void *slots_list[3];  // IDWriteFontList: GetFontCollection, GetFontCount, GetFont
    void *slots_1[1];     // GetFamilyNames
    HRESULT(WINAPI *GetFirstMatchingFont)
    (IDWriteFontFamily *self, UINT32 weight, UINT32 stretch, UINT32 style, IDWriteFont **out);
} IDWriteFontFamilyVtbl;
struct IDWriteFontFamily { IDWriteFontFamilyVtbl *lpVtbl; };

typedef struct IDWriteFontVtbl {
    void *slots_unknown[3];  // IUnknown
    // Ten of them, GetFontFamily through HasCharacter: nine put CreateFontFace
    // on HasCharacter's slot, which writes through its second argument.
    void *slots_1[10];
    HRESULT(WINAPI *CreateFontFace)(IDWriteFont *self, IDWriteFontFace **out);
} IDWriteFontVtbl;
struct IDWriteFont { IDWriteFontVtbl *lpVtbl; };

typedef struct IDWriteFontFaceVtbl {
    void *slots_unknown[3];  // IUnknown
    void *slots_1[4];  // GetType, GetFiles, GetIndex, GetSimulations
    BOOL(WINAPI *IsSymbolFont)(IDWriteFontFace *self);
    void(WINAPI *GetMetrics)(IDWriteFontFace *self, DWRITE_FONT_METRICS *out);
    UINT16(WINAPI *GetGlyphCount)(IDWriteFontFace *self);
    HRESULT(WINAPI *GetDesignGlyphMetrics)
    (IDWriteFontFace *self, const UINT16 *glyphs, UINT32 count, DWRITE_GLYPH_METRICS *out,
     BOOL is_sideways);
    HRESULT(WINAPI *GetGlyphIndices)
    (IDWriteFontFace *self, const UINT32 *codepoints, UINT32 count, UINT16 *out);
} IDWriteFontFaceVtbl;
struct IDWriteFontFace { IDWriteFontFaceVtbl *lpVtbl; };

typedef struct IDWriteGlyphRunAnalysisVtbl {
    void *slots_unknown[3];  // IUnknown
    HRESULT(WINAPI *GetAlphaTextureBounds)
    (IDWriteGlyphRunAnalysis *self, UINT32 texture_type, RECT *out);
    HRESULT(WINAPI *CreateAlphaTexture)
    (IDWriteGlyphRunAnalysis *self, UINT32 texture_type, const RECT *bounds, BYTE *values,
     UINT32 size);
} IDWriteGlyphRunAnalysisVtbl;
struct IDWriteGlyphRunAnalysis { IDWriteGlyphRunAnalysisVtbl *lpVtbl; };

typedef struct IDWriteFontFallbackVtbl {
    void *slots_unknown[3];  // IUnknown
    HRESULT(WINAPI *MapCharacters)
    (IDWriteFontFallback *self, IDWriteTextAnalysisSource *source, UINT32 text_position,
     UINT32 text_length, IDWriteFontCollection *base_collection, const WCHAR *base_family,
     UINT32 base_weight, UINT32 base_style, UINT32 base_stretch, UINT32 *mapped_length,
     IDWriteFont **mapped_font, FLOAT *scale);
} IDWriteFontFallbackVtbl;
struct IDWriteFontFallback { IDWriteFontFallbackVtbl *lpVtbl; };

// The one interface we implement instead of calling: MapCharacters reads the
// text through it.
typedef struct IDWriteTextAnalysisSourceVtbl {
    HRESULT(WINAPI *QueryInterface)
    (IDWriteTextAnalysisSource *self, const GUID *iid, void **out);
    ULONG(WINAPI *AddRef)(IDWriteTextAnalysisSource *self);
    ULONG(WINAPI *Release)(IDWriteTextAnalysisSource *self);
    HRESULT(WINAPI *GetTextAtPosition)
    (IDWriteTextAnalysisSource *self, UINT32 position, const WCHAR **text, UINT32 *length);
    HRESULT(WINAPI *GetTextBeforePosition)
    (IDWriteTextAnalysisSource *self, UINT32 position, const WCHAR **text, UINT32 *length);
    UINT32(WINAPI *GetParagraphReadingDirection)(IDWriteTextAnalysisSource *self);
    HRESULT(WINAPI *GetLocaleName)
    (IDWriteTextAnalysisSource *self, UINT32 position, UINT32 *length, const WCHAR **locale);
    HRESULT(WINAPI *GetNumberSubstitution)
    (IDWriteTextAnalysisSource *self, UINT32 position, UINT32 *length,
     IDWriteNumberSubstitution **out);
} IDWriteTextAnalysisSourceVtbl;
struct IDWriteTextAnalysisSource { IDWriteTextAnalysisSourceVtbl *lpVtbl; };

#define DW(obj, method) (obj)->lpVtbl->method
#define DW_RELEASE(obj)                              \
    do {                                             \
        if (obj) {                                   \
            DwUnknown *unknown = (DwUnknown *)(obj); \
            unknown->lpVtbl->Release(unknown);       \
            (obj) = 0;                               \
        }                                            \
    } while (0)

typedef HRESULT(WINAPI *Win32DWriteCreateFactory)(UINT32 type, const GUID *iid, void **factory);

// --- our font table --------------------------------------------------------

#define WIN32_FONT_MAX 32          // 4 UI styles x (base + fallbacks), twice over
#define WIN32_FONT_MAP_SIZE 512    // codepoint -> (glyph, font), power of two
#define WIN32_FONT_FAMILY_MAX 64   // UTF-16 units, terminator included

typedef struct Win32GlyphMapEntry {
    u32 codepoint;  // 0 : empty slot
    u16 glyph;
    u16 font_id;
} Win32GlyphMapEntry;

typedef struct Win32Font {
    IDWriteFontFace *face;
    f32 size_px;
    u32 weight;
    f32 units_per_em;
    WCHAR family[WIN32_FONT_FAMILY_MAX];  // null terminated, MapCharacters wants it
    OsFontMetrics metrics;
    Win32GlyphMapEntry map[WIN32_FONT_MAP_SIZE];
} Win32Font;

typedef struct Win32FontState {
    HMODULE dwrite;
    IDWriteFactory *factory;
    IDWriteFactory2 *factory2;
    IDWriteFontCollection *collection;
    IDWriteFontFallback *fallback;
    IDWriteTextAnalysisSource analysis_source;  // stateless except for the text

    // MapCharacters reads the text through analysis_source; one codepoint at a
    // time is all we ask for, so the buffer is a surrogate pair at most.
    WCHAR map_text[2];
    UINT32 map_text_length;

    Win32Font fonts[WIN32_FONT_MAX];
    u32 font_count;  // fonts[0] is the "no font" slot, never used
} Win32FontState;

global Win32FontState win32_font;

// Three bytes per pixel in CLEARTYPE_3x1, collapsed to one on the way out.
global u8 win32_font_cleartype[OS_GLYPH_MAX_DIM * OS_GLYPH_MAX_DIM * 3];

static Win32Font *win32_font_get(OsFont font) {
    Assert(font.v > 0 && font.v < win32_font.font_count);
    return &win32_font.fonts[font.v];
}

// --- IDWriteTextAnalysisSource ---------------------------------------------
// A singleton with no reference count: it never outlives the MapCharacters call
// that borrows it, and DirectWrite never keeps it.

// Only the two interfaces we really are: answering yes to, say,
// IDWriteTextAnalysisSource1 would have DirectWrite call past the end of our
// vtable, which is a jump into whatever follows it in .data.
static HRESULT WINAPI win32_tas_query_interface(IDWriteTextAnalysisSource *self, const GUID *iid,
                                                void **out) {
    static const GUID iid_unknown = {
            0x00000000, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
    static const GUID iid_source = {
            0x688e1a58, 0x5094, 0x47c8, {0xad, 0xc8, 0xfb, 0xce, 0xa6, 0x0a, 0xe9, 0x2b}};
    if (mem_cmp(iid, &iid_unknown, sizeof(GUID)) == 0 ||
        mem_cmp(iid, &iid_source, sizeof(GUID)) == 0) {
        *out = self;
        return S_OK;
    }
    *out = 0;
    return E_NOINTERFACE;
}
static ULONG WINAPI win32_tas_add_ref(IDWriteTextAnalysisSource *self) {
    Unused(self);
    return 1;
}
static ULONG WINAPI win32_tas_release(IDWriteTextAnalysisSource *self) {
    Unused(self);
    return 1;
}
static HRESULT WINAPI win32_tas_get_text_at_position(IDWriteTextAnalysisSource *self,
                                                     UINT32 position, const WCHAR **text,
                                                     UINT32 *length) {
    Unused(self);
    if (position >= win32_font.map_text_length) {
        *text = 0;
        *length = 0;
        return S_OK;
    }
    *text = win32_font.map_text + position;
    *length = win32_font.map_text_length - position;
    return S_OK;
}
static HRESULT WINAPI win32_tas_get_text_before_position(IDWriteTextAnalysisSource *self,
                                                         UINT32 position, const WCHAR **text,
                                                         UINT32 *length) {
    Unused(self);
    Unused(position);
    *text = 0;
    *length = 0;
    return S_OK;
}
static UINT32 WINAPI win32_tas_get_reading_direction(IDWriteTextAnalysisSource *self) {
    Unused(self);
    return DWRITE_READING_DIRECTION_LEFT_TO_RIGHT;
}
static HRESULT WINAPI win32_tas_get_locale_name(IDWriteTextAnalysisSource *self, UINT32 position,
                                                UINT32 *length, const WCHAR **locale) {
    Unused(self);
    *length = win32_font.map_text_length - position;
    *locale = 0;  // no locale: Han unification is disambiguated by the system default
    return S_OK;
}
static HRESULT WINAPI win32_tas_get_number_substitution(IDWriteTextAnalysisSource *self,
                                                        UINT32 position, UINT32 *length,
                                                        IDWriteNumberSubstitution **out) {
    Unused(self);
    *length = win32_font.map_text_length - position;
    *out = 0;
    return S_OK;
}

global IDWriteTextAnalysisSourceVtbl win32_tas_vtbl = {
        win32_tas_query_interface,
        win32_tas_add_ref,
        win32_tas_release,
        win32_tas_get_text_at_position,
        win32_tas_get_text_before_position,
        win32_tas_get_reading_direction,
        win32_tas_get_locale_name,
        win32_tas_get_number_substitution,
};

// --- init ------------------------------------------------------------------

b32 os_font_init(void) {
    static const GUID iid_factory = {
            0xb859ee5a, 0xd838, 0x4b5b, {0xa2, 0xe8, 0x1a, 0xdc, 0x7d, 0x93, 0xdb, 0x48}};
    static const GUID iid_factory2 = {
            0x0439fc60, 0xca44, 0x4994, {0x8d, 0xee, 0x3a, 0x9a, 0xf7, 0xb7, 0x32, 0xec}};

    win32_font.dwrite = LoadLibraryW(L"dwrite.dll");
    if (!win32_font.dwrite) { return 0; }
    void *entry = (void *)GetProcAddress(win32_font.dwrite, "DWriteCreateFactory");
    if (!entry) { return 0; }
    Win32DWriteCreateFactory create = (Win32DWriteCreateFactory)entry;
    if (FAILED(create(DWRITE_FACTORY_TYPE_SHARED, &iid_factory, (void **)&win32_font.factory))) {
        return 0;
    }
    IDWriteFactory *factory = win32_font.factory;
    if (FAILED(DW(factory, GetSystemFontCollection)(factory, &win32_font.collection, FALSE))) {
        return 0;
    }
    // IDWriteFactory2 is Windows 8.1 and up; without it there is no system
    // fallback and Japanese would be tofu, so it is a hard requirement.
    DwUnknown *unknown = (DwUnknown *)factory;
    if (FAILED(unknown->lpVtbl->QueryInterface(unknown, &iid_factory2,
                                               (void **)&win32_font.factory2))) {
        return 0;
    }
    if (FAILED(DW(win32_font.factory2, GetSystemFontFallback)(win32_font.factory2,
                                                              &win32_font.fallback))) {
        return 0;
    }
    win32_font.analysis_source.lpVtbl = &win32_tas_vtbl;
    win32_font.font_count = 1;  // slot 0 stands for "no font"
    return 1;
}

void os_font_close_all(void) {
    for (u32 i = 1; i < win32_font.font_count; i += 1) { DW_RELEASE(win32_font.fonts[i].face); }
    win32_font.font_count = 1;
}

void os_font_shutdown(void) {
    os_font_close_all();
    DW_RELEASE(win32_font.fallback);
    DW_RELEASE(win32_font.factory2);
    DW_RELEASE(win32_font.collection);
    DW_RELEASE(win32_font.factory);
    if (win32_font.dwrite) {
        FreeLibrary(win32_font.dwrite);
        win32_font.dwrite = 0;
    }
}

// --- opening a face --------------------------------------------------------

static void win32_font_fill_metrics(Win32Font *font) {
    DWRITE_FONT_METRICS metrics;
    DW(font->face, GetMetrics)(font->face, &metrics);
    f32 scale = font->size_px / (f32)metrics.design_units_per_em;
    font->units_per_em = (f32)metrics.design_units_per_em;
    font->metrics.ascent = (f32)metrics.ascent * scale;
    font->metrics.descent = (f32)metrics.descent * scale;
    font->metrics.line_gap = (f32)metrics.line_gap * scale;
    font->metrics.x_height = (f32)metrics.x_height * scale;
    font->metrics.cap_height = (f32)metrics.cap_height * scale;

    // Tabular figures the cheap way (research/03 s5.8 option B): the widest of
    // the ten digits is the cell every digit is centred in. No shaping engine,
    // no `tnum` lookup, and it works with any font.
    UINT32 codepoints[10];
    UINT16 glyphs[10];
    for (u32 i = 0; i < 10; i += 1) { codepoints[i] = (UINT32)('0' + i); }
    DWRITE_GLYPH_METRICS glyph_metrics[10];
    f32 widest = 0.0f;
    if (SUCCEEDED(DW(font->face, GetGlyphIndices)(font->face, codepoints, 10, glyphs)) &&
        SUCCEEDED(DW(font->face, GetDesignGlyphMetrics)(font->face, glyphs, 10, glyph_metrics,
                                                        FALSE))) {
        for (u32 i = 0; i < 10; i += 1) {
            widest = max_f32(widest, (f32)glyph_metrics[i].advance_width * scale);
        }
    }
    font->metrics.digit_advance = widest;
}

// Adds a face to the table, or returns the entry that already holds it: the
// same fallback face comes back for every codepoint of a Japanese run.
static OsFont win32_font_add(IDWriteFontFace *face, f32 size_px, u32 weight, const WCHAR *family) {
    OsFont result;
    result.v = 0;
    for (u32 i = 1; i < win32_font.font_count; i += 1) {
        Win32Font *existing = &win32_font.fonts[i];
        if (existing->face == face && existing->size_px == size_px) {
            DW_RELEASE(face);  // the table already owns a reference
            result.v = i;
            return result;
        }
    }
    AssertAlways(win32_font.font_count < WIN32_FONT_MAX);
    u32 index = win32_font.font_count;
    win32_font.font_count += 1;
    Win32Font *font = &win32_font.fonts[index];
    mem_zero(font, sizeof(*font));
    font->face = face;
    font->size_px = size_px;
    font->weight = weight;
    u32 i = 0;
    while (family && family[i] && i + 1 < WIN32_FONT_FAMILY_MAX) {
        font->family[i] = family[i];
        i += 1;
    }
    font->family[i] = 0;
    win32_font_fill_metrics(font);
    result.v = index;
    return result;
}

OsFont os_font_open(String8 family, f32 size_px, u32 weight, b32 italic) {
    OsFont result;
    result.v = 0;
    if (!win32_font.factory) { return result; }

    ArenaTemp scratch = scratch_begin(0, 0);
    String16 name = str16_from_str8(scratch.arena, family);
    UINT32 index = 0;
    BOOL exists = FALSE;
    IDWriteFontCollection *collection = win32_font.collection;
    HRESULT hr = DW(collection, FindFamilyName)(collection, (const WCHAR *)name.str, &index,
                                                &exists);
    if (FAILED(hr) || !exists) {
        scratch_end(scratch);
        return result;  // the caller falls back to the next family it knows
    }

    IDWriteFontFamily *font_family = 0;
    IDWriteFont *font = 0;
    IDWriteFontFace *face = 0;
    if (SUCCEEDED(DW(collection, GetFontFamily)(collection, index, &font_family)) &&
        SUCCEEDED(DW(font_family, GetFirstMatchingFont)(font_family, weight,
                                                        DWRITE_FONT_STRETCH_NORMAL,
                                                        italic ? DWRITE_FONT_STYLE_ITALIC
                                                               : DWRITE_FONT_STYLE_NORMAL,
                                                        &font)) &&
        SUCCEEDED(DW(font, CreateFontFace)(font, &face))) {
        result = win32_font_add(face, size_px, weight, (const WCHAR *)name.str);
    }
    DW_RELEASE(font);
    DW_RELEASE(font_family);
    scratch_end(scratch);
    return result;
}

u32 os_font_id(OsFont font) { return (u32)font.v; }

OsFontMetrics os_font_metrics(OsFont font) { return win32_font_get(font)->metrics; }

// --- glyph lookup and fallback ---------------------------------------------

static u16 win32_font_glyph_of(Win32Font *font, u32 codepoint) {
    UINT32 cp = codepoint;
    UINT16 glyph = 0;
    DW(font->face, GetGlyphIndices)(font->face, &cp, 1, &glyph);
    return glyph;
}

// The system fallback (IDWriteFontFallback, factory 2). Per codepoint rather
// than per run: the result is cached in the base font's map, so the COM call
// happens once per new character in the whole session.
static OsFont win32_font_fallback_for(Win32Font *base, u32 codepoint) {
    OsFont result;
    result.v = 0;
    win32_font.map_text_length = (UINT32)utf16_encode((u16 *)win32_font.map_text, codepoint);

    IDWriteFont *mapped = 0;
    UINT32 mapped_length = 0;
    FLOAT scale = 1.0f;
    IDWriteFontFallback *fallback = win32_font.fallback;
    HRESULT hr = DW(fallback, MapCharacters)(fallback, &win32_font.analysis_source, 0,
                                             win32_font.map_text_length, win32_font.collection,
                                             base->family, base->weight, DWRITE_FONT_STYLE_NORMAL,
                                             DWRITE_FONT_STRETCH_NORMAL, &mapped_length, &mapped,
                                             &scale);
    if (FAILED(hr) || !mapped) { return result; }

    IDWriteFontFace *face = 0;
    if (SUCCEEDED(DW(mapped, CreateFontFace)(mapped, &face))) {
        result = win32_font_add(face, base->size_px * scale, base->weight, 0);
    }
    DW_RELEASE(mapped);
    return result;
}

u32 os_font_glyph_index(OsFont font, u32 codepoint, OsFont *out_font) {
    Win32Font *base = win32_font_get(font);
    u32 slot = (u32)(hash64_mix(codepoint) & (WIN32_FONT_MAP_SIZE - 1));
    while (base->map[slot].codepoint != 0 && base->map[slot].codepoint != codepoint) {
        slot = (slot + 1) & (WIN32_FONT_MAP_SIZE - 1);
    }
    if (base->map[slot].codepoint == codepoint) {
        out_font->v = base->map[slot].font_id;
        return base->map[slot].glyph;
    }

    OsFont carrier = font;
    u16 glyph = win32_font_glyph_of(base, codepoint);
    if (glyph == 0) {
        OsFont mapped = win32_font_fallback_for(base, codepoint);
        if (mapped.v != 0) {
            glyph = win32_font_glyph_of(win32_font_get(mapped), codepoint);
            if (glyph != 0) { carrier = mapped; }
        }
    }
    base->map[slot].codepoint = codepoint;
    base->map[slot].glyph = glyph;
    base->map[slot].font_id = (u16)carrier.v;
    *out_font = carrier;
    return glyph;
}

f32 os_font_advance(OsFont font, u32 glyph) {
    Win32Font *entry = win32_font_get(font);
    UINT16 index = (UINT16)glyph;
    DWRITE_GLYPH_METRICS metrics;
    if (FAILED(DW(entry->face, GetDesignGlyphMetrics)(entry->face, &index, 1, &metrics, FALSE))) {
        return 0.0f;
    }
    return (f32)metrics.advance_width * entry->size_px / entry->units_per_em;
}

// No kerning in v1 (research/03 s5.9): Segoe UI has nothing visible at 13 px.
// The seam exists so adding GetKerningPairAdjustments later changes one file.
f32 os_font_kern(OsFont font, u32 left_glyph, u32 right_glyph) {
    Unused(font);
    Unused(left_glyph);
    Unused(right_glyph);
    return 0.0f;
}

// --- rasterization ---------------------------------------------------------

// CLEARTYPE_3x1 collapsed to gray rather than ALIASED_1x1: GetAlphaTextureBounds
// returns an empty rect for ALIASED_1x1 unless the rendering mode is ALIASED,
// which is hard aliased text. Every antialiased mode only fills the 3x1 texture,
// so the grayscale path everyone ships (Skia, WebRender) averages the three
// subpixel samples. Same one byte per pixel in the atlas, no colour fringing.
b32 os_font_rasterize(OsFont font, u32 glyph, f32 subpixel_x, u8 *out, u64 out_capacity,
                      OsGlyphMetrics *out_metrics) {
    Win32Font *entry = win32_font_get(font);
    mem_zero(out_metrics, sizeof(*out_metrics));
    out_metrics->advance = os_font_advance(font, glyph);

    UINT16 index = (UINT16)glyph;
    FLOAT advance = 0.0f;
    DWRITE_GLYPH_RUN run;
    mem_zero(&run, sizeof(run));
    run.font_face = entry->face;
    run.font_em_size = entry->size_px;
    run.glyph_count = 1;
    run.glyph_indices = &index;
    run.glyph_advances = &advance;

    IDWriteGlyphRunAnalysis *analysis = 0;
    IDWriteFactory *factory = win32_font.factory;
    HRESULT hr = DW(factory, CreateGlyphRunAnalysis)(factory, &run, 1.0f, 0,
                                                     DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC,
                                                     DWRITE_MEASURING_MODE_NATURAL, subpixel_x,
                                                     0.0f, &analysis);
    if (FAILED(hr)) { return 0; }

    RECT bounds;
    b32 has_ink = 0;
    if (SUCCEEDED(DW(analysis, GetAlphaTextureBounds)(analysis, DWRITE_TEXTURE_CLEARTYPE_3x1,
                                                      &bounds))) {
        u32 width = (u32)(bounds.right - bounds.left);
        u32 height = (u32)(bounds.bottom - bounds.top);
        if (width > 0 && height > 0) {
            AssertAlways(width <= OS_GLYPH_MAX_DIM && height <= OS_GLYPH_MAX_DIM);
            AssertAlways((u64)width * height <= out_capacity);
            UINT32 bytes = width * height * 3;
            if (SUCCEEDED(DW(analysis, CreateAlphaTexture)(analysis, DWRITE_TEXTURE_CLEARTYPE_3x1,
                                                           &bounds, win32_font_cleartype, bytes))) {
                const u8 *src = win32_font_cleartype;
                for (u64 i = 0, count = (u64)width * height; i < count; i += 1) {
                    u32 sum = (u32)src[0] + src[1] + src[2];
                    out[i] = (u8)((sum + 1) / 3);
                    src += 3;
                }
                out_metrics->offset_x = bounds.left;
                out_metrics->offset_y = bounds.top;
                out_metrics->width = width;
                out_metrics->height = height;
                has_ink = 1;
            }
        }
    }
    DW_RELEASE(analysis);
    return has_ink;
}
