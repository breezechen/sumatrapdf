/* Copyright 2013 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 (see COPYING) */

// This file is auto-generated by gen_appprefs3.py

#ifndef AppPrefs3_h
#define AppPrefs3_h

// Values which are persisted for bookmarks/favorites
struct Favorite {
    // name of this favorite as shown in the menu
    WCHAR * name;
    // which page this favorite is about
    int pageNo;
    // optional label for this page (if logical and physical numers
    // disagree)
    WCHAR * pageLabel;
    // assigned in AppendFavMenuItems()
    int menuId;
};

// Most values in this structure are remembered individually for every
// file and are by default also persisted so that reading can be resumed
struct File {
    // absolute path to a document that's been loaded successfully
    WCHAR * filePath;
    // in order to prevent documents that haven't been opened for a while
    // but used to be opened very frequently constantly remain in top
    // positions, the openCount will be cut in half after every week, so
    // that the Frequently Read list hopefully better reflects the
    // currently relevant documents
    int openCount;
    // a user can "pin" a preferred document to the Frequently Read list so
    // that the document isn't replaced by more frequently used ones
    bool isPinned;
    // if a document can no longer be found but we still remember valuable
    // state, it's classified as missing so that it can be hidden instead
    // of removed
    bool isMissing;
    // whether global defaults should be used when reloading this file
    // instead of the values listed below
    bool useGlobalValues;
    // how pages should be layed out for this document
    WCHAR * displayMode;
    // how far this document has been scrolled
    PointI scrollPos;
    // the scrollPos values are relative to the top-left corner of this
    // page
    int pageNo;
    // for bookmarking ebook files: offset of the current page reparse
    // point within html
    int reparseIdx;
    // the current zoom factor in % (negative values indicate virtual
    // settings)
    float zoomVirtual;
    // how far pages have been rotated as a multiple of 90 degrees
    int rotation;
    // default state of new SumatraPDF windows (same as the last closed)
    int windowState;
    // default position (can be on any monitor)
    RectI windowPos;
    // hex encoded MD5 fingerprint of file content (32 chars) followed by
    // crypt key (64 chars) - only applies for PDF documents
    char * decryptionKey;
    // whether the table of contents (Bookmarks) sidebar is shown for this
    // document
    bool tocVisible;
    // the width of the left sidebar panel containing the table of contents
    int sidebarDx;
    // tocState is an array of ids for ToC items that have been toggled by
    // the user (i.e. aren't in their default expansion state). - Note: We
    // intentionally track toggle state as opposed to expansion state so
    // that we only have to save a diff instead of all states for the whole
    // tree (which can be quite large) - and also due to backwards
    // compatibility
    Vec<int> * tocState;
    // Values which are persisted for bookmarks/favorites
    Vec<Favorite *> * favorite;
    // temporary value needed for FileHistory::cmpOpenCount
    size_t index;
    // the thumbnail is persisted separately as a PNG in sumatrapdfcache
    void * thumbnail;
};

// Most values on this structure can be updated through the UI and are
// persisted in SumatraPDF.ini (previously in sumatrapdfprefs.dat)
struct GlobalPrefs {
    // whether not to store display settings for individual documents
    bool globalPrefsOnly;
    // pointer to a static string that is part of LangDef, don't free
    char * currLangCode;
    // whether the toolbar should be visible by default in the main window
    bool toolbarVisible;
    // whether the Favorites sidebar should be visible by default in the
    // main window
    bool favVisible;
    // if false, we won't ask the user if he wants Sumatra to handle PDF
    // files
    bool pdfAssociateDontAskAgain;
    // if pdfAssociateDontAskAgain is true, says whether we should silently
    // associate or not
    bool pdfAssociateShouldAssociate;
    // whether SumatraPDF should check once a day whether updates are
    // available
    bool enableAutoUpdate;
    // if true, we remember which files we opened and their settings
    bool rememberOpenedFiles;
    // whether to display documents black-on-white or in system colors
    bool useSysColors;
    // pattern used to launch the editor when doing inverse search
    WCHAR * inverseSearchCmdLine;
    // whether to expose the SyncTeX enhancements to the user
    bool enableTeXEnhancements;
    // When we show 'new version available', user has an option to check
    // 'skip this version'. This remembers which version is to be skipped.
    // If NULL - don't skip
    WCHAR * versionToSkip;
    // the time SumatraPDF has last checked for updates (cf.
    // EnableAutoUpdate)
    FILETIME lastUpdateTime;
    // how pages should be layed out by default
    WCHAR * defaultDisplayMode;
    // the default zoom factor in % (negative values indicate virtual
    // settings)
    float defaultZoom;
    // default state of new SumatraPDF windows (same as the last closed)
    int windowState;
    // default position (can be on any monitor)
    RectI windowPos;
    // whether the table of contents (Bookmarks) sidebar should be shown by
    // default when its available for a document
    bool tocVisible;
    // if sidebar (favorites and/or bookmarks) is visible, this is the
    // width of the left sidebar panel containing them
    int sidebarDx;
    // if both favorites and bookmarks parts of sidebar are visible, this
    // is the height of bookmarks (table of contents) part
    int tocDy;
    // whether to display Frequently Read documents or the About page in an
    // empty window
    bool showStartPage;
    // week count since 2011-01-01 needed to "age" openCount values in file
    // history
    int openCountWeek;
    // display CBX double pages from right to left
    bool cbxR2L;
    // Most values in this structure are remembered individually for every
    // file and are by default also persisted so that reading can be
    // resumed
    Vec<File *> * file;
    // modification time of the preferences file when it was last read
    FILETIME lastPrefUpdate;
    // a list of settings which this version of SumatraPDF didn't know how
    // to handle
    char * unknownSettings;
};

