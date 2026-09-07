// r_thumbs.c - see r_thumbs.h. A grid of cells, an open addressed map from
// cover hash to cell, and one intrusive LRU list per cell size.
//
// The whole page is 16 MB of RGBA8 in the arena, uploaded by dirty region like
// the glyph atlas. 1400 small cells hold every row a scroll can show a hundred
// times over, so eviction only ever happens on a library nobody is looking at.
#include "r_thumbs.h"

#include "r_backend.h"

#define R_THUMBS_MAP_SLOTS 4096  // power of two, > 2x R_THUMB_SLOT_COUNT
#define R_THUMBS_NONE      U16_MAX

typedef struct R_ThumbSlot {
    u64 key;      // cover hash, 0 when the cell is free
    u16 x, y;     // top left of the pixels, padding excluded
    u16 lru_prev, lru_next;  // R_THUMBS_NONE at the ends
    u8 band;      // 0: small, 1: large
    u8 live;
    u8 linked;    // in the band's LRU list: a fresh cell is not, yet
} R_ThumbSlot;

typedef struct R_ThumbBand {
    u16 first, count;   // slots[first .. first + count)
    u16 lru_head;       // least recently used
    u16 lru_tail;       // most recently used
    u16 used;
} R_ThumbBand;

typedef struct R_Thumbs {
    Arena *arena;
    u8 *pixels;  // R_THUMBS_SIZE^2 RGBA8
    u32 texture;
    R_ThumbSlot slots[R_THUMB_SLOT_COUNT];
    R_ThumbBand bands[2];
    u16 map[R_THUMBS_MAP_SLOTS];  // key -> slot + 1, 0 empty
    u64 evictions;
    u32 dirty_x0, dirty_y0, dirty_x1, dirty_y1;  // empty when x1 == 0
} R_Thumbs;

global R_Thumbs r_thumbs;

md_inline u32 r_thumbs_band_of(u32 size) {
    Assert(size == R_THUMB_SMALL || size == R_THUMB_LARGE);
    return size == R_THUMB_LARGE ? 1u : 0u;
}

md_inline u32 r_thumbs_cell_of(u32 band) {
    return band ? (u32)R_THUMB_LARGE_CELL : (u32)R_THUMB_SMALL_CELL;
}

static void r_thumbs_dirty_add(u32 x0, u32 y0, u32 x1, u32 y1) {
    if (r_thumbs.dirty_x1 == 0) {
        r_thumbs.dirty_x0 = x0;
        r_thumbs.dirty_y0 = y0;
        r_thumbs.dirty_x1 = x1;
        r_thumbs.dirty_y1 = y1;
        return;
    }
    r_thumbs.dirty_x0 = Min(r_thumbs.dirty_x0, x0);
    r_thumbs.dirty_y0 = Min(r_thumbs.dirty_y0, y0);
    r_thumbs.dirty_x1 = Max(r_thumbs.dirty_x1, x1);
    r_thumbs.dirty_y1 = Max(r_thumbs.dirty_y1, y1);
}

// --- the map ---------------------------------------------------------------
// Open addressing, linear probing, no deletion: an evicted key is rewritten by
// the slot that takes its place, and a lookup that lands on a slot whose key
// changed simply misses. That is why the slot carries the key too.
md_inline u32 r_thumbs_map_index(u64 key, u32 band) {
    u64 mixed = hash64_mix(key ^ ((u64)band << 63));
    return (u32)(mixed & (R_THUMBS_MAP_SLOTS - 1));
}

static u16 r_thumbs_map_find(u64 key, u32 band) {
    u32 index = r_thumbs_map_index(key, band);
    for (u32 probe = 0; probe < R_THUMBS_MAP_SLOTS; probe += 1) {
        u16 entry = r_thumbs.map[index];
        if (entry == 0) { return R_THUMBS_NONE; }
        R_ThumbSlot *slot = &r_thumbs.slots[entry - 1];
        if (slot->live && slot->key == key && slot->band == band) { return (u16)(entry - 1); }
        index = (index + 1) & (R_THUMBS_MAP_SLOTS - 1);
    }
    return R_THUMBS_NONE;
}

// Eviction leaves the old key behind in the map, so the map is rebuilt from
// the live cells rather than deleted from: no tombstones, no probe chains that
// only grow, and it costs one pass over 1400 cells on a rare event.
static void r_thumbs_map_rebuild(void);

