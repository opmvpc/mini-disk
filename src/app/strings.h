// strings.h - every user facing string of the app, FR and EN side by side
// (ADR-011 D10, research/02 s12). Nothing outside this file may hold a literal
// the user reads: a widget asks for an id, the table answers in the language of
// the moment. The default is French, chosen at compile time.
//
// Two flat arrays and an enum, on purpose: the id is an index, the lookup is a
// load, and adding a language is adding an array. Format strings keep the same
// specifiers in the same order in both languages - that is the whole contract.
#ifndef APP_STRINGS_H
#define APP_STRINGS_H

#include "../base/base.h"
#include "../base/base_string.h"

typedef enum Str {
    Str_LibraryTitle = 0,
    Str_LibrarySearchPlaceholder,
    Str_LibraryCount,          // "%u / %u"
    Str_LibraryScanning,       // "%u fichiers, %u / %u dossiers"
    Str_LibraryEmptyTitle,
    Str_LibraryEmptyBody,
    Str_LibraryEmptyBody2,
    Str_LibraryEmptyAction,
    Str_LibraryEmptyDrop,
    Str_LibraryNoResultTitle,  // "Aucun resultat pour %S"
    Str_LibraryNoResultBody,

    Str_ColumnIndex,
    Str_ColumnTitle,
    Str_ColumnArtist,
    Str_ColumnAlbum,
    Str_ColumnDuration,
    Str_ColumnFormat,
    Str_ColumnYear,
    Str_ColumnAdded,

    Str_BrowserArtists,
    Str_BrowserAlbums,
    Str_BrowserAllArtists,
    Str_BrowserAllAlbums,
    Str_BrowserUnnamed,
    Str_BrowserCollapse,
    Str_BrowserExpand,

    Str_MenuAddSelection,
    Str_MenuAddTrack,
    Str_MenuSelectAll,
    Str_MenuSelectNone,

    Str_ToolbarAddFolder,
    Str_ToolbarCancelScan,
    Str_ToolbarCancelScanHint,
    Str_ToolbarAddToPlan,
    Str_ToolbarAddToPlanHint,
    Str_ToolbarDeviceHint,
    Str_ToolbarPreviewHint,
    Str_ToolbarSelected,       // "%llu selectionnees"

    Str_PlanTitle,
    Str_PlanSubtitle,          // "%u pistes - %S"
    Str_PlanEmptyBody,
    Str_DiscTitle,
    Str_DiscBurn,
    Str_DiscBurnHint,
    Str_DiscClear,
    Str_DiscClearHint,

    Str_StatusBoxes,           // "%u pistes : %llu boxes ... %llu"
    Str_StatusKeys,

    Str_DetailTitle,
    Str_DetailEmpty,
    Str_DetailNoCover,
    Str_DetailShow,
    Str_DetailHide,
    Str_DetailSize,            // "%u,%u Mo"
    Str_DetailSizeKb,          // "%u Ko"
    Str_MenuThumbnails,
    Str_DropHint,
    Str_COUNT
} Str;

typedef enum StrLang {
    StrLang_Fr = 0,
    StrLang_En,
    StrLang_COUNT
} StrLang;

#ifndef APP_LANG_DEFAULT
#define APP_LANG_DEFAULT StrLang_Fr
#endif

static const char *app_strings_fr[Str_COUNT] = {
    "Bibliothèque",
    "Rechercher un titre, un artiste, un album",
    "%u / %u",
    "analyse : %u fichiers, %u / %u dossiers",
    "Aucune musique pour l'instant",
    "Ajoutez un dossier contenant vos fichiers audio.",
    "L'indexation tourne en arrière-plan.",
    "Ajouter un dossier…",
    "Vous pouvez aussi déposer un dossier dans cette fenêtre.",
    "Aucun résultat pour « %S »",
    "Essayez un autre mot, ou effacez la recherche avec Échap.",

    "#",
    "TITRE",
    "ARTISTE",
    "ALBUM",
    "DURÉE",
    "FORMAT",
    "ANNÉE",
    "AJOUTÉ",

    "ARTISTE",
    "ALBUM",
    "Tous les artistes",
    "Tous les albums",
    "(sans nom)",
    "Replier le navigateur",
    "Déplier le navigateur",

    "Ajouter la sélection au plan",
    "Ajouter cette piste",
    "Tout sélectionner",
    "Désélectionner",

    "Ajouter un dossier",
    "Annuler l'analyse",
    "L'analyse s'arrête à la fin du dossier courant",
    "Ajouter au plan",
    "Entrée ajoute aussi la sélection",
    "Rafraîchir l'appareil",
    "Préécouter la sélection",
    "%llu sélectionnées",

    "Plan",
    "%u pistes · %S",
    "Sélectionnez des morceaux et appuyez sur Entrée.",
    "Disque",
    "Graver le disque",
    "Écrit le plan sur le disque inséré",
    "Vider",
    "Retire toutes les pistes du plan",

    "%u pistes · %llu boxes pour %llu lignes visibles · %llu boxes dans la frame",
    "Tab navigue · Ctrl+F recherche · Ctrl+A tout sélectionner · Entrée ajoute au plan",

    "Détail",
    "Sélectionnez une piste pour voir sa pochette.",
    "Pas de pochette",
    "Afficher le détail",
    "Masquer le détail",
    "%u,%u Mo",
    "%u Ko",
    "Pochettes",
    "Déposez pour ajouter à la bibliothèque",
};

static const char *app_strings_en[Str_COUNT] = {
    "Library",
    "Search a title, an artist, an album",
    "%u / %u",
    "scanning: %u files, %u / %u folders",
    "No music yet",
    "Add a folder with your audio files.",
    "Indexing runs in the background.",
    "Add folder…",
    "You can also drop a folder onto this window.",
    "No results for “%S”",
    "Try another word, or clear the search with Esc.",

    "#",
    "TITLE",
    "ARTIST",
    "ALBUM",
    "LENGTH",
    "FORMAT",
    "YEAR",
    "ADDED",

    "ARTIST",
    "ALBUM",
    "All artists",
    "All albums",
    "(no name)",
    "Collapse the browser",
    "Expand the browser",

    "Add selection to plan",
    "Add this track",
    "Select all",
    "Select none",

    "Add folder",
    "Cancel scan",
    "The scan stops at the end of the current folder",
    "Add to plan",
    "Enter adds the selection too",
    "Refresh the device",
    "Preview the selection",
    "%llu selected",

    "Plan",
    "%u tracks · %S",
    "Pick tracks and press Enter.",
    "Disc",
    "Burn disc",
    "Writes the plan to the inserted disc",
    "Clear",
    "Removes every track from the plan",

    "%u tracks · %llu boxes for %llu visible rows · %llu boxes in the frame",
    "Tab moves · Ctrl+F search · Ctrl+A select all · Enter adds to the plan",

    "Details",
    "Select a track to see its cover.",
    "No cover",
    "Show the details",
    "Hide the details",
    "%u.%u MB",
    "%u KB",
    "Cover art",
    "Drop to add to the library",
};

global StrLang app_lang = APP_LANG_DEFAULT;

// The raw literal, for str8f: a format string is a `const char *`.
md_inline const char *app_str_c(Str id) {
    Assert(id < Str_COUNT);
    return (app_lang == StrLang_Fr) ? app_strings_fr[id] : app_strings_en[id];
}

md_inline String8 app_str(Str id) { return str8_cstr(app_str_c(id)); }

#endif // APP_STRINGS_H
