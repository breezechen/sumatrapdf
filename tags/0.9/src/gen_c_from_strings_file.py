import string
from extract_strings import load_strings_file, strings_file, get_lang_list

def as_utf8(n):
    if n < 0x7f:
        assert 0 # don't pass me those
    if n < 0x7ff:
        y = ((n >> 6) & 0x1f) | 0xC0 # 110yyyyy
        z = (n & 0x3f) | 0x80 # 10zzzzzz
        return [y,z]
    if n < 0xffff:
        x = ((n >> 12) & 0x0f) | 0xE0
        y = ((n >> 6) & 0x3f) | 0x80
        z = (n & 0x3f) | 0x80
        return [x,y,z]
    assert 0 # too lazy to encode those

# TODO: stupid C escaping fails on e.g. \xcbb
def c_hex2(n):
    h = hex(n)[2:]
    if 1 == len(h):
        h = "x0" + h
    else:
        assert 2 == len(h)
        h = "x" + h
    assert 3 == len(h)
    h = "\\" + h
    h = " \"" + h + "\" "
    return h

def c_hex(n):
    h = hex(n)[2:]
    if 1 == len(h):
        h = "x0" + h
    else:
        assert 2 == len(h)
        h = "x" + h
    assert 3 == len(h)
    h = "\\" + h
    return h

def c_oct(n):
    o = oct(n)
    if 1 == len(o):
        return "\\00" + o
    elif 2 == len(o):
        return "\\0" + o
    elif 3 == len(o):
        return "\\" + o
    elif 4 == len(o):
        assert '0' == o[0]
        return "\\" + o[1:]

def cstr_esc(n):
    return c_oct(n)

def c_escape(txt):
    if None == txt:
        return "NULL"
    a = []
    for c in txt:
        if ord(c) <= 0x7f:
            a.append(c)
        else:
            chars = as_utf8(ord(c))
            for c2 in chars:
                a.append(cstr_esc(c2))
    txt = string.join(a,"")
    txt.replace("\"", r'\"')
    txt =  '"%s"' % txt
    return txt

def get_trans_for_lang(strings_dict, keys, lang_arg):
    trans = []
    for k in keys:
        vals = strings_dict[k]
        txt = None
        for v in vals:
            (lang, tr) = v
            if lang_arg == lang:
                txt = tr
        trans.append(txt)
    return trans

# line append
def lapp(l, txt):
    l.append(txt)
    #print txt

def gen_cpp_safe_start(l):
    lapp(l, "")
    lapp(l, "#ifdef __cplusplus")
    lapp(l, "extern \"C\"")
    lapp(l, "{")
    lapp(l, "#endif")
    lapp(l, "")

def gen_cpp_safe_stop(l):
    lapp(l, "")
    lapp(l, "#ifdef __cplusplus")
    lapp(l, "}")
    lapp(l, "#endif")
    lapp(l, "")

def write_lines_to_file(lines, file_name):
    txt = string.join(lines, "\n")
    fo = open(file_name, "wb")
    fo.write(txt)
    fo.close()

def gen_h_code(strings_dict, file_name):
    l = []
    lapp(l, "#ifndef TRANSLATIONS_TXT_H__")
    lapp(l, "#define TRANSLATIONS_TXT_H__")
    gen_cpp_safe_start(l)
    lapp(l, "extern int g_transLangsCount;")
    lapp(l, "extern const char *g_transLangs[];")
    lapp(l, "extern int g_transTranslationsCount;")
    lapp(l, "extern const char *g_transTranslations[];")
    gen_cpp_safe_stop(l)
    lapp(l, "#endif")
    write_lines_to_file(l, file_name)

