/* Copyright 2012 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

#include "Mui.h"
#include "VecSegmented.h"
#include "Scoped.h"

using namespace Gdiplus;

/*
A css-like way to style controls/windows.

We define a bunch of css-like properties.

We have a Style, which is a logical group of properties.

Each control can have one or more styles that define how
a control looks like. A window has only one set of properties
but a button has several, one for each visual state of
the button (normal, on hover, pressed, default).

We define a bunch of default style so that if e.g. button
doesn't have style explicitly set, it'll get all the necessary
properties from our default set and have a consistent look.

Prop objects are never freed. To conserve memory, they are
internalized i.e. there are never 2 Prop objects with exactly
the same data.
*/

namespace mui {
namespace css {

#define MKARGB(a, r, g, b) (((ARGB) (b)) | ((ARGB) (g) << 8) | ((ARGB) (r) << 16) | ((ARGB) (a) << 24))
#define MKRGB(r, g, b) (((ARGB) (b)) | ((ARGB) (g) << 8) | ((ARGB) (r) << 16) | ((ARGB)(0xff) << 24))

struct PropToGet {
    // provided by the caller
    PropType    type;
    // filled-out by GetProps(). Must be set to NULL by
    // caller to enable being called twice with different
    // Style objects
    Prop *      prop;
};

struct FontCacheEntry {
    Prop *fontName;
    Prop *fontSize;
    Prop *fontWeight;
    Font *font;

