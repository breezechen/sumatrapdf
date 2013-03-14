/* Copyright 2013 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 (see COPYING) */

// This file is auto-generated by gen_appprefs2.py

#ifndef AppPrefs2_h
#define AppPrefs2_h

class AdvancedSettings {
public:
	/* ***** fields for section AdvancedOptions ***** */
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
	/* ***** fields for section PagePadding ***** */
	// size of the left/right margin between window and document
	int outerX;
	// size of the top/bottom margin between window and document
	int outerY;
	// size of the horizontal margin between two pages
	int innerX;
	// size of the vertical margin between two pages
	int innerY;
	/* ***** fields for section ForwardSearch ***** */
	// whether the inverse search command line setting is visible in the
	// Settings dialog
	bool enableTeXEnhancements;
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
	/* ***** fields for section ExternalViewers ***** */
	// command line with which to call the external viewer, may contain %p
	// for page numer and %1 for the file name
	WStrVec commandLine;
	// name of the external viewer to be shown in the menu (implied by
	// CommandLine if missing)
	WStrVec name;
	// filter for which file types the menu item is to be shown (e.g.
	// *.pdf;*.xps)
	WStrVec filter;

	AdvancedSettings() : traditionalEbookUI(false), reuseInstance(false), mainWindowBackground(0xFFF200), escToExit(false), textColor(0x000000), pageColor(0xFFFFFF), outerX(4), outerY(2), innerX(4), innerY(4), enableTeXEnhancements(false), highlightOffset(0), highlightWidth(15), highlightColor(0x6581FF), highlightPermanent(false) { }
};

#ifdef INCLUDE_APPPREFS2_METADATA
enum SettingType {
	SType_Section,
	SType_Bool, SType_Color, SType_Int, SType_String,
	SType_BoolVec, SType_ColorVec, SType_IntVec, SType_StringVec,
};

struct SettingInfo {
	const WCHAR *name;
	SettingType type;
	size_t offset;
};

static SettingInfo gAdvancedSettingsInfo[] = {
#define myoff(x) offsetof(AdvancedSettings, x)
	/* ***** fields for section AdvancedOptions ***** */
	{ L"AdvancedOptions", SType_Section, /* persist */ -1 },
	{ L"TraditionalEbookUI", SType_Bool, myoff(traditionalEbookUI) },
	{ L"ReuseInstance", SType_Bool, myoff(reuseInstance) },
	{ L"MainWindowBackground", SType_Color, myoff(mainWindowBackground) },
	{ L"EscToExit", SType_Bool, myoff(escToExit) },
	{ L"TextColor", SType_Color, myoff(textColor) },
	{ L"PageColor", SType_Color, myoff(pageColor) },
	/* ***** fields for section PagePadding ***** */
	{ L"PagePadding", SType_Section, 0 },
	{ L"OuterX", SType_Int, myoff(outerX) },
	{ L"OuterY", SType_Int, myoff(outerY) },
	{ L"InnerX", SType_Int, myoff(innerX) },
	{ L"InnerY", SType_Int, myoff(innerY) },
	/* ***** fields for section ForwardSearch ***** */
	{ L"ForwardSearch", SType_Section, 0 },
	{ L"EnableTeXEnhancements", SType_Bool, myoff(enableTeXEnhancements) },
	{ L"HighlightOffset", SType_Int, myoff(highlightOffset) },
	{ L"HighlightWidth", SType_Int, myoff(highlightWidth) },
	{ L"HighlightColor", SType_Color, myoff(highlightColor) },
	{ L"HighlightPermanent", SType_Bool, myoff(highlightPermanent) },
	/* ***** fields for section ExternalViewers ***** */
	{ L"ExternalViewers", SType_Section, 0 },
	{ L"CommandLine", SType_StringVec, myoff(commandLine) },
	{ L"Name", SType_StringVec, myoff(name) },
	{ L"Filter", SType_StringVec, myoff(filter) },
#undef myoff
};

#endif

#endif
