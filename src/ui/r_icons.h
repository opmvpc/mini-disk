// r_icons.h - the eight test icons, described as polygons, rasterized once by
// r_raster and parked in the atlas. No font, no SVG parser, no data file.
#ifndef R_ICONS_H
#define R_ICONS_H

#include "../base/base.h"
#include "../base/base_arena.h"

#include "r_atlas.h"

typedef enum R_Icon {
    R_Icon_Circle = 0,
    R_Icon_Play,
    R_Icon_Cross,
    R_Icon_Check,
    R_Icon_ChevronLeft,
    R_Icon_ChevronRight,
    R_Icon_ChevronDown,
    R_Icon_Disc,
    R_Icon_COUNT
} R_Icon;

// Rasterizes the eight icons at `size` physical pixels and adds them to the
// atlas. Called again after r_atlas_reset on a DPI change.
void r_icons_build(Arena *scratch, u32 size);

R_AtlasRect r_icon_rect(R_Icon icon);

// --- the 45 degree hatch of research/02 s9.3 --------------------------------
// One 8 px period, generated - not loaded: the pattern is arithmetic, and a PNG
// for sixty-four bytes of information would be sixty-four bytes of information
// plus a decoder. It is stored as a tile of eight periods so that a wide zone
// costs a handful of quads instead of one per stripe, and the tile is a
// multiple of the period on both axes, so two neighbouring tiles line up.
#define R_HATCH_PERIOD_PX 8
#define R_HATCH_TILE_PX   64
#define R_HATCH_MAX_TILES 64

// One quad of a hatched area: where it goes, and which part of the tile it
// shows. `uv` are inside the tile, 0..1, so the caller maps them into whatever
// atlas rectangle the tile ended up in.
typedef struct R_HatchTile {
    Rect dst;
    V2 uv0, uv1;
} R_HatchTile;

R_AtlasRect r_hatch_rect(void);

// Pure. Cuts `area` along the tile grid anchored at `origin`, so the stripes
// keep their phase whatever part of the pattern is on screen: two areas that
// share an origin line up, and scrolling one of them does not slide its
// stripes inside it. Returns the number of tiles written, at most `max`.
u32 r_hatch_tiles(Rect area, V2 origin, R_HatchTile *out, u32 max);

#endif // R_ICONS_H