def gen_c_code(strings_dict, file_name):
    l = []
    default_lang = "en"
    langs = get_lang_list(strings_dict)
    assert default_lang not in langs
    langs = [default_lang] + langs
    langs_count = len(langs)
    lapp(l, "/* Generated by script gen_c_from_strings_file.py")
    lapp(l, "   DO NOT EDIT MANUALLY */")
    lapp(l, "#ifndef NULL")
    lapp(l, "#define NULL 0")
    lapp(l, "#endif")
    gen_cpp_safe_start(l)
    lapp(l, "int g_transLangsCount = %d;" % langs_count)
    translations_count = len(strings_dict)
    lapp(l, "")
    lapp(l, "const char *g_transLangs[] = {")
    langs_c = [c_escape(tmp) for tmp in langs]
    txt = "  %s" % string.join(langs_c, ", ")
    lapp(l, txt)
    lapp(l, "};")
    lapp(l, "")
    lapp(l, "int g_transTranslationsCount = %d;\n" % translations_count)
    lapp(l, "const char *g_transTranslations[] = {")
    keys = strings_dict.keys()
    keys.sort()
    for t in range(langs_count):
        if 0 == t:
            trans = keys
        else:
            lang = langs[t]
            trans = get_trans_for_lang(strings_dict, keys, lang)
        lapp(l, "")
        lapp(l, "  /* Translations for language %s */" % langs[t])
        trans_c = ["  %s," % c_escape(t) for t in trans]
        for t in trans_c:
            lapp(l, t)
    lapp(l, "};")
    gen_cpp_safe_stop(l)
    write_lines_to_file(l, file_name)

def to_idm(iso): return "IDM_LANG_" + iso.upper().replace("-", "_")

def lang_sort_func(x,y):
    # special case: english is first
    if x[0] == "en": return -1
    if y[0] == "en": return 1
    return cmp(x[1], y[1])

def gen_lang_menu_def_h(langs, file_name):
    langs.sort(lang_sort_func)
    l = []
    lapp(l, """
 /* Generated by script gen_c_from_strings_file.py
    DO NOT EDIT MANUALLY */

#ifndef LANG_MENU_DEF_H__
#define LANG_MENU_DEF_H__
""")
    lapp(l, "#define LANGS_COUNT %d" % len(langs))
    lapp(l, "")
    lapp(l, """
typedef struct MenuDef {
    const char *m_title;
    int         m_id;
} MenuDef;

typedef struct LangDef {
    const char* _langName;
    int         _langId;
} LangDef;

extern LangDef g_langs[LANGS_COUNT];
extern MenuDef g_menuDefLang[LANGS_COUNT];
""")
    lapp(l, "#endif")
    write_lines_to_file(l, file_name)

def gen_lang_menu_def_c(langs, file_name):
    langs.sort(lang_sort_func)
    l = []
    lapp(l, """
/* Generated by script gen_c_from_strings_file.py
   DO NOT EDIT MANUALLY */

#include "Resource.h"
#include "LangMenuDef.h"
""")
    lapp(l, "LangDef g_langs[LANGS_COUNT] = {")
    for lang in langs:
        lang_iso = lang[0]
        lang_name = lang[1]
        txt = '    {"' + lang_iso + '", ' + to_idm(lang_iso) + "},"
        lapp(l, txt)

    lapp(l, "};")
    lapp(l, "")

    lapp(l, "MenuDef g_menuDefLang[LANGS_COUNT] = {")
    for lang in langs:
        lang_iso = lang[0]
        lang_name = lang[1]
        txt = '    { ' + c_escape(lang_name) + ', '
        txt += to_idm(lang_iso)
        txt += " },"
        lapp(l, txt)

    lapp(l, "};")
    lapp(l, "")

    write_lines_to_file(l, file_name)

def gen_code(strings_dict, langs, h_file_name, c_file_name):
    gen_h_code(strings_dict, h_file_name)
    gen_c_code(strings_dict, c_file_name)
    gen_lang_menu_def_c(langs, "LangMenuDef.cpp")
    gen_lang_menu_def_h(langs, "LangMenuDef.h")

def main():
    (strings_dict, langs) = load_strings_file(strings_file)
    h_file_name = "translations_txt.h"
    c_file_name = "translations_txt.c"
    gen_code(strings_dict, langs, h_file_name, c_file_name)

if __name__ == "__main__":
    main()

