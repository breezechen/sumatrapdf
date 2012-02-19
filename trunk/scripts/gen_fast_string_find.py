#!/usr/bin/python
"""
This script generates the fastest possible C code for the following
function: given a string, see if it belongs to a known set of strings.
If it does, return an enum corresponding to that string.
It can be thought of as internalizing a string.

TODO: this is not actually the fastest possible implementation.
Assuming our set of known strings is "a", "abba2", "bo", the fastest
possible implementation would be along the lines:

int FindTag(char *s, size_t len)
{
    char c = *s++; --len;
    if (c == 'a') {
        if (0 == len) return Tag_A;
        if ((len == 4) && memeq(s, "bba2", 4)) return Tag_Abba2;
    } else if (c == 'b') {
        if ((len == 1) && (*s == 'o')) return Tag_Bo;
    }
    return Tag_NotFound;
}

or:

uint32_t GetUpToFour(char*& s, size_t& len)
{
    CrashIf(0 == len);
    size_t n = 0;
    uint32_t v = *s++; len--;
    while ((n < 3) && (len > 0)) {
        v = v << 8;
        v = v | *s++;
        len--; n++;
    }
    return v;
}

#define V_A 'a'
#define V_BO  (('b' << 8) | 'o'))
#define V_ABBA ...
#define V_2 ...

int FindTag(char *s, size_t len)
{
    uint32_t v = GetUpToFour(s, len);
    if (v == V_A) return Tag_A;
    if (v == V_BO) return Tag_B;
    if (v == V_ABBA) {
        v = GetUpToFour(s, len);
        if (v == V_2) return Tag_Abba2;
     }
     return Tag_NotFound;
}

This code generator isn't smart enough to generate such code yet.
"""

import string
from html_entitites import html_entities

# first letter upper case, rest lower case
def capitalize(s):
    s = s.lower()
    return s[0].upper() + s[1:]

# This list has been generated by instrumenting MobiHtmlParse.cpp
# to dump all tags we see in a mobi file
g_tags_str = "a abbr acronym audio b blockquote body br center code dd div dl dt em font guide h1 h2 h3 h4 h5 head hr html i img li link mbp:pagebreak meta object ol p pagebreak pre reference s small span strike strong style sub sup table td th title tr tt u ul video"
g_attrs_str = "size href color filepos border valign rowspan colspan link vlink style face value bgcolor class id mediarecindex controls recindex title lang clear xmlns xmlns:dc width align height"
g_align_attr_str = "left right center justify"

# array of name/value for css colors, value is what goes inside MKRGB()
# based on https://developer.mozilla.org/en/CSS/color_value
# TODO: add more colors
g_css_colors = [
    ["black",        "0, 0, 0"],
    ["white",        "255,255,255"],
    ["gray",         "128,128,128"],
    ["red",          "255,0,0"],
    ["green",        "0,128,0"],
    ["blue",         "0,0,255"],
    ["transparent",  "0,0,0,0"],
    ["yellow",       "255,255,0"],
    ];

css_colors_c = """
static const char *gCssKnownColorsStrings = "%s";
static ARGB gCssKnownColorsValues[] = { %s };

static bool GetKnownCssColor(const char *name, ARGB& colOut)
{
    int pos = str::FindStrPos(gCssKnownColorsStrings, name, str::Len(name));
    if (-1 == pos)
        return false;
    colOut = gCssKnownColorsValues[pos];
    return true;
}
"""

def mrgb(col):
    els = col.split(",")
    if 3 == len(els): return "MKRGB(%s)" % col
    if 4 == len(els): return "MKARGB(%s)" % col
    assert 0

def gen_css_colors():
    g_css_colors.sort(key=lambda a: a[0])
    names = [v[0] for v in g_css_colors]
    vals = [mrgb(v[1]) for v in g_css_colors]
    names_c = string.join(names, "\\0") + "\\0"
    vals_c = string.join(vals, ", ")
    return css_colors_c % (names_c, vals_c)

