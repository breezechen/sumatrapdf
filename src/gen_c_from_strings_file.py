import string
from extract_strings import load_strings_file, strings_file, get_lang_list

def c_escape(txt):
    if None == txt:
        return "NULL"
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

def gen_c_code(strings_dict):
    default_lang = "en"
    langs = get_lang_list(strings_dict)
    assert default_lang not in langs
    langs = [default_lang] + langs
    langs_count = len(langs)
    translations_count = len(strings_dict)
    print "#define TRANS_LANG_COUNT %d" % langs_count
    print
    print "const char* g_defaultLang = \"%s\";" % default_lang
    print
    print "const char *g_langs[TRANS_LANG_COUNT] = {"
    langs_c = [c_escape(l) for l in langs]
    print "  " + string.join(langs_c, ", ")
    print "};"
    print
    print "int g_translationsCount = %d;\n" % translations_count
    keys = strings_dict.keys()
    keys.sort()
    for t in range(langs_count):
        print "char *g_translation%d[] = {" % t
        if 0 == t:
            trans = keys
        else:
            lang = langs[t]
            trans = get_trans_for_lang(strings_dict, keys, lang)
        trans_c = ["  " + c_escape(t) for t in trans]
        print string.join(trans_c, ",\n")
        print "};\n"
    print "char *g_translation[TRANS_LANG_COUNT] = {"
    for t in range(langs_count):
        print "  &g_translations%d," % t
    print "};"

def main():
    strings_dict = load_strings_file(strings_file)
    gen_c_code(strings_dict)

if __name__ == "__main__":
    main()