static void r_thumbs_map_insert(u64 key, u32 band, u16 slot_index) {
    u32 index = r_thumbs_map_index(key, band);
    for (u32 probe = 0; probe < R_THUMBS_MAP_SLOTS; probe += 1) {
        u16 entry = r_thumbs.map[index];
        if (entry == 0 || !r_thumbs.slots[entry - 1].live) {
            r_thumbs.map[index] = (u16)(slot_index + 1);
            return;
        }
        index = (index + 1) & (R_THUMBS_MAP_SLOTS - 1);
    }
    Assert(!"thumbnail map is full, which the slot count forbids");
}

static void r_thumbs_map_rebuild(void) {
    mem_zero(r_thumbs.map, sizeof(r_thumbs.map));
    for (u32 i = 0; i < R_THUMB_SLOT_COUNT; i += 1) {
        R_ThumbSlot *slot = &r_thumbs.slots[i];
        if (slot->live) { r_thumbs_map_insert(slot->key, slot->band, (u16)i); }
    }
}

// --- the LRU list ----------------------------------------------------------
// Unlinking a cell that is not in the list would clear the head, which is how
// the list ends up holding one entry and the LRU evicts the newest cell.
static void r_thumbs_lru_unlink(R_ThumbBand *band, u16 index) {
    R_ThumbSlot *slot = &r_thumbs.slots[index];
    if (!slot->linked) { return; }
    if (slot->lru_prev != R_THUMBS_NONE) {
        r_thumbs.slots[slot->lru_prev].lru_next = slot->lru_next;
    } else {
        band->lru_head = slot->lru_next;
    }
    if (slot->lru_next != R_THUMBS_NONE) {
        r_thumbs.slots[slot->lru_next].lru_prev = slot->lru_prev;
    } else {
        band->lru_tail = slot->lru_prev;
    }
    slot->lru_prev = R_THUMBS_NONE;
    slot->lru_next = R_THUMBS_NONE;
    slot->linked = 0;
}

static void r_thumbs_lru_touch(R_ThumbBand *band, u16 index) {
    if (band->lru_tail == index && r_thumbs.slots[index].linked) { return; }
    r_thumbs_lru_unlink(band, index);
    R_ThumbSlot *slot = &r_thumbs.slots[index];
    slot->lru_prev = band->lru_tail;
    slot->lru_next = R_THUMBS_NONE;
    if (band->lru_tail != R_THUMBS_NONE) { r_thumbs.slots[band->lru_tail].lru_next = index; }
    band->lru_tail = index;
    slot->linked = 1;
    if (band->lru_head == R_THUMBS_NONE) { band->lru_head = index; }
}

// --- the page --------------------------------------------------------------
static void r_thumbs_layout(void) {
    u16 index = 0;
    for (u32 band_index = 0; band_index < 2; band_index += 1) {
        R_ThumbBand *band = &r_thumbs.bands[band_index];
        u32 cell = r_thumbs_cell_of(band_index);
        u32 columns = band_index ? (u32)R_THUMB_LARGE_COLS : (u32)R_THUMB_SMALL_COLS;
        u32 rows = band_index ? 1u : (u32)R_THUMB_SMALL_ROWS;
        u32 top = band_index ? 0u : (u32)R_THUMB_SMALL_TOP;
        band->first = index;
        band->count = (u16)(columns * rows);
        band->lru_head = R_THUMBS_NONE;
        band->lru_tail = R_THUMBS_NONE;
        band->used = 0;
        for (u32 row = 0; row < rows; row += 1) {
            for (u32 column = 0; column < columns; column += 1) {
                R_ThumbSlot *slot = &r_thumbs.slots[index];
                slot->key = 0;
                slot->live = 0;
                slot->linked = 0;
                slot->band = (u8)band_index;
                slot->x = (u16)(column * cell + R_THUMB_PAD);
                slot->y = (u16)(top + row * cell + R_THUMB_PAD);
                slot->lru_prev = R_THUMBS_NONE;
                slot->lru_next = R_THUMBS_NONE;
                index += 1;
            }
        }
    }
    Assert(index == R_THUMB_SLOT_COUNT);
}

