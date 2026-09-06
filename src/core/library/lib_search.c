#include "lib_search.h"

void lib_search_init(LibSearch *search, Arena *arena, u32 capacity) {
    StructZero(search);
    search->capacity = capacity ? capacity : 1;
    search->results = push_array(arena, u32, search->capacity);
    search->word_count = (search->capacity + 63) / 64;
    search->matched = push_array_zero(arena, u64, search->word_count);
    search->artist_filter = LIB_FILTER_ANY;
    search->album_filter = LIB_FILTER_ANY;
}

void lib_search_invalidate(LibSearch *search) { search->valid = 0; }

void lib_search_set_order(LibSearch *search, LibSortColumn column, b32 descending) {
    // The order changes which way the results are emitted, not which tracks
    // match: only the emit pass has to run again, but that is the cheap one.
    search->column = column;
    search->descending = descending;
}

void lib_search_set_filter(LibSearch *search, StringId artist, StringId album) {
    if (search->artist_filter != artist || search->album_filter != album) {
        search->artist_filter = artist;
        search->album_filter = album;
        search->valid = 0;
    }
}

// Substring over normalized bytes: sixteen candidate positions per compare
// (SSE2 is our baseline), then the tail confirms. This is the innermost loop
// of the whole feature - it runs once per token per surviving track.
static b32 lib_search_contains(const u8 *haystack, u32 haystack_size, const u8 *needle,
                               u32 needle_size) {
    if (needle_size > haystack_size) { return 0; }
    u32 last = haystack_size - needle_size;  // inclusive
    __m128i first = _mm_set1_epi8((char)needle[0]);
    u32 i = 0;
    // i + 15 <= last, so the widest load still stops inside the haystack.
    for (; i + 16 <= last + 1; i += 16) {
        __m128i block = _mm_loadu_si128((const __m128i *)(haystack + i));
        u32 hits = (u32)_mm_movemask_epi8(_mm_cmpeq_epi8(block, first));
        while (hits) {
            u32 at = i + (u32)_tzcnt_u32(hits);
            hits &= hits - 1;
            u32 j = 1;
            while (j < needle_size && haystack[at + j] == needle[j]) { j += 1; }
            if (j == needle_size) { return 1; }
        }
    }
    for (; i <= last; i += 1) {
        if (haystack[i] != needle[0]) { continue; }
        u32 j = 1;
        while (j < needle_size && haystack[i + j] == needle[j]) { j += 1; }
        if (j == needle_size) { return 1; }
    }
    return 0;
}

typedef struct LibSearchQuery {
    String8 tokens[LIB_TOKEN_MAX];
    u32 token_count;
    u64 needed;  // the letters every match must carry
} LibSearchQuery;

md_inline b32 lib_search_match(const LibIndex *index, const LibSearchQuery *query, u32 id) {
    if ((index->mask[id] & query->needed) != query->needed) { return 0; }
    const u8 *text = index->text + index->text_off[id];
    u32 size = index->text_len[id];
    for (u32 t = 0; t < query->token_count; t += 1) {
        if (!lib_search_contains(text, size, query->tokens[t].str, (u32)query->tokens[t].size)) {
            return 0;
        }
    }
    return 1;
}

