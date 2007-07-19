import codecs

files_to_process = ["SumatraPDF.cpp"]
strings_file = "strings.txt"

(ST_NONE, ST_BEFORE_ORIG, ST_IN_TRANSLATIONS) = range(3)

def state_name(state):
    if ST_NONE == state: return "ST_NONE"
    if ST_BEFORE_ORIG == state: return "ST_BEFORE_ORIG"
    if ST_IN_TRANSLATIONS == state: return "ST_IN_TRANSLATIONS"
    assert 0
    return "UNKNOWN STATE"

LANGUAGES_TXT = "Languages:"

def is_langs_line(l): return l.startswith(LANGUAGES_TXT)
def is_separator_line(l): return l == "-"
def is_newline_char(c): return c == '\n' or c == '\r'

def is_comment_line(l): 
    if 0 == len(l): return False
    return l[0] == '#'

def line_strip_newline(l):
    while is_newline_char(l[-1]):
        l = l[:-1]
    return l

def line_with_translation(l):
    if len(l) < 3: return False
    if ':' == l[2]: return True
    if len(l) < 6: return False
    if ':' == l[5]: return True
    return False

def parse_line_with_translation(l):
    if ':' == l[2]:
        idx = 2
    elif ':' == l[5]:
        idx = 5
    else:
        assert 0
    lang = l[:idx]
    txt = l[idx+1:]
    return (lang, txt)

def parse_langs_line(l):
    assert is_langs_line(l)
    l = l[len(LANGUAGES_TXT):]
    langs = l.split()
    for lang in langs:
        # lang format is either "fr" or "en-us"
        assert 2 == len(lang) or 5 == len(lang)
    return langs

def report_error(line_no, line, err_txt):
    print "Error on line %d:" % line_no
    print "'%s'" % line
    print err_txt
    assert 0

def load_strings_file(file_name):
    strings_dict = {}
    langs = None
    fo = codecs.open(file_name, "r", "utf-8-sig")
    state = ST_NONE
    curr_trans = None
    line_no = 0
    for l in fo.readlines():
        line_no = line_no + 1
        l = line_strip_newline(l)
        #print l
        #print state_name(state)
        if is_comment_line(l):
            assert ST_NONE == state
            continue
        if is_langs_line(l):
            assert ST_NONE == state
            langs = parse_langs_line(l)
            continue
        if is_separator_line(l):
            if None != curr_trans:
                key = curr_trans[0]
                value = curr_trans[1:]
                if key in strings_dict:
                    report_error(line_no, l, "'%s' is a duplicate text" % key)
                strings_dict[key] = value
            state = ST_BEFORE_ORIG
            curr_trans = None
            continue
        if None == langs:
            report_error(line_no, l, "Expected list of languages (Languages: ...)")
        if ST_BEFORE_ORIG == state:
            if None != curr_trans:
                print curr_trans
                assert None == curr_trans
            if line_with_translation(l):
                report_error(line_no, l, "Looks like a line with translation and expected the original string")
            curr_trans = [l]
            state = ST_IN_TRANSLATIONS
        elif ST_IN_TRANSLATIONS == state:
            if not line_with_translation(l):
                report_error(line_no, l, "Expected line with translation")
            (lang, txt) = parse_line_with_translation(l)
            if lang not in langs:
                report_error(line_no, l, "lang '%s' is not in declared list of languages '%s'" % (lang, str(langs)))
            curr_trans.append([lang, txt])
        else:
            assert 0
        #print l
    fo.close()
    # TODO: repeat of a code above
    if None != curr_trans:
        key = curr_trans[0]
        value = curr_trans[1:]
        if key in strings_dict:
            report_error(line_no, l, "'%s' is a duplicate text" % key)
        strings_dict[key] = value
    return strings_dict

def main():
    strings_dict = load_strings_file(strings_file)
    print strings_dict

if __name__ == "__main__":
    main()

