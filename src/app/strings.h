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

    // --- the plan view (T-032) ------------------------------------------------
    Str_PlanDiscTab,           // "Disque %u"
    Str_PlanDiscTitlePlaceholder,
    Str_PlanLength,
    Str_PlanDefaultMode,
    Str_PlanOpen,
    Str_PlanSave,
    Str_PlanSaveAs,
    Str_PlanSavedHint,
    Str_PlanUnsavedHint,
    Str_PlanFileFilter,
    Str_PlanOpenTitle,
    Str_PlanSaveTitle,
    Str_PlanDefaultName,
    Str_PlanNewDisc,
    Str_PlanNewDiscHint,
    Str_PlanSplitFirstFit,
    Str_PlanSplitKeepAlbums,
    Str_PlanFill,
    Str_PlanFillHint,
    Str_PlanFillNone,
    Str_PlanColumnMode,
    Str_PlanColumnTitle,
    Str_PlanColumnSource,
    Str_PlanColumnClusters,
    Str_PlanMissing,
    Str_PlanShortenedTip,      // "Titre raccourci :%S"
    Str_PlanShortenFeat,
    Str_PlanShortenBrackets,
    Str_PlanShortenArtist,
    Str_PlanShortenTitle,
    Str_PlanGroupHeader,       // "%S - %u pistes - %S"
    Str_PlanGroupUnnamed,
    Str_MenuPlanRename,
    Str_MenuPlanRemove,
    Str_MenuPlanMoveUp,
    Str_MenuPlanMoveDown,
    Str_MenuPlanGroup,
    Str_MenuPlanUngroup,
    Str_MenuPlanModeSp,
    Str_MenuPlanModeMono,
    Str_MenuPlanModeLp2,
    Str_MenuPlanModeLp4,
    Str_MenuPlanSplit,

    // --- the capacity gauge (research/02 s9) ------------------------------------
    Str_GaugeTitle,
    Str_GaugeScaleUnit,        // "%u min (%s)"
    Str_GaugeReadout,          // "%S / %S"
    Str_GaugeRemaining,        // "reste %S SP - %S LP2 - %S LP4"
    Str_GaugeEmpty,            // "disque vierge %u min"
    Str_GaugeFull,
    Str_GaugeOverflow,         // "depassement %S"
    Str_GaugeSegmentTip,       // "%u. %S - %S - %s - facture %S (+%S)"
    Str_GaugeMergedTip,        // "%u pistes - %S - %s"
    Str_GaugeFreeTip,          // "reste %S en SP - %S en LP2 - %S en LP4"
    Str_GaugePlayheadTip,      // "%S / %S"
    Str_TocLabel,              // "TOC %u / %u car."
    Str_TocBreakdown,          // "titre du disque %u - groupes %u - pistes %u"
    Str_TocShorten,
    Str_TocShortenHint,
    Str_StatusPlan,            // "%u pistes - %S / %S"
    Str_PlanShortenFold,

    // The device panel (T-020). The driver steps are numbered in the text
    // itself: they get read out over the phone as often as they get clicked.
    Str_DeviceNone,
    Str_DeviceNoneHint,
    Str_DeviceSearching,
    Str_DeviceNoDriver,
    Str_DeviceNoDriverBody,    // "%S est branche, mais Windows ..."
    Str_DeviceStep1,
    Str_DeviceStep2,
    Str_DeviceStep3,
    Str_DeviceZadig,
    Str_DeviceZadigHint,
    Str_DeviceInUse,
    Str_DeviceConnected,       // "Connecte : %S"
    Str_DeviceError,           // "Appareil injoignable (%i)"
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

    "Disque %u",
    "Titre du disque",
    "Longueur",
    "Mode",
    "Ouvrir",
    "Enregistrer",
    "Enregistrer sous…",
    "Plan enregistré",
    "Modifications non enregistrées",
    "Plan de disque minidisk",
    "Ouvrir un plan",
    "Enregistrer le plan",
    "plan.mdplan",
    "Nouveau disque",
    "Ouvre un disque de plus et y met ce qui dépasse",
    "Répartir : au plus tôt",
    "Répartir : garder les albums ensemble",
    "Remplir l'espace restant",
    "Ajoute la sélection tant que ça rentre",
    "Rien de la sélection ne rentre dans l'espace restant",
    "MODE",
    "TITRE MD",
    "SOURCE",
    "CL.",
    "piste manquante",
    "Titre raccourci :%S",
    " feat.",
    " parenthèses",
    " artiste",
    " coupé",
    "%S · %u pistes · %S",
    "Groupe sans nom",
    "Renommer (F2)",
    "Retirer du plan (Suppr)",
    "Monter (Alt+↑)",
    "Descendre (Alt+↓)",
    "Grouper la sélection (Ctrl+G)",
    "Dissoudre le groupe (Ctrl+Maj+G)",
    "Mode SP (Ctrl+Alt+1)",
    "Mode SP mono (Ctrl+Alt+2)",
    "Mode LP2 (Ctrl+Alt+3)",
    "Mode LP4 (Ctrl+Alt+4)",
    "Couper le disque ici",

    "Capacité du disque",
    "%u min (%s)",
    "%S / %S",
    "reste %S SP · %S LP2 · %S LP4",
    "disque vierge %u min",
    "disque plein",
    "dépassement %S",
    "%u. %S · %S · %s · facturé %S (+%S)",
    "%u pistes · %S · %s",
    "reste %S en SP · %S en LP2 · %S en LP4",
    "%S / %S",
    "TOC %u / %u car.",
    "titre du disque %u · groupes %u · pistes %u",
    "Raccourcir automatiquement",
    "Réduit les titres les plus longs jusqu'à ce que le budget rentre",
    "%u pistes · %S / %S",
    " caractères adaptés au TOC",

    "Aucun appareil",
    "Branchez votre baladeur NetMD en USB.",
    "Recherche d'un appareil…",
    "Appareil détecté, pilote manquant",
    "%S est branché, mais Windows n'a aucun pilote pour lui. Une installation, une seule fois :",
    "1. Téléchargez Zadig (zadig.akeo.ie), puis lancez-le.",
    "2. Choisissez « Net MD Walkman » dans la liste, puis le pilote WinUSB.",
    "3. Cliquez « Install Driver », puis rebranchez l'appareil.",
    "Ouvrir zadig.akeo.ie",
    "Ouvre la page de téléchargement de Zadig dans votre navigateur",
    "Appareil utilisé par une autre application",
    "Connecté : %S",
    "Appareil injoignable (%i)",
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

    "Disc %u",
    "Disc title",
    "Length",
    "Mode",
    "Open",
    "Save",
    "Save as…",
    "Plan saved",
    "Unsaved changes",
    "minidisk disc plan",
    "Open a plan",
    "Save the plan",
    "plan.mdplan",
    "New disc",
    "Opens one more disc and moves the overflow onto it",
    "Split: first fit",
    "Split: keep albums together",
    "Fill the remaining space",
    "Adds the selection while it still fits",
    "Nothing in the selection fits in the remaining space",
    "MODE",
    "MD TITLE",
    "SOURCE",
    "CL.",
    "missing track",
    "Shortened title:%S",
    " feat.",
    " brackets",
    " artist",
    " cut",
    "%S · %u tracks · %S",
    "Unnamed group",
    "Rename (F2)",
    "Remove from the plan (Del)",
    "Move up (Alt+Up)",
    "Move down (Alt+Down)",
    "Group the selection (Ctrl+G)",
    "Ungroup (Ctrl+Shift+G)",
    "SP mode (Ctrl+Alt+1)",
    "SP mono mode (Ctrl+Alt+2)",
    "LP2 mode (Ctrl+Alt+3)",
    "LP4 mode (Ctrl+Alt+4)",
    "Cut the disc here",

    "Disc capacity",
    "%u min (%s)",
    "%S / %S",
    "%S SP · %S LP2 · %S LP4 left",
    "blank %u min disc",
    "disc full",
    "over by %S",
    "%u. %S · %S · %s · billed %S (+%S)",
    "%u tracks · %S · %s",
    "%S in SP · %S in LP2 · %S in LP4 left",
    "%S / %S",
    "TOC %u / %u chars",
    "disc title %u · groups %u · tracks %u",
    "Shorten automatically",
    "Trims the longest titles until the budget fits",
    "%u tracks · %S / %S",
    " characters adapted to the TOC",

    "No device",
    "Plug your NetMD player in over USB.",
    "Looking for a device…",
    "Device found, driver missing",
    "%S is plugged in, but Windows has no driver for it. One install, once per machine:",
    "1. Download Zadig (zadig.akeo.ie), then run it.",
    "2. Pick “Net MD Walkman” in the list, then the WinUSB driver.",
    "3. Click “Install Driver”, then plug the device back in.",
    "Open zadig.akeo.ie",
    "Opens the Zadig download page in your browser",
    "Device held by another application",
    "Connected: %S",
    "Device unreachable (%i)",
};

global StrLang app_lang = APP_LANG_DEFAULT;

// The raw literal, for str8f: a format string is a `const char *`.
md_inline const char *app_str_c(Str id) {
    Assert(id < Str_COUNT);
    return (app_lang == StrLang_Fr) ? app_strings_fr[id] : app_strings_en[id];
}

md_inline String8 app_str(Str id) { return str8_cstr(app_str_c(id)); }

#endif // APP_STRINGS_H
