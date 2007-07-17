import codecs

files_to_process = ["SumatraPDF.cpp"]
strings_file = "strings.txt"

(ST_NONE) = range(1)

def is_comment_line(l): return l[0] == '#'
def is_langs_line(l): return l.startswith("Languages:")
def is_separator_line(l): return l == "-"
def is_newline_char(c): return c == '\n' or c == '\r'
def line_strip_newline(l):
    while is_newline_char(l[-1]):
        l = l[:-1]
    return l

def parse_langs_line(l):
    assert is_langs_line(l)
    # TODO: write me
    return l

def load_strings_file(file_name):
    strings_dict = {}
    langs = []
    fo = codecs.open(file_name, "r", "utf-8-sig")
    state = ST_NONE
    for l in fo.readlines():
        l = line_strip_newline(l)
        print l
        if is_comment_line(l):
            assert ST_NONE == state
            continue
        if is_langs_line(l):
            langs = parse_langs_line(l)
        #print l
    fo.close()
    return strings_dict

def main():
    strings_dict = load_strings_file(strings_file)

if __name__ == "__main__":
    main()

