/* Copyright 2012 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

// This file is auto-generated by generate-htmlparserlookup.py

#include "BaseUtil.h"
#include "HtmlParserLookup.h"

#define CS1(c1)             (c1)
#define CS2(c1, c2)         (CS1(c1) | (c2 << 8))
#define CS3(c1, c2, c3)     (CS2(c1, c2) | (c3 << 16))
#define CS4(c1, c2, c3, c4) (CS3(c1, c2, c3) | (c4 << 24))

#define STR1(s) ((s)[0])
#define STR2(s) (STR1(s) | ((s)[1] << 8))
#define STR3(s) (STR2(s) | ((s)[2] << 16))
#define STR4(s) (STR3(s) | ((s)[3] << 24))

#define STR1i(s) (tolower((s)[0]))
#define STR2i(s) (STR1i(s) | (tolower((s)[1]) << 8))
#define STR3i(s) (STR2i(s) | (tolower((s)[2]) << 16))
#define STR4i(s) (STR3i(s) | (tolower((s)[3]) << 24))

HtmlTag FindHtmlTag(const char *name, size_t len)
{
    uint32_t key = 0 == len ? 0 : 1 == len ? STR1i(name) :
                   2 == len ? STR2i(name) : 3 == len ? STR3i(name) : STR4i(name);
    switch (key) {
    case CS1('a'): return Tag_A;
    case CS4('a','b','b','r'):
        if (4 == len) return Tag_Abbr;
        break;
    case CS4('a','c','r','o'):
        if (7 == len && CS3('n','y','m') == STR3i(name + 4)) return Tag_Acronym;
        break;
    case CS4('a','r','e','a'):
        if (4 == len) return Tag_Area;
        break;
    case CS4('a','u','d','i'):
        if (5 == len && CS1('o') == STR1i(name + 4)) return Tag_Audio;
        break;
    case CS1('b'): return Tag_B;
    case CS4('b','a','s','e'):
        if (4 == len) return Tag_Base;
        if (8 == len && CS4('f','o','n','t') == STR4i(name + 4)) return Tag_Basefont;
        break;
    case CS4('b','l','o','c'):
        if (10 == len && str::EqNI(name + 4, "kquote", 6)) return Tag_Blockquote;
        break;
    case CS4('b','o','d','y'):
        if (4 == len) return Tag_Body;
        break;
    case CS2('b','r'): return Tag_Br;
    case CS4('c','e','n','t'):
        if (6 == len && CS2('e','r') == STR2i(name + 4)) return Tag_Center;
        break;
    case CS4('c','o','d','e'):
        if (4 == len) return Tag_Code;
        break;
    case CS3('c','o','l'): return Tag_Col;
    case CS2('d','d'): return Tag_Dd;
    case CS3('d','i','v'): return Tag_Div;
    case CS2('d','l'): return Tag_Dl;
    case CS2('d','t'): return Tag_Dt;
    case CS2('e','m'): return Tag_Em;
    case CS4('f','o','n','t'):
        if (4 == len) return Tag_Font;
        break;
    case CS4('f','r','a','m'):
        if (5 == len && CS1('e') == STR1i(name + 4)) return Tag_Frame;
        break;
    case CS2('h','1'): return Tag_H1;
    case CS2('h','2'): return Tag_H2;
    case CS2('h','3'): return Tag_H3;
    case CS2('h','4'): return Tag_H4;
    case CS2('h','5'): return Tag_H5;
    case CS2('h','6'): return Tag_H6;
    case CS4('h','e','a','d'):
        if (4 == len) return Tag_Head;
        break;
    case CS2('h','r'): return Tag_Hr;
    case CS4('h','t','m','l'):
        if (4 == len) return Tag_Html;
        break;
    case CS1('i'): return Tag_I;
    case CS4('i','m','a','g'):
        if (5 == len && CS1('e') == STR1i(name + 4)) return Tag_Image;
        break;
    case CS3('i','m','g'): return Tag_Img;
    case CS4('i','n','p','u'):
        if (5 == len && CS1('t') == STR1i(name + 4)) return Tag_Input;
        break;
    case CS2('l','h'): return Tag_Lh;
    case CS2('l','i'): return Tag_Li;
    case CS4('l','i','n','k'):
        if (4 == len) return Tag_Link;
        break;
    case CS4('m','b','p',':'):
        if (13 == len && str::EqNI(name + 4, "pagebreak", 9)) return Tag_Mbp_Pagebreak;
        break;
    case CS4('m','e','t','a'):
        if (4 == len) return Tag_Meta;
        break;
    case CS3('n','a','v'): return Tag_Nav;
    case CS4('o','b','j','e'):
        if (6 == len && CS2('c','t') == STR2i(name + 4)) return Tag_Object;
        break;
    case CS2('o','l'): return Tag_Ol;
    case CS1('p'): return Tag_P;
    case CS4('p','a','g','e'):
        if (9 == len && str::EqNI(name + 4, "break", 5)) return Tag_Pagebreak;
        break;
    case CS4('p','a','r','a'):
        if (5 == len && CS1('m') == STR1i(name + 4)) return Tag_Param;
        break;
    case CS3('p','r','e'): return Tag_Pre;
    case CS1('s'): return Tag_S;
    case CS4('s','e','c','t'):
        if (7 == len && CS3('i','o','n') == STR3i(name + 4)) return Tag_Section;
        break;
    case CS4('s','m','a','l'):
        if (5 == len && CS1('l') == STR1i(name + 4)) return Tag_Small;
        break;
    case CS4('s','p','a','n'):
        if (4 == len) return Tag_Span;
        break;
    case CS4('s','t','r','i'):
        if (6 == len && CS2('k','e') == STR2i(name + 4)) return Tag_Strike;
        break;
    case CS4('s','t','r','o'):
        if (6 == len && CS2('n','g') == STR2i(name + 4)) return Tag_Strong;
        break;
    case CS4('s','t','y','l'):
        if (5 == len && CS1('e') == STR1i(name + 4)) return Tag_Style;
        break;
    case CS3('s','u','b'): return Tag_Sub;
    case CS4('s','u','b','t'):
        if (8 == len && CS4('i','t','l','e') == STR4i(name + 4)) return Tag_Subtitle;
        break;
    case CS3('s','u','p'): return Tag_Sup;
    case CS4('t','a','b','l'):
        if (5 == len && CS1('e') == STR1i(name + 4)) return Tag_Table;
        break;
    case CS2('t','d'): return Tag_Td;
    case CS2('t','h'): return Tag_Th;
    case CS4('t','i','t','l'):
        if (5 == len && CS1('e') == STR1i(name + 4)) return Tag_Title;
        break;
    case CS2('t','r'): return Tag_Tr;
    case CS2('t','t'): return Tag_Tt;
    case CS1('u'): return Tag_U;
    case CS2('u','l'): return Tag_Ul;
    case CS4('v','i','d','e'):
        if (5 == len && CS1('o') == STR1i(name + 4)) return Tag_Video;
        break;
    }
    return Tag_NotFound;
}

bool IsTagSelfClosing(HtmlTag item)
{
    switch (item) {
    case Tag_Area: case Tag_Base: case Tag_Basefont: case Tag_Br:
    case Tag_Col: case Tag_Frame: case Tag_Hr: case Tag_Img:
    case Tag_Input: case Tag_Link: case Tag_Mbp_Pagebreak: case Tag_Meta:
    case Tag_Pagebreak: case Tag_Param:
        return true;
    default:
        return false;
    }
}

bool IsInlineTag(HtmlTag item)
{
    switch (item) {
    case Tag_A: case Tag_Abbr: case Tag_Acronym: case Tag_Audio:
    case Tag_B: case Tag_Code: case Tag_Em: case Tag_Font:
    case Tag_I: case Tag_S: case Tag_Small: case Tag_Span:
    case Tag_Strike: case Tag_Strong: case Tag_Sub: case Tag_Sup:
    case Tag_Tt: case Tag_U: case Tag_Video:
        return true;
    default:
        return false;
    }
}

AlignAttr FindAlignAttr(const char *name, size_t len)
{
    uint32_t key = 0 == len ? 0 : 1 == len ? STR1i(name) :
                   2 == len ? STR2i(name) : 3 == len ? STR3i(name) : STR4i(name);
    switch (key) {
    case CS4('c','e','n','t'):
        if (6 == len && CS2('e','r') == STR2i(name + 4)) return Align_Center;
        break;
    case CS4('j','u','s','t'):
        if (7 == len && CS3('i','f','y') == STR3i(name + 4)) return Align_Justify;
        break;
    case CS4('l','e','f','t'):
        if (4 == len) return Align_Left;
        break;
    case CS4('r','i','g','h'):
        if (5 == len && CS1('t') == STR1i(name + 4)) return Align_Right;
        break;
    }
    return Align_NotFound;
}

// map of entity names to their Unicode runes, cf.
// http://en.wikipedia.org/wiki/List_of_XML_and_HTML_character_entity_references

uint32_t FindHtmlEntityRune(const char *name, size_t len)
{
    uint32_t key = 0 == len ? 0 : 1 == len ? STR1(name) :
                   2 == len ? STR2(name) : 3 == len ? STR3(name) : STR4(name);
    switch (key) {
    case CS4('A','E','l','i'):
        if (5 == len && CS1('g') == STR1(name + 4)) return 198;
        break;
    case CS4('A','a','c','u'):
        if (6 == len && CS2('t','e') == STR2(name + 4)) return 193;
        break;
    case CS4('A','c','i','r'):
        if (5 == len && CS1('c') == STR1(name + 4)) return 194;
        break;
    case CS4('A','g','r','a'):
        if (6 == len && CS2('v','e') == STR2(name + 4)) return 192;
        break;
    case CS4('A','l','p','h'):
        if (5 == len && CS1('a') == STR1(name + 4)) return 913;
        break;
    case CS4('A','r','i','n'):
        if (5 == len && CS1('g') == STR1(name + 4)) return 197;
        break;
    case CS4('A','t','i','l'):
        if (6 == len && CS2('d','e') == STR2(name + 4)) return 195;
        break;
    case CS4('A','u','m','l'):
        if (4 == len) return 196;
        break;
    case CS4('B','e','t','a'):
        if (4 == len) return 914;
        break;
    case CS4('C','c','e','d'):
        if (6 == len && CS2('i','l') == STR2(name + 4)) return 199;
        break;
    case CS3('C','h','i'): return 935;
    case CS4('D','a','g','g'):
        if (6 == len && CS2('e','r') == STR2(name + 4)) return 8225;
        break;
    case CS4('D','e','l','t'):
        if (5 == len && CS1('a') == STR1(name + 4)) return 916;
        break;
    case CS3('E','T','H'): return 208;
    case CS4('E','a','c','u'):
        if (6 == len && CS2('t','e') == STR2(name + 4)) return 201;
        break;
    case CS4('E','c','i','r'):
        if (5 == len && CS1('c') == STR1(name + 4)) return 202;
        break;
    case CS4('E','g','r','a'):
        if (6 == len && CS2('v','e') == STR2(name + 4)) return 200;
        break;
    case CS4('E','p','s','i'):
        if (7 == len && CS3('l','o','n') == STR3(name + 4)) return 917;
        break;
    case CS3('E','t','a'): return 919;
    case CS4('E','u','m','l'):
        if (4 == len) return 203;
        break;
    case CS4('G','a','m','m'):
        if (5 == len && CS1('a') == STR1(name + 4)) return 915;
        break;
    case CS4('I','a','c','u'):
        if (6 == len && CS2('t','e') == STR2(name + 4)) return 205;
        break;
    case CS4('I','c','i','r'):
        if (5 == len && CS1('c') == STR1(name + 4)) return 206;
        break;
    case CS4('I','g','r','a'):
        if (6 == len && CS2('v','e') == STR2(name + 4)) return 204;
        break;
    case CS4('I','o','t','a'):
        if (4 == len) return 921;
        break;
    case CS4('I','u','m','l'):
        if (4 == len) return 207;
        break;
    case CS4('K','a','p','p'):
        if (5 == len && CS1('a') == STR1(name + 4)) return 922;
        break;
    case CS4('L','a','m','b'):
        if (6 == len && CS2('d','a') == STR2(name + 4)) return 923;
        break;
    case CS2('M','u'): return 924;
    case CS4('N','t','i','l'):
        if (6 == len && CS2('d','e') == STR2(name + 4)) return 209;
        break;
    case CS2('N','u'): return 925;
    case CS4('O','E','l','i'):
        if (5 == len && CS1('g') == STR1(name + 4)) return 338;
        break;
    case CS4('O','a','c','u'):
        if (6 == len && CS2('t','e') == STR2(name + 4)) return 211;
        break;
    case CS4('O','c','i','r'):
        if (5 == len && CS1('c') == STR1(name + 4)) return 212;
        break;
    case CS4('O','g','r','a'):
        if (6 == len && CS2('v','e') == STR2(name + 4)) return 210;
        break;
    case CS4('O','m','e','g'):
        if (5 == len && CS1('a') == STR1(name + 4)) return 937;
        break;
    case CS4('O','m','i','c'):
        if (7 == len && CS3('r','o','n') == STR3(name + 4)) return 927;
        break;
    case CS4('O','s','l','a'):
        if (6 == len && CS2('s','h') == STR2(name + 4)) return 216;
        break;
    case CS4('O','t','i','l'):
        if (6 == len && CS2('d','e') == STR2(name + 4)) return 213;
        break;
    case CS4('O','u','m','l'):
        if (4 == len) return 214;
        break;
    case CS3('P','h','i'): return 934;
    case CS2('P','i'): return 928;
    case CS4('P','r','i','m'):
        if (5 == len && CS1('e') == STR1(name + 4)) return 8243;
        break;
    case CS3('P','s','i'): return 936;
    case CS3('R','h','o'): return 929;
    case CS4('S','c','a','r'):
        if (6 == len && CS2('o','n') == STR2(name + 4)) return 352;
        break;
    case CS4('S','i','g','m'):
        if (5 == len && CS1('a') == STR1(name + 4)) return 931;
        break;
    case CS4('T','H','O','R'):
        if (5 == len && CS1('N') == STR1(name + 4)) return 222;
        break;
    case CS3('T','a','u'): return 932;
    case CS4('T','h','e','t'):
        if (5 == len && CS1('a') == STR1(name + 4)) return 920;
        break;
    case CS4('U','a','c','u'):
        if (6 == len && CS2('t','e') == STR2(name + 4)) return 218;
        break;
    case CS4('U','c','i','r'):
        if (5 == len && CS1('c') == STR1(name + 4)) return 219;
        break;
    case CS4('U','g','r','a'):
        if (6 == len && CS2('v','e') == STR2(name + 4)) return 217;
        break;
    case CS4('U','p','s','i'):
        if (7 == len && CS3('l','o','n') == STR3(name + 4)) return 933;
        break;
    case CS4('U','u','m','l'):
        if (4 == len) return 220;
        break;
    case CS2('X','i'): return 926;
    case CS4('Y','a','c','u'):
        if (6 == len && CS2('t','e') == STR2(name + 4)) return 221;
        break;
    case CS4('Y','u','m','l'):
        if (4 == len) return 376;
        break;
    case CS4('Z','e','t','a'):
        if (4 == len) return 918;
        break;
    case CS4('a','a','c','u'):
        if (6 == len && CS2('t','e') == STR2(name + 4)) return 225;
        break;
    case CS4('a','c','i','r'):
        if (5 == len && CS1('c') == STR1(name + 4)) return 226;
        break;
    case CS4('a','c','u','t'):
        if (5 == len && CS1('e') == STR1(name + 4)) return 180;
        break;
    case CS4('a','e','l','i'):
        if (5 == len && CS1('g') == STR1(name + 4)) return 230;
        break;
    case CS4('a','g','r','a'):
        if (6 == len && CS2('v','e') == STR2(name + 4)) return 224;
        break;
    case CS4('a','l','e','f'):
        if (7 == len && CS3('s','y','m') == STR3(name + 4)) return 8501;
        break;
    case CS4('a','l','p','h'):
        if (5 == len && CS1('a') == STR1(name + 4)) return 945;
        break;
    case CS3('a','m','p'): return 38;
    case CS3('a','n','d'): return 8743;
    case CS3('a','n','g'): return 8736;
    case CS4('a','p','o','s'):
        if (4 == len) return 39;
        break;
    case CS4('a','r','i','n'):
        if (5 == len && CS1('g') == STR1(name + 4)) return 229;
        break;
    case CS4('a','s','y','m'):
        if (5 == len && CS1('p') == STR1(name + 4)) return 8776;
        break;
    case CS4('a','t','i','l'):
        if (6 == len && CS2('d','e') == STR2(name + 4)) return 227;
        break;
    case CS4('a','u','m','l'):
        if (4 == len) return 228;
        break;
    case CS4('b','d','q','u'):
        if (5 == len && CS1('o') == STR1(name + 4)) return 8222;
        break;
    case CS4('b','e','t','a'):
        if (4 == len) return 946;
        break;
    case CS4('b','r','v','b'):
        if (6 == len && CS2('a','r') == STR2(name + 4)) return 166;
        break;
    case CS4('b','u','l','l'):
        if (4 == len) return 8226;
        break;
    case CS3('c','a','p'): return 8745;
    case CS4('c','c','e','d'):
        if (6 == len && CS2('i','l') == STR2(name + 4)) return 231;
        break;
    case CS4('c','e','d','i'):
        if (5 == len && CS1('l') == STR1(name + 4)) return 184;
        break;
    case CS4('c','e','n','t'):
        if (4 == len) return 162;
        break;
    case CS3('c','h','i'): return 967;
    case CS4('c','i','r','c'):
        if (4 == len) return 710;
        break;
    case CS4('c','l','u','b'):
        if (5 == len && CS1('s') == STR1(name + 4)) return 9827;
        break;
    case CS4('c','o','n','g'):
        if (4 == len) return 8773;
        break;
    case CS4('c','o','p','y'):
        if (4 == len) return 169;
        break;
    case CS4('c','r','a','r'):
        if (5 == len && CS1('r') == STR1(name + 4)) return 8629;
        break;
    case CS3('c','u','p'): return 8746;
    case CS4('c','u','r','r'):
        if (6 == len && CS2('e','n') == STR2(name + 4)) return 164;
        break;
    case CS4('d','A','r','r'):
        if (4 == len) return 8659;
        break;
    case CS4('d','a','g','g'):
        if (6 == len && CS2('e','r') == STR2(name + 4)) return 8224;
        break;
    case CS4('d','a','r','r'):
        if (4 == len) return 8595;
        break;
    case CS3('d','e','g'): return 176;
    case CS4('d','e','l','t'):
        if (5 == len && CS1('a') == STR1(name + 4)) return 948;
        break;
    case CS4('d','i','a','m'):
        if (5 == len && CS1('s') == STR1(name + 4)) return 9830;
        break;
    case CS4('d','i','v','i'):
        if (6 == len && CS2('d','e') == STR2(name + 4)) return 247;
        break;
    case CS4('e','a','c','u'):
        if (6 == len && CS2('t','e') == STR2(name + 4)) return 233;
        break;
    case CS4('e','c','i','r'):
        if (5 == len && CS1('c') == STR1(name + 4)) return 234;
        break;
    case CS4('e','g','r','a'):
        if (6 == len && CS2('v','e') == STR2(name + 4)) return 232;
        break;
    case CS4('e','m','p','t'):
        if (5 == len && CS1('y') == STR1(name + 4)) return 8709;
        break;
    case CS4('e','m','s','p'):
        if (4 == len) return 8195;
        break;
    case CS4('e','n','s','p'):
        if (4 == len) return 8194;
        break;
    case CS4('e','p','s','i'):
        if (7 == len && CS3('l','o','n') == STR3(name + 4)) return 949;
        break;
    case CS4('e','q','u','i'):
        if (5 == len && CS1('v') == STR1(name + 4)) return 8801;
        break;
    case CS3('e','t','a'): return 951;
    case CS3('e','t','h'): return 240;
    case CS4('e','u','m','l'):
        if (4 == len) return 235;
        break;
    case CS4('e','u','r','o'):
        if (4 == len) return 8364;
        break;
    case CS4('e','x','i','s'):
        if (5 == len && CS1('t') == STR1(name + 4)) return 8707;
        break;
    case CS4('f','n','o','f'):
        if (4 == len) return 402;
        break;
    case CS4('f','o','r','a'):
        if (6 == len && CS2('l','l') == STR2(name + 4)) return 8704;
        break;
    case CS4('f','r','a','c'):
        if (6 == len && CS2('1','2') == STR2(name + 4)) return 189;
        if (6 == len && CS2('1','4') == STR2(name + 4)) return 188;
        if (6 == len && CS2('3','4') == STR2(name + 4)) return 190;
        break;
    case CS4('f','r','a','s'):
        if (5 == len && CS1('l') == STR1(name + 4)) return 8260;
        break;
    case CS4('g','a','m','m'):
        if (5 == len && CS1('a') == STR1(name + 4)) return 947;
        break;
    case CS2('g','e'): return 8805;
    case CS2('g','t'): return 62;
    case CS4('h','A','r','r'):
        if (4 == len) return 8660;
        break;
    case CS4('h','a','r','r'):
        if (4 == len) return 8596;
        break;
    case CS4('h','e','a','r'):
        if (6 == len && CS2('t','s') == STR2(name + 4)) return 9829;
        break;
    case CS4('h','e','l','l'):
        if (6 == len && CS2('i','p') == STR2(name + 4)) return 8230;
        break;
    case CS4('i','a','c','u'):
        if (6 == len && CS2('t','e') == STR2(name + 4)) return 237;
        break;
    case CS4('i','c','i','r'):
        if (5 == len && CS1('c') == STR1(name + 4)) return 238;
        break;
    case CS4('i','e','x','c'):
        if (5 == len && CS1('l') == STR1(name + 4)) return 161;
        break;
    case CS4('i','g','r','a'):
        if (6 == len && CS2('v','e') == STR2(name + 4)) return 236;
        break;
    case CS4('i','m','a','g'):
        if (5 == len && CS1('e') == STR1(name + 4)) return 8465;
        break;
    case CS4('i','n','f','i'):
        if (5 == len && CS1('n') == STR1(name + 4)) return 8734;
        break;
    case CS3('i','n','t'): return 8747;
    case CS4('i','o','t','a'):
        if (4 == len) return 953;
        break;
    case CS4('i','q','u','e'):
        if (6 == len && CS2('s','t') == STR2(name + 4)) return 191;
        break;
    case CS4('i','s','i','n'):
        if (4 == len) return 8712;
        break;
    case CS4('i','u','m','l'):
        if (4 == len) return 239;
        break;
    case CS4('k','a','p','p'):
        if (5 == len && CS1('a') == STR1(name + 4)) return 954;
        break;
    case CS4('l','A','r','r'):
        if (4 == len) return 8656;
        break;
    case CS4('l','a','m','b'):
        if (6 == len && CS2('d','a') == STR2(name + 4)) return 955;
        break;
    case CS4('l','a','n','g'):
        if (4 == len) return 9001;
        break;
    case CS4('l','a','q','u'):
        if (5 == len && CS1('o') == STR1(name + 4)) return 171;
        break;
    case CS4('l','a','r','r'):
        if (4 == len) return 8592;
        break;
    case CS4('l','c','e','i'):
        if (5 == len && CS1('l') == STR1(name + 4)) return 8968;
        break;
    case CS4('l','d','q','u'):
        if (5 == len && CS1('o') == STR1(name + 4)) return 8220;
        break;
    case CS2('l','e'): return 8804;
    case CS4('l','f','l','o'):
        if (6 == len && CS2('o','r') == STR2(name + 4)) return 8970;
        break;
    case CS4('l','o','w','a'):
        if (6 == len && CS2('s','t') == STR2(name + 4)) return 8727;
        break;
    case CS3('l','o','z'): return 9674;
    case CS3('l','r','m'): return 8206;
    case CS4('l','s','a','q'):
        if (6 == len && CS2('u','o') == STR2(name + 4)) return 8249;
        break;
    case CS4('l','s','q','u'):
        if (5 == len && CS1('o') == STR1(name + 4)) return 8216;
        break;
    case CS2('l','t'): return 60;
    case CS4('m','a','c','r'):
        if (4 == len) return 175;
        break;
    case CS4('m','d','a','s'):
        if (5 == len && CS1('h') == STR1(name + 4)) return 8212;
        break;
    case CS4('m','i','c','r'):
        if (5 == len && CS1('o') == STR1(name + 4)) return 181;
        break;
    case CS4('m','i','d','d'):
        if (6 == len && CS2('o','t') == STR2(name + 4)) return 183;
        break;
    case CS4('m','i','n','u'):
        if (5 == len && CS1('s') == STR1(name + 4)) return 8722;
        break;
    case CS2('m','u'): return 956;
    case CS4('n','a','b','l'):
        if (5 == len && CS1('a') == STR1(name + 4)) return 8711;
        break;
    case CS4('n','b','s','p'):
        if (4 == len) return 160;
        break;
    case CS4('n','d','a','s'):
        if (5 == len && CS1('h') == STR1(name + 4)) return 8211;
        break;
    case CS2('n','e'): return 8800;
    case CS2('n','i'): return 8715;
    case CS3('n','o','t'): return 172;
    case CS4('n','o','t','i'):
        if (5 == len && CS1('n') == STR1(name + 4)) return 8713;
        break;
    case CS4('n','s','u','b'):
        if (4 == len) return 8836;
        break;
    case CS4('n','t','i','l'):
        if (6 == len && CS2('d','e') == STR2(name + 4)) return 241;
        break;
    case CS2('n','u'): return 957;
    case CS4('o','a','c','u'):
        if (6 == len && CS2('t','e') == STR2(name + 4)) return 243;
        break;
    case CS4('o','c','i','r'):
        if (5 == len && CS1('c') == STR1(name + 4)) return 244;
        break;
    case CS4('o','e','l','i'):
        if (5 == len && CS1('g') == STR1(name + 4)) return 339;
        break;
    case CS4('o','g','r','a'):
        if (6 == len && CS2('v','e') == STR2(name + 4)) return 242;
        break;
    case CS4('o','l','i','n'):
        if (5 == len && CS1('e') == STR1(name + 4)) return 8254;
        break;
    case CS4('o','m','e','g'):
        if (5 == len && CS1('a') == STR1(name + 4)) return 969;
        break;
    case CS4('o','m','i','c'):
        if (7 == len && CS3('r','o','n') == STR3(name + 4)) return 959;
        break;
    case CS4('o','p','l','u'):
        if (5 == len && CS1('s') == STR1(name + 4)) return 8853;
        break;
    case CS2('o','r'): return 8744;
    case CS4('o','r','d','f'):
        if (4 == len) return 170;
        break;
    case CS4('o','r','d','m'):
        if (4 == len) return 186;
        break;
    case CS4('o','s','l','a'):
        if (6 == len && CS2('s','h') == STR2(name + 4)) return 248;
        break;
    case CS4('o','t','i','l'):
        if (6 == len && CS2('d','e') == STR2(name + 4)) return 245;
        break;
    case CS4('o','t','i','m'):
        if (6 == len && CS2('e','s') == STR2(name + 4)) return 8855;
        break;
    case CS4('o','u','m','l'):
        if (4 == len) return 246;
        break;
    case CS4('p','a','r','a'):
        if (4 == len) return 182;
        break;
    case CS4('p','a','r','t'):
        if (4 == len) return 8706;
        break;
    case CS4('p','e','r','m'):
        if (6 == len && CS2('i','l') == STR2(name + 4)) return 8240;
        break;
    case CS4('p','e','r','p'):
        if (4 == len) return 8869;
        break;
    case CS3('p','h','i'): return 966;
    case CS2('p','i'): return 960;
    case CS3('p','i','v'): return 982;
    case CS4('p','l','u','s'):
        if (6 == len && CS2('m','n') == STR2(name + 4)) return 177;
        break;
    case CS4('p','o','u','n'):
        if (5 == len && CS1('d') == STR1(name + 4)) return 163;
        break;
    case CS4('p','r','i','m'):
        if (5 == len && CS1('e') == STR1(name + 4)) return 8242;
        break;
    case CS4('p','r','o','d'):
        if (4 == len) return 8719;
        break;
    case CS4('p','r','o','p'):
        if (4 == len) return 8733;
        break;
    case CS3('p','s','i'): return 968;
    case CS4('q','u','o','t'):
        if (4 == len) return 34;
        break;
    case CS4('r','A','r','r'):
        if (4 == len) return 8658;
        break;
    case CS4('r','a','d','i'):
        if (5 == len && CS1('c') == STR1(name + 4)) return 8730;
        break;
    case CS4('r','a','n','g'):
        if (4 == len) return 9002;
        break;
    case CS4('r','a','q','u'):
        if (5 == len && CS1('o') == STR1(name + 4)) return 187;
        break;
    case CS4('r','a','r','r'):
        if (4 == len) return 8594;
        break;
    case CS4('r','c','e','i'):
        if (5 == len && CS1('l') == STR1(name + 4)) return 8969;
        break;
    case CS4('r','d','q','u'):
        if (5 == len && CS1('o') == STR1(name + 4)) return 8221;
        break;
    case CS4('r','e','a','l'):
        if (4 == len) return 8476;
        break;
    case CS3('r','e','g'): return 174;
    case CS4('r','f','l','o'):
        if (6 == len && CS2('o','r') == STR2(name + 4)) return 8971;
        break;
    case CS3('r','h','o'): return 961;
    case CS3('r','l','m'): return 8207;
    case CS4('r','s','a','q'):
        if (6 == len && CS2('u','o') == STR2(name + 4)) return 8250;
        break;
    case CS4('r','s','q','u'):
        if (5 == len && CS1('o') == STR1(name + 4)) return 8217;
        break;
    case CS4('s','b','q','u'):
        if (5 == len && CS1('o') == STR1(name + 4)) return 8218;
        break;
    case CS4('s','c','a','r'):
        if (6 == len && CS2('o','n') == STR2(name + 4)) return 353;
        break;
    case CS4('s','d','o','t'):
        if (4 == len) return 8901;
        break;
    case CS4('s','e','c','t'):
        if (4 == len) return 167;
        break;
    case CS3('s','h','y'): return 173;
    case CS4('s','i','g','m'):
        if (5 == len && CS1('a') == STR1(name + 4)) return 963;
        if (6 == len && CS2('a','f') == STR2(name + 4)) return 962;
        break;
    case CS3('s','i','m'): return 8764;
    case CS4('s','p','a','d'):
        if (6 == len && CS2('e','s') == STR2(name + 4)) return 9824;
        break;
    case CS3('s','u','b'): return 8834;
    case CS4('s','u','b','e'):
        if (4 == len) return 8838;
        break;
    case CS3('s','u','m'): return 8721;
    case CS3('s','u','p'): return 8835;
    case CS4('s','u','p','1'):
        if (4 == len) return 185;
        break;
    case CS4('s','u','p','2'):
        if (4 == len) return 178;
        break;
    case CS4('s','u','p','3'):
        if (4 == len) return 179;
        break;
    case CS4('s','u','p','e'):
        if (4 == len) return 8839;
        break;
    case CS4('s','z','l','i'):
        if (5 == len && CS1('g') == STR1(name + 4)) return 223;
        break;
    case CS3('t','a','u'): return 964;
    case CS4('t','h','e','r'):
        if (6 == len && CS2('e','4') == STR2(name + 4)) return 8756;
        break;
    case CS4('t','h','e','t'):
        if (5 == len && CS1('a') == STR1(name + 4)) return 952;
        if (8 == len && CS4('a','s','y','m') == STR4(name + 4)) return 977;
        break;
    case CS4('t','h','i','n'):
        if (6 == len && CS2('s','p') == STR2(name + 4)) return 8201;
        break;
    case CS4('t','h','o','r'):
        if (5 == len && CS1('n') == STR1(name + 4)) return 254;
        break;
    case CS4('t','i','l','d'):
        if (5 == len && CS1('e') == STR1(name + 4)) return 732;
        break;
    case CS4('t','i','m','e'):
        if (5 == len && CS1('s') == STR1(name + 4)) return 215;
        break;
    case CS4('t','r','a','d'):
        if (5 == len && CS1('e') == STR1(name + 4)) return 8482;
        break;
    case CS4('u','A','r','r'):
        if (4 == len) return 8657;
        break;
    case CS4('u','a','c','u'):
        if (6 == len && CS2('t','e') == STR2(name + 4)) return 250;
        break;
    case CS4('u','a','r','r'):
        if (4 == len) return 8593;
        break;
    case CS4('u','c','i','r'):
        if (5 == len && CS1('c') == STR1(name + 4)) return 251;
        break;
    case CS4('u','g','r','a'):
        if (6 == len && CS2('v','e') == STR2(name + 4)) return 249;
        break;
    case CS3('u','m','l'): return 168;
    case CS4('u','p','s','i'):
        if (5 == len && CS1('h') == STR1(name + 4)) return 978;
        if (7 == len && CS3('l','o','n') == STR3(name + 4)) return 965;
        break;
    case CS4('u','u','m','l'):
        if (4 == len) return 252;
        break;
    case CS4('w','e','i','e'):
        if (6 == len && CS2('r','p') == STR2(name + 4)) return 8472;
        break;
    case CS2('x','i'): return 958;
    case CS4('y','a','c','u'):
        if (6 == len && CS2('t','e') == STR2(name + 4)) return 253;
        break;
    case CS3('y','e','n'): return 165;
    case CS4('y','u','m','l'):
        if (4 == len) return 255;
        break;
    case CS4('z','e','t','a'):
        if (4 == len) return 950;
        break;
    case CS3('z','w','j'): return 8205;
    case CS4('z','w','n','j'):
        if (4 == len) return 8204;
        break;
    }
    return -1;
}