html_entities_c = """
// map of entity names to their Unicde codes, based on
// http://en.wikipedia.org/wiki/List_of_XML_and_HTML_character_entity_references
// the order of strings in gHtmlEntityNames corresponds to
// order of Unicode codes in gHtmlEntityCodes
static const char *gHtmlEntityNames = "%s";

static int gHtmlEntityCodes[] = { %s };

// returns -1 if didn't find
static int GetHtmlEntityCode(const char *name, size_t nameLen)
{
    int c = str::FindStrPos(gHtmlEntityNames, name, nameLen);
    if (-1 != c)
        c = gHtmlEntityCodes[c];
    return c;
}
"""

def gen_html_entities():
    html_entities.sort(key=lambda a: a[0])
    names = [v[0].lower() for v in html_entities]
    codes = [str(v[1]) for v in html_entities]
    names_c = string.join(names, "\\0") + "\\0"
    codes_c = string.join(codes, ", ")
    return html_entities_c % (names_c, codes_c)

find_simple_c = """
// enums must match HTML_TAGS_STRINGS order
enum HtmlTag {
    %s
};
#define HTML_TAGS_STRINGS "%s"

// enums must match HTML_ATTRS_STRINGS order
enum HtmlAttr {
    %s
};
#define HTML_ATTRS_STRINGS "%s"

// enums must match ALIGN_ATTRS_STRINGS order
enum AlignAttr {
    %s
};
#define ALIGN_ATTRS_STRINGS "%s"

// strings is an array of 0-separated strings consequitevely laid out
// in memory. This functions find the position of str in this array,
// -1 means not found. The search is case-insensitive
static int FindStrPos(const char *strings, char *str, size_t len)
{
    const char *curr = strings;
    char *end = str + len;
    char firstChar = tolower(*str);
    int n = 0;
    for (;;) {
        // we're at the start of the next tag
        char c = *curr;
        if ((0 == c) || (c > firstChar)) {
            // strings are sorted alphabetically, so we
            // can quit if current str is > tastringg
            return -1;
        }
        char *s = str;
        while (*curr && (s < end)) {
            char c = tolower(*s++);
            if (c != *curr++)
                goto Next;
        }
        if ((s == end) && (0 == *curr))
            return n;
Next:
        while (*curr) {
            ++curr;
        }
        ++curr;
        ++n;
    }
    return -1;
}

HtmlTag FindTag(char *tag, size_t len)
{
    return (HtmlTag)FindStrPos(HTML_TAGS_STRINGS, tag, len);
}

static HtmlAttr FindAttr(char *attr, size_t len)
{
    return (HtmlAttr)FindStrPos(HTML_ATTRS_STRINGS, attr, len);
}

static AlignAttr FindAlignAttr(char *attr, size_t len)
{
    return (AlignAttr)FindStrPos(ALIGN_ATTRS_STRINGS, attr, len);
}
"""

# given e.g. "br", returns "Tag_Br"
def enum_name_from_name(name, prefix):
    name = name.replace("-", ":")
    parts = name.split(":")
    parts = [capitalize(p) for p in parts]
    parts = [prefix] + parts
    return string.join(parts, "_")

def gen_enum_str_list(strings, prefix):
    strings = [t.lower() for t in strings.split(" ")]
    strings.sort()
    strings_c = string.join(strings, "\\0") + "\\0"
    strings.append("last")
    # el[0] is tag, el[1] is 0-based position of the tag
    enums = [(enum_name_from_name(el[0], prefix), el[1]) for el in zip(strings, range(len(strings)))]
    enums = [(prefix + "_NotFound", -1)] + enums
    enum_strings = ["%s = %d" % t for t in enums]
    enums_string = string.join(enum_strings, ",\n    ")
    return (enums_string, strings_c)

def main():
    (tags_enum_str, tags_strings) = gen_enum_str_list(g_tags_str, "Tag")
    (attrs_enum_str, attrs_strings) = gen_enum_str_list(g_attrs_str, "Attr")
    (align_attrs_enum_str, align_strings) = gen_enum_str_list(g_align_attr_str, "Align")

    print(find_simple_c % (tags_enum_str, tags_strings, attrs_enum_str, attrs_strings, align_attrs_enum_str, align_strings))
    print(gen_css_colors())
    print(gen_html_entities())
    #print(tags_str)

if __name__ == "__main__":
    main()
