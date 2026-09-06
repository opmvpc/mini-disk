// lib_search.h - incremental search while the user types (T-012, R-02).
//
// Three steps, in this order because each one is cheaper than the one after it:
//   1. refinement - when the query only grew, the candidates are the bits the
//      previous run left set, and nothing else is looked at;
//   2. rejection by mask - one AND per track against the 64 bit letter mask;
//   3. the real compare - case and accent insensitive substring over the
//      normalized "title\1artist\1album" blob the index already built.
//
// Nothing is allocated while searching: the result buffer is sized once, and
// the refinement filters it in place.
#ifndef LIB_SEARCH_H
#define LIB_SEARCH_H

#include "../../base/base.h"
#include "../../base/base_arena.h"
#include "lib_index.h"

#define LIB_QUERY_CAP 128
#define LIB_TOKEN_MAX 8

typedef struct LibSearch {
    u32 *results;      // track ids, in the order of `column`
    u32 result_count;
    u32 capacity;      // track slots the buffers were sized for
    // One bit per slot, the survivors of the last run. It is what the next
    // keystroke refines, and walking it visits the tracks in id order - which
    // is the order the normalized blob is laid out in.
    u64 *matched;
    u32 word_count;

    u8 query[LIB_QUERY_CAP];  // the *normalized* query the results answer
    u32 query_size;
    b32 valid;

    LibSortColumn column;
    b32 descending;
    LibSortColumn emitted_column;  // the order `results` is actually in
    b32 emitted_desc;
    StringId artist_filter;  // LIB_FILTER_ANY: no constraint
    StringId album_filter;

    u32 scanned;  // candidates the last run walked, for the overlay and the bench
    b32 refined;  // the last run reused the previous results
} LibSearch;

void lib_search_init(LibSearch *search, Arena *arena, u32 capacity);
// The next run starts from scratch: call it when the index was rebuilt.
void lib_search_invalidate(LibSearch *search);
void lib_search_set_order(LibSearch *search, LibSortColumn column, b32 descending);
void lib_search_set_filter(LibSearch *search, StringId artist, StringId album);
void lib_search_run(LibSearch *search, LibIndex *index, String8 query);

#endif // LIB_SEARCH_H
