#include "base_math.h"

Rect rect_intersect(Rect a, Rect b) {
    Rect result;
    result.min = v2(max_f32(a.min.x, b.min.x), max_f32(a.min.y, b.min.y));
    result.max = v2(min_f32(a.max.x, b.max.x), min_f32(a.max.y, b.max.y));
    result.max.x = max_f32(result.max.x, result.min.x);
    result.max.y = max_f32(result.max.y, result.min.y);
    return result;
}