// this list contains a list of additional external viewers for various
// file types (multiple entries of the same format are recognised)
struct ExternalViewer {
    // command line with which to call the external viewer, may contain %p
    // for page numer and %1 for the file name
    WCHAR * commandLine;
    // name of the external viewer to be shown in the menu (implied by
    // CommandLine if missing)
    WCHAR * name;
    // filter for which file types the menu item is to be shown (e.g.
    // "*.pdf;*.xps"; "*" if missing)
    WCHAR * filter;
};

// these values allow to customize how the forward search highlight
// appears
struct ForwardSearch {
    // when set to a positive value, the forward search highlight style
    // will be changed to a rectangle at the left of the page (with the
    // indicated amount of margin from the page margin)
    int highlightOffset;
    // the width of the highlight rectangle for when HighlightOffset is set
    int highlightWidth;
    // the color used for the forward search highlight
    COLORREF highlightColor;
    // whether the forward search highlight will remain visible until the
    // next mouse click instead of fading away instantly
    bool highlightPermanent;
};

// these values allow to tweak the experimental feature for using a
// color gradient to subconsciously determine reading progress
struct BackgroundGradient {
    // whether to draw a gradient behind the pages
    bool enabled;
    // color at the top of the document (first page)
    COLORREF colorTop;
    // color at the center of the document (middlest page)
    COLORREF colorMiddle;
    // color at the bottom of the document (last page)
    COLORREF colorBottom;
};

// these values allow to change how far apart pages are layed out
struct PagePadding {
    // size of the left/right margin between window and document
    int outerX;
    // size of the top/bottom margin between window and document
    int outerY;
    // size of the horizontal margin between two pages
    int innerX;
    // size of the vertical margin between two pages
    int innerY;
};

// these values allow to override the default settings in the Print
// dialog
struct PrinterDefaults {
    // default value for scaling (shrink, fit, none)
    char * printScale;
    // default value for the compatibility option
    bool printAsImage;
};

// these values allow to tweak various bits and pieces of SumatraPDF
struct AdvancedPrefs {
    // whether the UI used for PDF documents will be used for ebooks as
    // well (enables printing and searching, disables automatic reflow)
    bool traditionalEbookUI;
    // whether opening a new document should happen in an already running
    // SumatraPDF instance so that there's only one process and documents
    // aren't opend twice
    bool reuseInstance;
    // background color of the non-document windows, traditionally yellow
    COLORREF mainWindowBackground;
    // whether the Esc key will exit SumatraPDF same as 'q'
    bool escToExit;
    // color value with which black (text) will be substituted
    COLORREF textColor;
    // color value with which white (background) will be substituted
    COLORREF pageColor;
    // zoom step size in percents relative to the current zoom level (if
    // zero or negative, the values from ZoomLevels are used instead)
    float zoomIncrement;
    // zoom levels which zooming steps through, excluding the virtual zoom
    // levels fit page, fit content and fit width (minimal allowed value is
    // 8.33 and maximum allowed value is 6400)
    Vec<float> * zoomLevels;
};