void r_thumbs_init(Arena *arena) {
    r_thumbs.arena = arena;
    r_thumbs.pixels = push_array_zero(arena, u8, (u64)R_THUMBS_SIZE * R_THUMBS_SIZE * 4);
    r_thumbs.texture = r_backend_texture_rgba8(R_THUMBS_SIZE);
    r_thumbs.evictions = 0;
    r_thumbs.dirty_x1 = 0;
    mem_zero(r_thumbs.map, sizeof(r_thumbs.map));
    r_thumbs_layout();
}

void r_thumbs_reset(void) {
    mem_zero(r_thumbs.pixels, (u64)R_THUMBS_SIZE * R_THUMBS_SIZE * 4);
    mem_zero(r_thumbs.map, sizeof(r_thumbs.map));
    r_thumbs_layout();
    r_thumbs_dirty_add(0, 0, R_THUMBS_SIZE, R_THUMBS_SIZE);
}

static R_AtlasRect r_thumbs_rect_of(const R_ThumbSlot *slot, u32 size) {
    f32 inv = 1.0f / (f32)R_THUMBS_SIZE;
    R_AtlasRect result;
    result.x = slot->x;
    result.y = slot->y;
    result.width = (u16)size;
    result.height = (u16)size;
    result.uv0 = v2((f32)slot->x * inv, (f32)slot->y * inv);
    result.uv1 = v2((f32)(slot->x + size) * inv, (f32)(slot->y + size) * inv);
    return result;
}

b32 r_thumbs_lookup(u64 key, u32 size, R_AtlasRect *out) {
    u32 band_index = r_thumbs_band_of(size);
    u16 index = r_thumbs_map_find(key, band_index);
    if (index == R_THUMBS_NONE) { return 0; }
    r_thumbs_lru_touch(&r_thumbs.bands[band_index], index);
    *out = r_thumbs_rect_of(&r_thumbs.slots[index], size);
    return 1;
}

R_AtlasRect r_thumbs_add(u64 key, u32 size, const u8 *pixels) {
    Assert(key != 0);
    u32 band_index = r_thumbs_band_of(size);
    R_ThumbBand *band = &r_thumbs.bands[band_index];

    u16 index = r_thumbs_map_find(key, band_index);
    if (index == R_THUMBS_NONE) {
        if (band->used < band->count) {
            index = (u16)(band->first + band->used);
            band->used += 1;
        } else {
            // Full: the head of the list is the cell nobody has drawn for the
            // longest time, and it is the one that goes.
            index = band->lru_head;
            Assert(index != R_THUMBS_NONE);
            r_thumbs_lru_unlink(band, index);
            r_thumbs.slots[index].live = 0;
            r_thumbs.evictions += 1;
            r_thumbs.slots[index].key = key;
            r_thumbs.slots[index].live = 1;
            r_thumbs_map_rebuild();
            r_thumbs_lru_touch(band, index);
            goto placed;
        }
        r_thumbs.slots[index].key = key;
        r_thumbs.slots[index].live = 1;
        r_thumbs_map_insert(key, band_index, index);
    }
    r_thumbs_lru_touch(band, index);

placed:;
    R_ThumbSlot *slot = &r_thumbs.slots[index];
    for (u32 row = 0; row < size; row += 1) {
        u8 *destination = r_thumbs.pixels + (((u64)(slot->y + row) * R_THUMBS_SIZE + slot->x) * 4);
        mem_copy(destination, pixels + (u64)row * size * 4, (u64)size * 4);
    }
    r_thumbs_dirty_add(slot->x, slot->y, (u32)slot->x + size, (u32)slot->y + size);
    return r_thumbs_rect_of(slot, size);
}

void r_thumbs_flush(void) {
    if (r_thumbs.dirty_x1 == 0) { return; }
    r_backend_texture_upload_rgba8(r_thumbs.texture, R_THUMBS_SIZE, r_thumbs.pixels,
                                   r_thumbs.dirty_x0, r_thumbs.dirty_y0,
                                   r_thumbs.dirty_x1 - r_thumbs.dirty_x0,
                                   r_thumbs.dirty_y1 - r_thumbs.dirty_y0);
    r_thumbs.dirty_x1 = 0;
}

u32 r_thumbs_texture(void) { return r_thumbs.texture; }
u32 r_thumbs_count(u32 size) { return r_thumbs.bands[r_thumbs_band_of(size)].used; }
u32 r_thumbs_capacity(u32 size) { return r_thumbs.bands[r_thumbs_band_of(size)].count; }
u64 r_thumbs_evictions(void) { return r_thumbs.evictions; }
