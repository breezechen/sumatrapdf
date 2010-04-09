from extract_strings import load_strings_file_old, load_strings_file_new, get_lang_list, untranslated_count_for_lang, extract_strings_from_c_files, dump_missing_per_language, write_out_strings_files
import simplejson

g_can_upload = True

try:
    import boto.s3
    from boto.s3.key import Key
except:
    print("You need boto library (http://code.google.com/p/boto/)")
    print("svn checkout http://boto.googlecode.com/svn/trunk/ boto")
    print("cd boto; python setup.py install")
    g_can_upload = False

try:
    import awscreds
except:
    print("awscreds.py not present")
    g_can_upload = False

S3_BUCKET = "kjkpub"
S3_JS_NAME = "blog/sumatrapdf-langs.js"
g_s3conn = None

def s3connection():
  global g_s3conn
  if g_s3conn is None:
    g_s3conn = boto.s3.connection.S3Connection(awscreds.access, awscreds.secret, True)
  return g_s3conn

def s3PubBucket(): return s3connection().get_bucket(S3_BUCKET)

def ul_cb(sofar, total):
    print("So far: %d, total: %d" % (sofar , total))

def s3UploadFilePublic(local_file_name, remote_file_name):
    bucket = s3PubBucket()
    k = Key(bucket)
    k.key = remote_file_name
    k.set_contents_from_filename(local_file_name, cb=ul_cb)
    k.make_public()

def s3UploadDataPublic(data, remote_file_name):
    print("Uploading data to s3 as '%s'" % remote_file_name)
    bucket = s3PubBucket()
    k = Key(bucket)
    k.key = remote_file_name
    k.set_contents_from_string(data)
    k.make_public()

### file templates ###

TRANSLATIONS_TXT_H = """\
#ifndef TRANSLATIONS_TXT_H__
#define TRANSLATIONS_TXT_H__

#ifdef __cplusplus
extern "C"
{
#endif

extern int g_transLangsCount;
extern const char *g_transLangs[];
extern int g_transTranslationsCount;
extern const char *g_transTranslations[];

#ifdef __cplusplus
}
#endif

#endif
"""

TRANSLATIONS_TXT_C = """\
/* Generated by script gen_c_from_strings_file.py
   DO NOT EDIT MANUALLY */
#ifndef NULL
#define NULL 0
#endif

#ifdef __cplusplus
extern "C"
{
#endif

int g_transLangsCount = %(langs_count)d;

const char *g_transLangs[] = {
  %(langs_c)s
};

int g_transTranslationsCount = %(translations_count)d;

const char *g_transTranslations[] = {
%(translations)s
};

#ifdef __cplusplus
}
#endif

"""

LANG_MENU_DEF_H = """
/* Generated by script gen_c_from_.py
    DO NOT EDIT MANUALLY */

#ifndef LANG_MENU_DEF_H__
#define LANG_MENU_DEF_H__

#define LANGS_COUNT %(langs_count)d


typedef struct MenuDef {
    const char *m_title;
    int         m_id;
    int         m_flags;
} MenuDef;

typedef struct LangDef {
    const char* _langName;
    int         _langId;
} LangDef;

extern LangDef g_langs[LANGS_COUNT];
extern MenuDef g_menuDefLang[LANGS_COUNT];

#endif
"""

LANG_MENU_DEF_C = """
/* Generated by script gen_c_from_strings_file.py
   DO NOT EDIT MANUALLY */

#include "Resource.h"
#include "LangMenuDef.h"

LangDef g_langs[LANGS_COUNT] = {
    %(lang_codes)s
};

MenuDef g_menuDefLang[LANGS_COUNT] = {
    %(lang_names)s
};
"""

### end of file templates ###

# TODO: stupid C escaping fails on e.g. \xcbb
def c_hex(c):
    return r'\x' + hex(ord(c))[2:].upper()

def c_oct(c):
    o = "0" + oct(ord(c))
    return "\\" + o[-3:]

def c_escape(txt):
    if txt is None:
        return "NULL"
    a = []
    for c in txt:
        c = c.encode("utf-8")
        if c == '"':
            a.append(r'\"')
        elif len(c) == 1:
            a.append(c)
        else:
            a += [c_oct(c2) for c2 in c]
    return '"%s"' % "".join(a)

def get_trans_for_lang(strings_dict, keys, lang_arg):
    trans = []
    for k in keys:
        txt = None
        for (lang, tr) in strings_dict[k]:
            if lang_arg == lang:
                # don't include a translation, if it's the same as the default
                if tr != k: txt = tr
                break
        trans.append(txt)
    return trans

def gen_h_code(strings_dict, file_name):
    file_content = TRANSLATIONS_TXT_H % locals()
    file(file_name, "wb").write(file_content)