// All values in this structure are read from SumatraPDF-user.ini and
// can't be changed from within the UI
struct UserPrefs {
    // these values allow to tweak various bits and pieces of SumatraPDF
    AdvancedPrefs advancedPrefs;
    // these values allow to override the default settings in the Print
    // dialog
    PrinterDefaults printerDefaults;
    // these values allow to change how far apart pages are layed out
    PagePadding pagePadding;
    // these values allow to tweak the experimental feature for using a
    // color gradient to subconsciously determine reading progress
    BackgroundGradient backgroundGradient;
    // these values allow to customize how the forward search highlight
    // appears
    ForwardSearch forwardSearch;
    // this list contains a list of additional external viewers for various
    // file types (multiple entries of the same format are recognised)
    Vec<ExternalViewer *> * externalViewer;
};

#if defined(INCLUDE_APPPREFS3_STRUCTS) || defined(INCLUDE_APPPREFS3_METADATA)

enum SettingType {
    Type_Struct, Type_Array, Type_Compact,
    Type_Bool, Type_Color, Type_Float, Type_Int, Type_String, Type_Utf8String,
    Type_IntArray, Type_FloatArray,
};

struct FieldInfo {
    size_t offset;
    SettingType type;
    intptr_t value;
};

struct SettingInfo {
    uint16_t structSize;
    uint16_t fieldCount;
    const FieldInfo *fields;
    const char *fieldNames;
};

static inline const SettingInfo *GetSubstruct(const FieldInfo& field) { return (const SettingInfo *)field.value; }

#endif

#ifdef INCLUDE_APPPREFS3_METADATA

static const FieldInfo gFILETIMEFields[] = {
    { offsetof(FILETIME, dwHighDateTime), Type_Int, 0 },
    { offsetof(FILETIME, dwLowDateTime),  Type_Int, 0 },
};
static const SettingInfo gFILETIMEInfo = { sizeof(FILETIME), 2, gFILETIMEFields, "DwHighDateTime\0DwLowDateTime" };

static const FieldInfo gRectIFields[] = {
    { offsetof(RectI, x),  Type_Int, 0 },
    { offsetof(RectI, y),  Type_Int, 0 },
    { offsetof(RectI, dx), Type_Int, 0 },
    { offsetof(RectI, dy), Type_Int, 0 },
};
static const SettingInfo gRectIInfo = { sizeof(RectI), 4, gRectIFields, "X\0Y\0Dx\0Dy" };

static const FieldInfo gPointIFields[] = {
    { offsetof(PointI, x), Type_Int, 0 },
    { offsetof(PointI, y), Type_Int, 0 },
};
static const SettingInfo gPointIInfo = { sizeof(PointI), 2, gPointIFields, "X\0Y" };

static const FieldInfo gFavoriteFields[] = {
    { offsetof(Favorite, name),      Type_String, NULL },
    { offsetof(Favorite, pageNo),    Type_Int,    0    },
    { offsetof(Favorite, pageLabel), Type_String, NULL },
};
static const SettingInfo gFavoriteInfo = { sizeof(Favorite), 3, gFavoriteFields, "Name\0PageNo\0PageLabel" };

static const FieldInfo gFileFields[] = {
    { offsetof(File, filePath),        Type_String,     NULL                     },
    { offsetof(File, openCount),       Type_Int,        0                        },
    { offsetof(File, isPinned),        Type_Bool,       false                    },
    { offsetof(File, isMissing),       Type_Bool,       false                    },
    { offsetof(File, useGlobalValues), Type_Bool,       false                    },
    { offsetof(File, displayMode),     Type_String,     (intptr_t)L"automatic"   },
    { offsetof(File, scrollPos),       Type_Compact,    (intptr_t)&gPointIInfo   },
    { offsetof(File, pageNo),          Type_Int,        1                        },
    { offsetof(File, reparseIdx),      Type_Int,        0                        },
    { offsetof(File, zoomVirtual),     Type_Float,      (intptr_t)"100"          },
    { offsetof(File, rotation),        Type_Int,        0                        },
    { offsetof(File, windowState),     Type_Int,        0                        },
    { offsetof(File, windowPos),       Type_Compact,    (intptr_t)&gRectIInfo    },
    { offsetof(File, decryptionKey),   Type_Utf8String, NULL                     },
    { offsetof(File, tocVisible),      Type_Bool,       true                     },
    { offsetof(File, sidebarDx),       Type_Int,        0                        },
    { offsetof(File, tocState),        Type_IntArray,   NULL                     },
    { offsetof(File, favorite),        Type_Array,      (intptr_t)&gFavoriteInfo },
};
static const SettingInfo gFileInfo = { sizeof(File), 18, gFileFields, "FilePath\0OpenCount\0IsPinned\0IsMissing\0UseGlobalValues\0DisplayMode\0ScrollPos\0PageNo\0ReparseIdx\0ZoomVirtual\0Rotation\0WindowState\0WindowPos\0DecryptionKey\0TocVisible\0SidebarDx\0TocState\0Favorite" };

