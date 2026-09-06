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

#endif // R_ICONS_H