LANG_ORDER = [
    "en", "am", "af", "pt", "br", "bg", "de", "tr", "by", 
    "hu", "lt", "my", "ja", "fa", "it", "nl", "fi", "ca", 
    "sl", "sr-rs", "ml", "he", "sp-rs", "id", "mk", "ro", 
    "sk", "vn", "kr", "bn", "cn", "fr", "ru", "gl", "es", 
    "ar", "uk", "eu", "gr", "hr", "tl", "va", "sn", "tw", 
    "cy", "ga", "mm", "pa", "hi", "nn", "sv", "pl", "dk", 
    "ta", "cz", "th", "no", "kw", "fy-nl"
]

# This is just to make the order the same as the old code that was parsing
# just one translation file, to avoid a diff in generted c code when switching
# from the old to the new method
def fix_langs_order(langs):
    return sorted(langs, lambda x,y: cmp(LANG_ORDER.index(x), LANG_ORDER.index(y)))

def gen_c_code(strings_dict, file_name):
    default_lang = "en"
    langs = get_lang_list(strings_dict)
    langs = fix_langs_order(langs)
    assert default_lang not in langs
    langs = [default_lang] + langs
    langs_count = len(langs)
    translations_count = len(strings_dict)
    langs_c = [c_escape(tmp) for tmp in langs]
    langs_c = ", ".join(langs_c)
    
    keys = strings_dict.keys()
    keys.sort()
    lines = []
    for lang in langs:
        if "en" == lang:
            trans = keys
        else:
            trans = get_trans_for_lang(strings_dict, keys, lang)
        lines.append("")
        lines.append("  /* Translations for language %s */" % lang)
        lines += ["  %s," % c_escape(t) for t in trans]
    translations = "\n".join(lines)
    
    file_content = TRANSLATIONS_TXT_C % locals()
    file(file_name, "wb").write(file_content)

def to_idm(iso): return "IDM_LANG_" + iso.upper().replace("-", "_")

def lang_sort_func(x,y):
    # special case: english is first
    if x[0] == "en": return -1
    if y[0] == "en": return 1
    return cmp(x[1], y[1])

def gen_lang_menu_def_h(langs, file_name):
    langs_count = len(langs)
    file_content = LANG_MENU_DEF_H % locals()
    file(file_name, "wb").write(file_content)

def gen_lang_menu_def_c(langs, file_name):
    langs.sort(lang_sort_func)
    lang_codes = ['{"%s", %s},' % (lang[0], to_idm(lang[0])) for lang in langs]
    lang_codes = "\n    ".join(lang_codes)
    
    lang_names = ['{ %s, %s },' % (c_escape(lang[1]), to_idm(lang[0])) for lang in langs]
    lang_names = "\n    ".join(lang_names)

    file_content = LANG_MENU_DEF_C % locals()
    file(file_name, "wb").write(file_content)

def gen_code(strings_dict, langs, h_file_name, c_file_name):
    gen_h_code(strings_dict, h_file_name)
    gen_c_code(strings_dict, c_file_name)
    gen_lang_menu_def_c(langs, "LangMenuDef.cpp")
    gen_lang_menu_def_h(langs, "LangMenuDef.h")

def contributors_for_lang(contributors, lang):
    return sorted(contributors.get(lang, []))

def gen_js_data(strings_dict, langs, contributors):
    res = []
    for (lang_iso, lang_name) in langs:
        if "en" == lang_iso: continue
        lang_name = lang_name.split(" (")[0]
        count = untranslated_count_for_lang(strings_dict, lang_iso)
        svnurl = "http://sumatrapdf.googlecode.com/svn/trunk/src/strings-" + lang_iso + ".txt"
        c = contributors_for_lang(contributors, lang_iso)
        res.append([lang_iso, lang_name, count, svnurl, c])
    return sorted(res, lambda x,y: cmp(y[2], x[2]))

# Generate json data as array of arrays in the format:
# [langname, lang-iso-code, untranslated_strings_count, svn_url, [contributors]]
# sorted by untranslated string count (biggest at the top)
def gen_and_upload_js(strings_dict, langs, contributors):
    global g_can_upload
    if not g_can_upload:
        print("Can't upload javascript to s3")
        return
    data = gen_js_data(strings_dict, langs, contributors)
    js = simplejson.dumps(data)
    js = "var g_langsData = " + js + ";\n"
    #print(js)
    s3UploadDataPublic(js, S3_JS_NAME)

def get_untranslated_as_list(untranslated_dict):
    untranslated = []
    for l in untranslated_dict.values():
        for el in l:
            if el not in untranslated:
                untranslated.append(el)
    return untranslated

def main():
    #(strings_dict, langs) = load_strings_file_old()
    (strings_dict, langs, contributors) = load_strings_file_new()
    strings = extract_strings_from_c_files()
    untranslated_dict = dump_missing_per_language(strings, strings_dict)
    untranslated = get_untranslated_as_list(untranslated_dict)
    write_out_strings_files(strings_dict, langs, contributors, untranslated_dict)
    for s in untranslated:
        if s not in strings_dict:
            strings_dict[s] = []
    h_file_name = "translations_txt.h"
    c_file_name = "translations_txt.c"
    gen_code(strings_dict, langs, h_file_name, c_file_name)
    gen_and_upload_js(strings_dict, langs, contributors)

if __name__ == "__main__":
    main()