static const FieldInfo gGlobalPrefsFields[] = {
    { offsetof(GlobalPrefs, globalPrefsOnly),             Type_Bool,       false                    },
    { offsetof(GlobalPrefs, currLangCode),                Type_Utf8String, NULL                     },
    { offsetof(GlobalPrefs, toolbarVisible),              Type_Bool,       true                     },
    { offsetof(GlobalPrefs, favVisible),                  Type_Bool,       false                    },
    { offsetof(GlobalPrefs, pdfAssociateDontAskAgain),    Type_Bool,       false                    },
    { offsetof(GlobalPrefs, pdfAssociateShouldAssociate), Type_Bool,       false                    },
    { offsetof(GlobalPrefs, enableAutoUpdate),            Type_Bool,       true                     },
    { offsetof(GlobalPrefs, rememberOpenedFiles),         Type_Bool,       true                     },
    { offsetof(GlobalPrefs, useSysColors),                Type_Bool,       false                    },
    { offsetof(GlobalPrefs, inverseSearchCmdLine),        Type_String,     NULL                     },
    { offsetof(GlobalPrefs, enableTeXEnhancements),       Type_Bool,       false                    },
    { offsetof(GlobalPrefs, versionToSkip),               Type_String,     NULL                     },
    { offsetof(GlobalPrefs, lastUpdateTime),              Type_Compact,    (intptr_t)&gFILETIMEInfo },
    { offsetof(GlobalPrefs, defaultDisplayMode),          Type_String,     (intptr_t)L"automatic"   },
    { offsetof(GlobalPrefs, defaultZoom),                 Type_Float,      (intptr_t)"-1"           },
    { offsetof(GlobalPrefs, windowState),                 Type_Int,        1                        },
    { offsetof(GlobalPrefs, windowPos),                   Type_Compact,    (intptr_t)&gRectIInfo    },
    { offsetof(GlobalPrefs, tocVisible),                  Type_Bool,       true                     },
    { offsetof(GlobalPrefs, sidebarDx),                   Type_Int,        0                        },
    { offsetof(GlobalPrefs, tocDy),                       Type_Int,        0                        },
    { offsetof(GlobalPrefs, showStartPage),               Type_Bool,       true                     },
    { offsetof(GlobalPrefs, openCountWeek),               Type_Int,        0                        },
    { offsetof(GlobalPrefs, cbxR2L),                      Type_Bool,       false                    },
    { offsetof(GlobalPrefs, file),                        Type_Array,      (intptr_t)&gFileInfo     },
};
static const SettingInfo gGlobalPrefsInfo = { sizeof(GlobalPrefs), 24, gGlobalPrefsFields, "GlobalPrefsOnly\0CurrLangCode\0ToolbarVisible\0FavVisible\0PdfAssociateDontAskAgain\0PdfAssociateShouldAssociate\0EnableAutoUpdate\0RememberOpenedFiles\0UseSysColors\0InverseSearchCmdLine\0EnableTeXEnhancements\0VersionToSkip\0LastUpdateTime\0DefaultDisplayMode\0DefaultZoom\0WindowState\0WindowPos\0TocVisible\0SidebarDx\0TocDy\0ShowStartPage\0OpenCountWeek\0CbxR2L\0File" };

static const FieldInfo gAdvancedPrefsFields[] = {
    { offsetof(AdvancedPrefs, traditionalEbookUI),   Type_Bool,       false                                                                                                                 },
    { offsetof(AdvancedPrefs, reuseInstance),        Type_Bool,       false                                                                                                                 },
    { offsetof(AdvancedPrefs, mainWindowBackground), Type_Color,      0xfff200                                                                                                              },
    { offsetof(AdvancedPrefs, escToExit),            Type_Bool,       false                                                                                                                 },
    { offsetof(AdvancedPrefs, textColor),            Type_Color,      0x000000                                                                                                              },
    { offsetof(AdvancedPrefs, pageColor),            Type_Color,      0xffffff                                                                                                              },
    { offsetof(AdvancedPrefs, zoomIncrement),        Type_Float,      (intptr_t)"0"                                                                                                         },
    { offsetof(AdvancedPrefs, zoomLevels),           Type_FloatArray, (intptr_t)"8.33 12.5 18 25 33.33 50 66.67 75 100 125 150 200 300 400 600 800 1000 1200 1600 2000 2400 3200 4800 6400" },
};
static const SettingInfo gAdvancedPrefsInfo = { sizeof(AdvancedPrefs), 8, gAdvancedPrefsFields, "TraditionalEbookUI\0ReuseInstance\0MainWindowBackground\0EscToExit\0TextColor\0PageColor\0ZoomIncrement\0ZoomLevels" };