    // Prop objects are interned, so if the pointer is
    // the same, the value is the same too
    bool operator==(FontCacheEntry& other) const {
        return ((fontName == other.fontName) &&
                (fontSize == other.fontSize) &&
                (fontWeight == other.fontWeight));
    }
};

VecSegmented<Prop> *gAllProps = NULL;

Style *gStyleDefault = NULL;
Style *gStyleButtonDefault = NULL;
Style *gStyleButtonMouseOver = NULL;

static Vec<FontCacheEntry> *gCachedFonts = NULL;

struct PropCacheEntry {
    Style *     style1;
    size_t      style1Id;
    Style *     style2;
    size_t      style2Id;
    Prop **     props; // memory within gCachedProps
};

static Vec<PropCacheEntry> *    gPropCache = NULL;
static VecSegmented<Prop*> *    gCachedProps = NULL;

void Initialize()
{
    CrashIf(gAllProps);
    mui::InitMuiCriticalSection();

    gAllProps = new VecSegmented<Prop>();
    gCachedFonts = new Vec<FontCacheEntry>();

    // gDefaults is the very basic set shared by everyone
    gStyleDefault = new Style();
    gStyleDefault->Set(Prop::AllocFontName(L"Times New Roman"));
    gStyleDefault->Set(Prop::AllocFontSize(14));
    gStyleDefault->Set(Prop::AllocFontWeight(FontStyleBold));
    gStyleDefault->Set(Prop::AllocColorSolid(PropColor, "black"));
    //gDefaults->Set(Prop::AllocColorSolid(PropBgColor, 0xff, 0xff, 0xff));
    ARGB c1 = MKRGB(0xf5, 0xf6, 0xf6);
    ARGB c2 = MKRGB(0xe4, 0xe4, 0xe3);
    gStyleDefault->Set(Prop::AllocColorLinearGradient(PropBgColor, LinearGradientModeVertical, c1, c2));
    gStyleDefault->SetBorderWidth(1);
    gStyleDefault->SetBorderColor(MKRGB(0x99, 0x99, 0x99));
    gStyleDefault->Set(Prop::AllocColorSolid(PropBorderBottomColor, "#888"));
    gStyleDefault->Set(Prop::AllocPadding(0, 0, 0, 0));
    gStyleDefault->Set(Prop::AllocTextAlign(Align_Left));

    gStyleButtonDefault = new Style(gStyleDefault);
    gStyleButtonDefault->Set(Prop::AllocPadding(4, 8, 4, 8));
    gStyleButtonDefault->Set(Prop::AllocFontName(L"Lucida Grande"));
    gStyleButtonDefault->Set(Prop::AllocFontSize(8));
    gStyleButtonDefault->Set(Prop::AllocFontWeight(FontStyleBold));

    gStyleButtonMouseOver = new Style(gStyleButtonDefault);
    gStyleButtonMouseOver->Set(Prop::AllocColorSolid(PropBorderTopColor, "#777"));
    gStyleButtonMouseOver->Set(Prop::AllocColorSolid(PropBorderRightColor, "#777"));
    gStyleButtonMouseOver->Set(Prop::AllocColorSolid(PropBorderBottomColor, "#666"));
    //gStyleButtonMouseOver->Set(Prop::AllocColorSolid(PropBgColor, 180, 0, 0, 255));
    //gStyleButtonMouseOver->Set(Prop::AllocColorSolid(PropBgColor, "transparent"));

    gPropCache = new Vec<PropCacheEntry>();
    gCachedProps = new VecSegmented<Prop*>();
}

static void DeleteCachedFonts()
{
    for (size_t i = 0; i < gCachedFonts->Count(); i++) {
        FontCacheEntry c = gCachedFonts->At(i);
        ::delete c.font;
    }
    delete gCachedFonts;

    delete gPropCache;
    delete gCachedProps;
}

void Destroy()
{
    VecSegmented<Prop>::Iter iter(gAllProps);
    for (Prop *p = iter.Next(); p; p = iter.Next()) {
        p->Free();
    }

    delete gAllProps;

    delete gStyleButtonDefault;
    delete gStyleButtonMouseOver;

    DeleteCachedFonts();
    mui::DeleteMuiCriticalSection();
}

bool IsWidthProp(PropType type)
{
    return (PropBorderTopWidth == type) ||
           (PropBorderRightWidth == type) ||
           (PropBorderBottomWidth == type) ||
           (PropBorderLeftWidth == type);
}

bool IsColorProp(PropType type)
{
    return (PropColor == type) ||
           (PropBgColor == type) ||
           (PropBorderTopColor == type) ||
           (PropBorderRightColor == type) ||
           (PropBorderBottomColor == type) ||
           (PropBorderLeftColor == type);
}

// based on https://developer.mozilla.org/en/CSS/color_value
// TODO: add more colors
// TODO: change strings into linear string format, similar to how we store names
// html tags
static struct {
    const char *name;
    ARGB        color;
} gKnownColors[] = {
    { "black",  MKRGB(0, 0, 0) },
    { "white",  MKRGB(255,255,255) },
    { "gray",   MKRGB(128,128,128) },
    { "red",    MKRGB(255,0,0) },
    { "green",  MKRGB(0,128,0) },
    { "blue",   MKRGB(0,0,255) },
    { "transparent", MKARGB(0,0,0,0) },
    { "yellow", MKRGB(255,255,0) },
};

// Parses css-like color formats:
// #rgb, #rrggbb, rgb(r,g,b), rgba(r,g,b,a)
// rgb(r%, g%, b%), rgba(r%, g%, b%, a%)
// cf. https://developer.mozilla.org/en/CSS/color_value
static ARGB ParseCssColor(const char *color)
{
    // parse #RRGGBB and #RGB and rgb(R,G,B)
    int a, r, g, b;

    // #rgb is shorthand for #rrggbb
    if (str::Parse(color, "#%1x%1x%1x%$", &r, &g, &b)) {
        r |= (r << 4);
        g |= (g << 4);
        b |= (b << 4);
        return MKRGB(r, g, b);
    }

    if (str::Parse(color, "#%2x%2x%2x%$", &r, &g, &b) ||
        str::Parse(color, "rgb(%d,%d,%d)", &r, &g, &b)) {
        return MKRGB(r, g, b);
    }
    // parse rgba(R,G,B,A)
    if (str::Parse(color, "rgba(%d,%d,%d,%d)", &r, &g, &b, &a)) {
        return MKARGB(a, r, g, b);
    }
    // parse rgb(R%,G%,B%) and rgba(R%,G%,B%,A%)
    float fa = 1.0f, fr, fg, fb;
    if (str::Parse(color, "rgb(%f%%,%f%%,%f%%)", &fr, &fg, &fb) ||
        str::Parse(color, "rgba(%f%%,%f%%,%f%%,%f%%)", &fr, &fg, &fb, &fa)) {
        return MKARGB((int)(fa * 2.55f), (int)(fr * 2.55f), (int)(fg * 2.55f), (int)(fb * 2.55f));
    }
    // parse color names
    for (size_t i = 0; i < dimof(gKnownColors); i++) {
        if (str::EqI(gKnownColors[i].name, color)) {
            return gKnownColors[i].color;
        }
    }
    return MKARGB(0,0,0,0); // transparent
}

bool ColorData::operator==(const ColorData& other) const
{
    if (type != other.type)
        return false;

    if (ColorSolid == type)
        return solid.color == other.solid.color;

    if (ColorGradientLinear == type)
    {
        return (gradientLinear.mode       == other.gradientLinear.mode) &&
               (gradientLinear.startColor == other.gradientLinear.startColor) &&
               (gradientLinear.endColor   == other.gradientLinear.endColor);
    }
    CrashIf(true);
    return false;
}

void Prop::Free()
{
    if (PropFontName == type)
        free((void*)fontName.name);
}

bool Prop::Eq(const Prop *other) const
{
    if (type != other->type)
        return false;

    switch (type) {
    case PropFontName:
        return str::Eq(fontName.name, other->fontName.name);
    case PropFontSize:
        return fontSize.size == other->fontSize.size;
    case PropFontWeight:
        return fontWeight.style == other->fontWeight.style;
    case PropPadding:
        return padding == other->padding;
    case PropTextAlign:
        return align.align == other->align.align;
    }

    if (IsColorProp(type))
        return color == other->color;

    if (IsWidthProp(type))
        return width.width == other->width.width;

    CrashIf(true);
    return false;
}

static Prop *FindExistingProp(Prop *prop)
{
    VecSegmented<Prop>::Iter iter(gAllProps);
    for (Prop *p = iter.Next(); p; p = iter.Next()) {
        if (p->Eq(prop))
            return p;
    }
    return NULL;
}

static Prop *UniqifyProp(Prop& p)
{
    Prop *existing = FindExistingProp(&p);
    if (existing) {
        p.Free();
        return existing;
    }
    return gAllProps->Append(p);
}

Prop *Prop::AllocFontName(const TCHAR *name)
{
    Prop p(PropFontName);
    p.fontName.name = str::Dup(name);
    return UniqifyProp(p);
}

Prop *Prop::AllocFontSize(float size)
{
    Prop p(PropFontSize);
    p.fontSize.size = size;
    return UniqifyProp(p);
}

Prop *Prop::AllocFontWeight(FontStyle style)
{
    Prop p(PropFontWeight);
    p.fontWeight.style = style;
    return UniqifyProp(p);
}

Prop *Prop::AllocWidth(PropType type, float width)
{
    CrashIf(!IsWidthProp(type));
    Prop p(type);
    p.width.width = width;
    return UniqifyProp(p);
}

Prop *Prop::AllocTextAlign(AlignAttr align)
{
    Prop p(PropTextAlign);
    p.align.align = align;
    return UniqifyProp(p);
}

Prop *Prop::AllocPadding(int top, int right, int bottom, int left)
{
    PaddingData pd = { top, right, bottom, left };
    Prop p(PropPadding);
    p.padding = pd;
    return UniqifyProp(p);
}

Prop *Prop::AllocColorSolid(PropType type, ARGB color)
{
    CrashIf(!IsColorProp(type));
    Prop p(type);
    p.color.type = ColorSolid;
    p.color.solid.color = color;
    return UniqifyProp(p);
}

Prop *Prop::AllocColorSolid(PropType type, int a, int r, int g, int b)
{
    return AllocColorSolid(type, MKARGB(a, r, g, b));
}

Prop *Prop::AllocColorSolid(PropType type, int r, int g, int b)
{
    return AllocColorSolid(type, MKARGB(0xff, r, g, b));
}

Prop *Prop::AllocColorLinearGradient(PropType type, LinearGradientMode mode, ARGB startColor, ARGB endColor)
{
    Prop p(type);
    p.color.type = ColorGradientLinear;
    p.color.gradientLinear.mode = mode;
    p.color.gradientLinear.startColor = startColor;
    p.color.gradientLinear.endColor = endColor;
    return UniqifyProp(p);
}

Prop *Prop::AllocColorLinearGradient(PropType type, LinearGradientMode mode, const char *startColor, const char *endColor)
{
    ARGB c1 = ParseCssColor(startColor);
    ARGB c2 = ParseCssColor(endColor);
    return AllocColorLinearGradient(type, mode, c1, c2);
}

Prop *Prop::AllocColorSolid(PropType type, const char *color)
{
    ARGB col = ParseCssColor(color);
    return AllocColorSolid(type, col);
}

#undef ALLOC_BODY

Style* Style::GetInheritsFrom() const
{
    return inheritsFrom;
}

// Identity is a way to track changes to Style
size_t Style::GetIdentity() const
{
    int identity = gen;
    Style *curr = inheritsFrom;
    while (curr) {
        identity += curr->gen;
        curr = curr->inheritsFrom;
    }
    return identity;
}

// Add a property to a set, if a given PropType doesn't exist,
// replace if a given PropType already exists in the set.
void Style::Set(Prop *prop)
{
    CrashIf(!prop);
    for (size_t i = 0; i < props.Count(); i++) {
        Prop *p = props.At(i);
        if (p->type == prop->type) {
            if (!p->Eq(prop))
                ++gen;
            props.At(i) = prop;
            return;
        }
    }
    props.Append(prop);
    ++gen;
}

void Style::SetBorderWidth(float width)
{
    Set(Prop::AllocWidth(PropBorderTopWidth, width));
    Set(Prop::AllocWidth(PropBorderRightWidth, width));
    Set(Prop::AllocWidth(PropBorderBottomWidth, width));
    Set(Prop::AllocWidth(PropBorderLeftWidth, width));
}

void Style::SetBorderColor(ARGB color)
{
    Set(Prop::AllocColorSolid(PropBorderTopColor, color));
    Set(Prop::AllocColorSolid(PropBorderRightColor, color));
    Set(Prop::AllocColorSolid(PropBorderBottomColor, color));
    Set(Prop::AllocColorSolid(PropBorderLeftColor, color));
}

static bool FoundAllProps(PropToGet *props, size_t propsCount)
{
    for (size_t i = 0; i < propsCount; i++) {
        if (props[i].prop == NULL)
            return false;
    }
    return true;
}

// returns true if set, false if was already set or didn't find the prop
static bool SetPropIfFound(Prop *prop, PropToGet *props, size_t propsCount)
{
    for (size_t i = 0; i < propsCount; i++) {
        if (props[i].type != prop->type)
            continue;
        if (NULL == props[i].prop) {
            props[i].prop = prop;
            return true;
        }
        return false;
    }
    return false;
}

void GetProps(Style *style, PropToGet *props, size_t propsCount)
{
    Prop *p;
    Style *curr = style;
    while (curr) {
        for (size_t i = 0; i < curr->props.Count(); i++) {
            p = curr->props.At(i);
            bool didSet = SetPropIfFound(p, props, propsCount);
            if (didSet && FoundAllProps(props, propsCount))
                return;
        }
        curr = curr->GetInheritsFrom();
    }
}

void GetProps(Style *first, Style *second, PropToGet *props, size_t propsCount)
{
    GetProps(first, props, propsCount);
    GetProps(second, props, propsCount);
}

Prop *GetProp(Style *first, Style *second, PropType type)
{
    PropToGet props[1] = {
        { type, NULL }
    };
    GetProps(first, second, props, dimof(props));
    return props[0].prop;
}

// convenience function: given cached props, get a Font object matching the font
// properties. 
// Caller should not delete the font - it's cached for performance and deleted at exit
// in DeleteCachedFonts()
Font *CachedFontFromCachedProps(Prop **props)
{
    Prop *fontName   = props[PropFontName];
    Prop *fontSize   = props[PropFontSize];
    Prop *fontWeight = props[PropFontWeight];
    CrashIf(!fontName || !fontSize || !fontWeight);
    FontCacheEntry c = { fontName, fontSize, fontWeight, NULL };
    for (size_t i = 0; i < gCachedFonts->Count(); i++) {
        FontCacheEntry c2 = gCachedFonts->At(i);
        if (c2 == c) {
            CrashIf(NULL == c2.font);
            return c2.font;
        }
    }
    c.font = ::new Font(fontName->fontName.name, fontSize->fontSize.size, fontWeight->fontWeight.style);
    gCachedFonts->Append(c);
    return c.font;
}

static size_t GetStyleId(Style *style) {
    if (!style)
        return 0;
    return style->GetIdentity();
}

Prop **CachePropsForStyle(Style *style1, Style *style2)
{
    static PropToGet propsToGet[PropsCount];

    ScopedMuiCritSec muiCs;

    for (size_t i = 0; i < gPropCache->Count(); i++) {
        PropCacheEntry e = gPropCache->At(i);
        if ((e.style1 == style1) && (e.style2 == style2)) {
            if ((e.style1Id == GetStyleId(style1)) &&
                (e.style2Id == GetStyleId(style2))) {
                return e.props;
            }
            // TODO: optimize by updating props in-place
            break;
        }
    }

    for (size_t i = 0; i < PropsCount; i++) {
        propsToGet[i].type = (PropType)i;
        propsToGet[i].prop = NULL;
    }
    GetProps(style1, style2, propsToGet, PropsCount);

    Prop **props = gCachedProps->AllocAtEnd(PropsCount);
    for (size_t i = 0; i < PropsCount; i++) {
        props[i] = propsToGet[i].prop;
        CrashIf(!props[i]);
    }

    PropCacheEntry e = { style1, GetStyleId(style1), style2, GetStyleId(style2), props };
    gPropCache->Append(e);
    return props;
}

} // namespace css
} // namespace mui