void lib_search_run(LibSearch *search, LibIndex *index, String8 text) {
    AssertAlways(index->count <= search->capacity);
    u8 normalized[LIB_QUERY_CAP];
    u32 size = (u32)lib_normalize(normalized, sizeof(normalized), text, 0);

    // Tokens: whitespace separated, ANDed. More than LIB_TOKEN_MAX of them is
    // a user leaning on the space bar, and the extra ones change nothing.
    LibSearchQuery query;
    StructZero(&query);
    String8 tokens[LIB_TOKEN_MAX];
    u32 token_count = 0;
    for (u32 at = 0; at < size && token_count < LIB_TOKEN_MAX;) {
        while (at < size && normalized[at] == ' ') { at += 1; }
        u32 start = at;
        while (at < size && normalized[at] != ' ') { at += 1; }
        if (at > start) {
            tokens[token_count] = str8(normalized + start, at - start);
            query.needed |= lib_char_mask(tokens[token_count]);
            token_count += 1;
        }
    }

    // The refinement is sound exactly when the new normalized query extends the
    // previous one: every old token is then a substring of a new one, so a
    // track that failed before cannot pass now.
    b32 refine = search->valid && size >= search->query_size &&
                 mem_cmp(normalized, search->query, search->query_size) == 0;

    // And a token the previous query already carried word for word is proven
    // on every candidate that survived it: typing "the b" after "the" tests
    // "b" and nothing else, which is where the sub millisecond comes from.
    for (u32 t = 0; t < token_count; t += 1) {
        b32 proven = 0;
        for (u32 at = 0; refine && at < search->query_size;) {
            while (at < search->query_size && search->query[at] == ' ') { at += 1; }
            u32 start = at;
            while (at < search->query_size && search->query[at] != ' ') { at += 1; }
            if (at > start && str8_eq(str8(search->query + start, at - start), tokens[t])) {
                proven = 1;
                break;
            }
        }
        // A one letter token needs no text at all: the mask holds one bit per
        // letter and per digit, so testing that bit *is* the substring test.
        u8 c = tokens[t].str[0];
        b32 in_mask = tokens[t].size == 1 &&
                      ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'));
        if (!proven && !in_mask) {
            query.tokens[query.token_count] = tokens[t];
            query.token_count += 1;
        }
    }

    const Library *library = index->library;
    StringId artist_filter = search->artist_filter;
    StringId album_filter = search->album_filter;
    u64 *matched = search->matched;
    u32 words = (index->count + 63) / 64;
    u32 scanned = 0;

    // Pass one, always in id order: the normalized blob was written in id
    // order, so this walks it forwards instead of jumping through 12 MB.
    if (refine) {
        for (u32 word = 0; word < words; word += 1) {
            u64 bits = matched[word];
            while (bits) {
                u32 bit = (u32)_tzcnt_u64(bits);
                bits &= bits - 1;
                u32 id = word * 64 + bit;
                scanned += 1;
                if (!lib_search_match(index, &query, id)) {
                    matched[word] &= ~(1ull << bit);
                }
            }
        }
    } else {
        mem_zero(matched, sizeof(u64) * search->word_count);
        for (u32 id = 0; id < index->count; id += 1) {
            if (!(library->flags[id] & LibTrackFlag_Live)) { continue; }
            if (artist_filter != LIB_FILTER_ANY && library->artist_id[id] != artist_filter) {
                continue;
            }
            if (album_filter != LIB_FILTER_ANY && library->album_id[id] != album_filter) {
                continue;
            }
            scanned += 1;
            if (lib_search_match(index, &query, id)) {
                matched[id >> 6] |= 1ull << (id & 63);
            }
        }
    }

    // Pass two: the sorted view decides the order, the bitset decides who is
    // in it. Both are walked forwards, and the bitset is 12 KB for 100 000
    // tracks - it stays in L1 the whole way. A refinement whose order did not
    // change skips even that: its previous results are already sorted, and
    // compacting them in place is O(what was on screen).
    u32 out = 0;
    if (refine && search->emitted_column == search->column &&
        search->emitted_desc == search->descending) {
        for (u32 i = 0; i < search->result_count; i += 1) {
            u32 id = search->results[i];
            if (matched[id >> 6] & (1ull << (id & 63))) {
                search->results[out] = id;
                out += 1;
            }
        }
    } else {
        const u32 *order = lib_index_order(index, search->column);
        u32 live = index->live_count;
        for (u32 i = 0; i < live; i += 1) {
            u32 id = order[search->descending ? (live - 1 - i) : i];
            if (matched[id >> 6] & (1ull << (id & 63))) {
                search->results[out] = id;
                out += 1;
            }
        }
    }
    search->emitted_column = search->column;
    search->emitted_desc = search->descending;

    search->result_count = out;
    search->scanned = scanned;
    search->refined = refine;
    mem_copy(search->query, normalized, size);
    search->query_size = size;
    search->valid = 1;
}