static const FieldInfo gPrinterDefaultsFields[] = {
    { offsetof(PrinterDefaults, printScale),   Type_Utf8String, (intptr_t)"shrink" },
    { offsetof(PrinterDefaults, printAsImage), Type_Bool,       false              },
};
static const SettingInfo gPrinterDefaultsInfo = { sizeof(PrinterDefaults), 2, gPrinterDefaultsFields, "PrintScale\0PrintAsImage" };

static const FieldInfo gPagePaddingFields[] = {
    { offsetof(PagePadding, outerX), Type_Int, 4 },
    { offsetof(PagePadding, outerY), Type_Int, 2 },
    { offsetof(PagePadding, innerX), Type_Int, 4 },
    { offsetof(PagePadding, innerY), Type_Int, 4 },
};
static const SettingInfo gPagePaddingInfo = { sizeof(PagePadding), 4, gPagePaddingFields, "OuterX\0OuterY\0InnerX\0InnerY" };

static const FieldInfo gBackgroundGradientFields[] = {
    { offsetof(BackgroundGradient, enabled),     Type_Bool,  false    },
    { offsetof(BackgroundGradient, colorTop),    Type_Color, 0xaa2828 },
    { offsetof(BackgroundGradient, colorMiddle), Type_Color, 0x28aa28 },
    { offsetof(BackgroundGradient, colorBottom), Type_Color, 0x2828aa },
};
static const SettingInfo gBackgroundGradientInfo = { sizeof(BackgroundGradient), 4, gBackgroundGradientFields, "Enabled\0ColorTop\0ColorMiddle\0ColorBottom" };

static const FieldInfo gForwardSearchFields[] = {
    { offsetof(ForwardSearch, highlightOffset),    Type_Int,   0        },
    { offsetof(ForwardSearch, highlightWidth),     Type_Int,   15       },
    { offsetof(ForwardSearch, highlightColor),     Type_Color, 0x6581ff },
    { offsetof(ForwardSearch, highlightPermanent), Type_Bool,  false    },
};
static const SettingInfo gForwardSearchInfo = { sizeof(ForwardSearch), 4, gForwardSearchFields, "HighlightOffset\0HighlightWidth\0HighlightColor\0HighlightPermanent" };

static const FieldInfo gExternalViewerFields[] = {
    { offsetof(ExternalViewer, commandLine), Type_String, NULL },
    { offsetof(ExternalViewer, name),        Type_String, NULL },
    { offsetof(ExternalViewer, filter),      Type_String, NULL },
};
static const SettingInfo gExternalViewerInfo = { sizeof(ExternalViewer), 3, gExternalViewerFields, "CommandLine\0Name\0Filter" };

static const FieldInfo gUserPrefsFields[] = {
    { offsetof(UserPrefs, advancedPrefs),      Type_Struct, (intptr_t)&gAdvancedPrefsInfo      },
    { offsetof(UserPrefs, printerDefaults),    Type_Struct, (intptr_t)&gPrinterDefaultsInfo    },
    { offsetof(UserPrefs, pagePadding),        Type_Struct, (intptr_t)&gPagePaddingInfo        },
    { offsetof(UserPrefs, backgroundGradient), Type_Struct, (intptr_t)&gBackgroundGradientInfo },
    { offsetof(UserPrefs, forwardSearch),      Type_Struct, (intptr_t)&gForwardSearchInfo      },
    { offsetof(UserPrefs, externalViewer),     Type_Array,  (intptr_t)&gExternalViewerInfo     },
};
static const SettingInfo gUserPrefsInfo = { sizeof(UserPrefs), 6, gUserPrefsFields, "AdvancedPrefs\0PrinterDefaults\0PagePadding\0BackgroundGradient\0ForwardSearch\0ExternalViewer" };

#endif

#endif
