// netmd_backup.h - the copy of the TOC taken before anything is written to it
// (ADR-011 D4, research/01 s6.3).
//
// The TOC of a MiniDisc holds the only copy of the titles, the order and the
// groups; it is rewritten in place, it lives in the device's RAM until the disc
// is ejected, and the pain point this file exists for is the one cluster of bug
// reports every NetMD tool shares - "it ate my titles". So before any edit
// command reaches the device, the DiscLayout that was just read is written out
// as text under %LOCALAPPDATA%\minidisk\toc-backups.
//
// Text, not a binary blob, for three reasons: it can be read without this
// program, it diffs, and a restore written in a later ticket has to parse
// exactly what a human can check by eye. The file is written the way every
// other file of this application is (ADR-010): a temporary next to it, then one
// atomic rename.
#ifndef NETMD_BACKUP_H
#define NETMD_BACKUP_H

#include "../../platform/platform.h"
#include "netmd_disc.h"

#define NETMD_BACKUP_VERSION 1

// What names the disc in the file name. Not an identity the disc carries - a
// MiniDisc has none - but the three things that together tell two discs apart
// in a shelf of backups: the title, the track count and the total time.
u64 netmd_disc_id(const DiscLayout *layout);

// The whole layout as the file holds it.
String8 netmd_backup_serialize(Arena *arena, const DiscLayout *layout);
// Text back into a layout. The text is untrusted input - a file the user may
// have edited - so this is a validation boundary (ADR-012): a line it does not
// understand is skipped, and it answers 0 when the header is not ours.
b32 netmd_backup_parse(String8 text, DiscLayout *out);

// "<dir>\<disc-id>-<yyyymmdd-hhmmss>.txt"
String8 netmd_backup_path(Arena *arena, String8 dir, const DiscLayout *layout,
                          const OsWallClock *now);
// Creates `dir`, writes the backup atomically, and hands back the path it used.
// 0 when the file could not be written - and then no edit may proceed (D4).
b32 netmd_backup_write(Arena *arena, String8 dir, const DiscLayout *layout, String8 *out_path);
// %LOCALAPPDATA%\minidisk\toc-backups, or the temp folder when there is none.
String8 netmd_backup_dir(Arena *arena);

#endif  // NETMD_BACKUP_H
