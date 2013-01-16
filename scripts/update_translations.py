import os.path, re, simplejson
from util import *
from extract_strings import load_strings_file, untranslated_count_for_lang
from extract_strings import extract_strings_from_c_files, get_missing_for_language
from extract_strings import dump_missing_per_language, write_out_strings_files
from extract_strings import key_sort_func, load_lang_index

g_can_upload = False
g_src_dir = os.path.join(os.path.split(__file__)[0], "..", "src")

config = load_config()
if not config.HasAwsCreds():
    print("aws creds not present in config.py")
else:
    try:
        import boto.s3
        from boto.s3.key import Key
        g_can_upload = True
    except:
        print("You need boto library (http://code.google.com/p/boto/)")
        print("svn checkout http://boto.googlecode.com/svn/trunk/ boto")
        print("cd boto; python setup.py install")

S3_JS_NAME = "blog/sumatrapdf-langs.js"
# number of missing translations for a language to be considered
# incomplete (will be excluded from Translations_txt.cpp)
INCOMPLETE_MISSING_THRESHOLD = 40

DEFAULT_LANG = "en"

TRANSLATIONS_TXT_C = """\
/* Generated by scripts\\update_translations.py
   DO NOT EDIT MANUALLY */

#ifndef MAKELANGID
#include <windows.h>
#endif

// from http://msdn.microsoft.com/en-us/library/windows/desktop/dd318693(v=vs.85).aspx
// those definition are not present in 7.0A SDK my VS 2010 uses
#ifndef LANG_CENTRAL_KURDISH
#define LANG_CENTRAL_KURDISH 0x92
#endif

#ifndef SUBLANG_CENTRAL_KURDISH_CENTRAL_KURDISH_IRAQ
#define SUBLANG_CENTRAL_KURDISH_CENTRAL_KURDISH_IRAQ 0x01
#endif

#define LANGS_COUNT   %(langs_count)d
#define STRINGS_COUNT %(translations_count)d

typedef struct {
    const char *code;
    const char *fullName;
    LANGID id;
    BOOL isRTL;
} LangDef;

#define _LANGID(lang) MAKELANGID(lang, SUBLANG_NEUTRAL)

LangDef gLangData[LANGS_COUNT] = {
    %(lang_data)s
};

#undef _LANGID

const char *gTranslations[LANGS_COUNT * STRINGS_COUNT] = {
%(translations)s
};
"""

# use octal escapes because hexadecimal ones can consist of
# up to four characters, e.g. \xABc isn't the same as \253c
def c_oct(c):
    o = "00" + oct(ord(c))
    return "\\" + o[-3:]

def c_escape(txt, encode_to_utf=False):
    if txt is None:
        return "NULL"
    # the old, pre-apptranslator translation system required encoding to utf8
    if encode_to_utf:
        txt = txt.encode("utf-8")
    # escape all quotes
    txt = txt.replace('"', r'\"')
    # and all non-7-bit characters of the UTF-8 encoded string
    txt = re.sub(r"[\x80-\xFF]", lambda m: c_oct(m.group(0)[0]), txt)
    return '"%s"' % txt

def get_trans_for_lang(strings_dict, keys, lang_arg):
    trans = []
    for k in keys:
        txt = None
        for (lang, tr) in strings_dict[k]:
            if lang_arg == lang:
                # don't include a translation, if it's the same as the default
                if tr != k:
                    txt = tr
                break
        trans.append(txt)
    return trans

def lang_sort_func(x,y):
    # special case: default language is first
    if x[0] == DEFAULT_LANG: return -1
    if y[0] == DEFAULT_LANG: return 1
    return cmp(x[1], y[1])

def make_lang_id(lang):
    ids = lang[2]
    if ids is None:
        return "-1"
    ids = [el.replace(" ", "_") for el in ids]
    if len(ids) == 2:
        id = "MAKELANGID(LANG_%s, SUBLANG_%s_%s)" % (ids[0], ids[0], ids[1])
    else:
        assert len(ids) == 1
        id = "_LANGID(LANG_%s)" % (ids[0])
    return id.upper()

def is_rtl_lang(lang):
    return len(lang) > 3 and lang[3] == "RTL"

def gen_c_code(langs_idx, strings_dict, file_name, encode_to_utf=False):
    langs_idx = sorted(langs_idx, cmp=lang_sort_func)
    assert DEFAULT_LANG == langs_idx[0][0]
    langs_count = len(langs_idx)
    translations_count = len(strings_dict)

    keys = strings_dict.keys()
    keys.sort(cmp=key_sort_func)
    lines = []
    for lang in langs_idx:
        if DEFAULT_LANG == lang[0]:
            trans = keys
        else:
            trans = get_trans_for_lang(strings_dict, keys, lang[0])
        lines.append("")
        lines.append("  /* Translations for language %s */" % lang[0])
        lines += ["  %s," % c_escape(t, encode_to_utf) for t in trans]
    translations = "\n".join(lines)
    #print [l[1] for l in langs_idx]
    lang_data = ['{ "%s", %s, %s, %d },' % (lang[0], c_escape(lang[1]), make_lang_id(lang), 1 if is_rtl_lang(lang) else 0) for lang in langs_idx]
    lang_data = "\n    ".join(lang_data)

    file_content = TRANSLATIONS_TXT_C % locals()
    file(file_name, "wb").write(file_content)

def contributors_for_lang(contributors, lang):
    return sorted(contributors.get(lang, []))

def gen_js_data(strings_dict, langs, contributors):
    res = []
    for (lang_iso, lang_name) in langs:
        if DEFAULT_LANG == lang_iso: continue
        lang_name = lang_name.split(" (")[0]
        count = untranslated_count_for_lang(strings_dict, lang_iso)
        svnurl = "http://sumatrapdf.googlecode.com/svn/trunk/strings/" + lang_iso + ".txt"
        c = contributors_for_lang(contributors, lang_iso)
        res.append([lang_iso, lang_name, count, svnurl, c])
    return sorted(res, lambda x, y: cmp(y[2], x[2]) or cmp(x[1], y[1]))

# Generate json data as array of arrays in the format:
# [langname, lang-iso-code, untranslated_strings_count, svn_url, [contributors]]
# sorted by untranslated string count (biggest at the top)
def gen_and_upload_js(strings_dict, langs, contributors):
    if not g_can_upload:
        print("Can't upload javascript to s3")
        return
    data = gen_js_data(strings_dict, langs, contributors)
    js = simplejson.dumps(data)
    js = "var g_langsData = " + js + ";\n"
    #print(js)
    s3UploadDataPublic(js, S3_JS_NAME)

def get_untranslated_as_list(untranslated_dict):
    return uniquify(sum(untranslated_dict.values(), []))

def remove_incomplete_translations(langs, strings, strings_dict, threshold=INCOMPLETE_MISSING_THRESHOLD):
    for lang in langs[:]:
        missing = get_missing_for_language(strings, strings_dict, lang[0])
        if len(missing) >= threshold and lang[0] != DEFAULT_LANG:
            langs.remove(lang)

def main_obsolete():
    (strings_dict, langs, contributors) = load_strings_file()
    strings = extract_strings_from_c_files()
    for s in strings_dict.keys():
        if s not in strings:
            del strings_dict[s]
    untranslated_dict = dump_missing_per_language(strings, strings_dict)
    write_out_strings_files(strings_dict, langs, contributors, untranslated_dict)
    untranslated = get_untranslated_as_list(untranslated_dict)
    for s in untranslated:
        if s not in strings_dict:
            strings_dict[s] = []

    langs_idx = load_lang_index()
    # check that langs_def.py agrees with the language files
    #print(langs)
    #print(langs_idx)
    for lang in langs + langs_idx:
        lang1, lang2 = [l for l in langs_idx if l[0] == lang[0]], [l for l in langs if l[0] == lang[0]]
        if not (len(lang1) == 1 and len(lang2) == 1 and lang1[0][1] == lang2[0][1]):
            print("lang1: %s" % str(lang1))
            print("lang2: %s" % str(lang2))
            assert len(lang1) == 1 and len(lang2) == 1 and lang1[0][1] == lang2[0][1]

    c_file_name = os.path.join(g_src_dir, "Translations_txt.cpp")
    #gen_and_upload_js(strings_dict, langs_idx, contributors)
    remove_incomplete_translations(langs_idx, strings, strings_dict)
    gen_c_code(langs_idx, strings_dict, c_file_name, encode_to_utf=True)

def main():
    import apptransdl
    changed = apptransdl.downloadAndUpdateTranslationsIfChanged()
    if changed:
        print("\nNew translations received from the server, checkin Translations_txt.cpp and translations.txt")

if __name__ == "__main__":
    main()
