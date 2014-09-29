#!/usr/bin/env python
"""
This script generates fairly fast C code for the following function:
Given a string, see if it belongs to a known set of strings. If it does,
return a value corresponding to that string.
"""

import util2

Template_Defines = """\
#define CS1(c1)             (c1)
#define CS2(c1, c2)         (CS1(c1) | (c2 << 8))
#define CS3(c1, c2, c3)     (CS2(c1, c2) | (c3 << 16))
#define CS4(c1, c2, c3, c4) (CS3(c1, c2, c3) | (c4 << 24))

#define STR1(s) ((s)[0])
#define STR2(s) (STR1(s) | ((s)[1] << 8))
#define STR3(s) (STR2(s) | ((s)[2] << 16))
#define STR4(s) (STR3(s) | ((s)[3] << 24))

#define lower(c) ((c) < 'A' || (c) > 'Z' ? (c) : (c) - 'A' + 'a')

#define STR1i(s) (lower((s)[0]))
#define STR2i(s) (STR1i(s) | (lower((s)[1]) << 8))
#define STR3i(s) (STR2i(s) | (lower((s)[2]) << 16))
#define STR4i(s) (STR3i(s) | (lower((s)[3]) << 24))
"""

Template_Find_Function = """\
%s Find%s(const char *name, size_t len)
{
	uint32_t key = 0 == len ? 0 : 1 == len ? STR1i(name) :
	               2 == len ? STR2i(name) : 3 == len ? STR3i(name) : STR4i(name);
	switch (key) {
	%s
	}
	return %s;
}
"""

Template_Enumeration = """\
enum %s {
	%s
};
"""

Template_Selector = """\
bool %s(%s item)
{
	switch (item) {
	%s
		return true;
	default:
		return false;
	}
}
"""

# given e.g. "br" returns "Tag_Br"
def getEnumName(name, prefix):
	parts = name.replace("-", ":").split(":")
	parts = [p[0].upper() + p[1:].lower() for p in parts]
	return "_".join([prefix] + parts)

# given e.g. "abcd" returns "'a','b','c','d'"
def splitChars(chars):
	return "'" + "','".join(chars) + "'"

def unTab(string):
	return string.replace("\t", "    ")

# creates a lookup function that works with one switch for quickly
# finding (or failing to find) the correct value
def createFastFinder(list, type, default, caseInsensitive, funcName=None):
	list = sorted(list, key=lambda a: a[0])
	output = []
	while list:
		name, value = list.pop(0)
		if len(name) < 4:
			# no further comparison is needed for names less than 4 characters in length
			output.append('case CS%d(%s): return %s;' % (len(name), splitChars(name), value))
		else:
			# for longer names, do either another quick check (up to 8 characters)
			# or use str::EqN(I) for longer names
			output.append('case CS4(%s):' % "'%s'" % "','".join(name[:4]))
			while True:
				if len(name) == 4:
					output.append("	if (4 == len) return %s;" % value)
				elif len(name) <= 8:
					rest = name[4:]
					output.append('	if (%d == len && CS%d(%s) == STR%di(name + 4)) return %s;' %
						(len(name), len(rest), splitChars(rest), len(rest), value))
				else:
					output.append('	if (%d == len && str::EqNI(name + 4, "%s", %d)) return %s;' %
						(len(name), name[4:], len(name) - 4, value))
				# reuse the same case for names that start the same
				if not list or list[0][0][:4] != name[:4]:
					break
				name, value = list.pop(0)
			output.append('	break;')

	output = Template_Find_Function % (type, funcName or type, "\n	".join(output), default)
	if not caseInsensitive:
		output = output.replace("STR1i(", "STR1(").replace("STR2i(", "STR2(")
		output = output.replace("STR3i(", "STR3(").replace("STR4i(", "STR4(")
		output = output.replace("str::EqNI(", "str::EqN(")
	else:
		assert not [c for c in output if c > '\x7f'], "lower() only supports ASCII letters"
	return unTab(output)

# creates an enumeration that can be used as a result for the lookup function
# (which would allow to "internalize" a string)
def createTypeEnum(list, type, default):
	list = sorted(list, key=lambda a: a[0])
	parts = util2.group([item[1] for item in list] + [default], 5)
	return unTab(Template_Enumeration % (type, ",\n	".join([", ".join(part) for part in parts])))

def createFastSelector(fullList, nameList, funcName, type):
	cases = ["case %s:" % value for (name, value) in fullList if name in nameList]
	return unTab(Template_Selector % (funcName, type, "\n	".join([" ".join(part) for part in util2.group(cases, 4)])))

########## HTML tags and attributes ##########

# This list has been generated by instrumenting HtmlFormatter.cpp
# to dump all tags we see in a mobi file and also some from EPUB and FB2 files
List_HTML_Tags = "a abbr acronym area audio b base basefont blockquote body br center code col dd div dl dt em font frame h1 h2 h3 h4 h5 h6 head hr html i img input lh li link meta nav object ol p param pre s script section small span strike strong style sub sup table td th title tr tt u ul video"
List_Other_Tags = "image mbp:pagebreak pagebreak subtitle svg svg:image"
List_Align_Values = "center justify left right"
# TODO: this incomplete list is currently unused
List_HTML_Attrs = "align bgcolor border class clear colspan color controls face height href id lang link rowspan size style title valign value vlink width"
List_Other_Attrs = "filepos mediarecindex recindex xmlns"

# these tags must all also appear in List_HTML_Tags or List_Other_Tags (else they're ignored)
List_Self_Closing_Tags = "area base basefont br col frame hr img input link mbp:pagebreak meta pagebreak param"
List_Inline_Tags = "a abbr acronym audio b code em font i s small span strike strong sub sup tt u video"

########## HTML and XML entities ##########

Template_Entities_Comment = """\
// map of entity names to their Unicode runes, cf.
// http://en.wikipedia.org/wiki/List_of_XML_and_HTML_character_entity_references
// and http://www.w3.org/TR/MathML2/bycodes.html
"""

# selection of MathML2 entities that aren't HTML entities
List_MathML2_Entities = [("DoubleDot", 168), ("OverBar", 175), ("PlusMinus", 177), ("Cedilla", 184), ("Amacr", 256), ("amacr", 257), ("Abreve", 258), ("abreve", 259), ("Aogon", 260), ("aogon", 261), ("Cacute", 262), ("cacute", 263), ("Ccirc", 264), ("ccirc", 265), ("Cdot", 266), ("cdot", 267), ("Ccaron", 268), ("ccaron", 269), ("Dcaron", 270), ("dcaron", 271), ("Dstrok", 272), ("dstrok", 273), ("Emacr", 274), ("emacr", 275), ("Edot", 278), ("edot", 279), ("Eogon", 280), ("eogon", 281), ("Ecaron", 282), ("ecaron", 283), ("Gcirc", 284), ("gcirc", 285), ("Gbreve", 286), ("gbreve", 287), ("Gdot", 288), ("gdot", 289), ("Gcedil", 290), ("Hcirc", 292), ("hcirc", 293), ("Hstrok", 294), ("hstrok", 295), ("Itilde", 296), ("itilde", 297), ("Imacr", 298), ("imacr", 299), ("Iogon", 302), ("iogon", 303), ("Idot", 304), ("IJlig", 306), ("ijlig", 307), ("Jcirc", 308), ("jcirc", 309), ("Kcedil", 310), ("kcedil", 311), ("kgreen", 312), ("Lacute", 313), ("lacute", 314), ("Lcedil", 315), ("lcedil", 316), ("Lcaron", 317), ("lcaron", 318), ("Lmidot", 319), ("lmidot", 320), ("Lstrok", 321), ("lstrok", 322), ("Nacute", 323), ("nacute", 324), ("Ncedil", 325), ("ncedil", 326), ("Ncaron", 327), ("ncaron", 328), ("napos", 329), ("ENG", 330), ("eng", 331), ("Omacr", 332), ("omacr", 333), ("Odblac", 336), ("odblac", 337), ("Racute", 340), ("racute", 341), ("Rcedil", 342), ("rcedil", 343), ("Rcaron", 344), ("rcaron", 345), ("Sacute", 346), ("sacute", 347), ("Scirc", 348), ("scirc", 349), ("Scedil", 350), ("scedil", 351), ("Tcedil", 354), ("tcedil", 355), ("Tcaron", 356), ("tcaron", 357), ("Tstrok", 358), ("tstrok", 359), ("Utilde", 360), ("utilde", 361), ("Umacr", 362), ("umacr", 363), ("Ubreve", 364), ("ubreve", 365), ("Uring", 366), ("uring", 367), ("Udblac", 368), ("udblac", 369), ("Uogon", 370), ("uogon", 371), ("Wcirc", 372), ("wcirc", 373), ("Ycirc", 374), ("ycirc", 375), ("Zacute", 377), ("zacute", 378), ("Zdot", 379), ("zdot", 380), ("Zcaron", 381), ("zcaron", 382), ("imped", 437), ("gacute", 501), ("Hacek", 711), ("Breve", 728), ("DiacriticalDot", 729), ("ring", 730), ("ogon", 731), ("DiacriticalTilde", 732), ("DiacriticalDoubleAcute", 733), ("DownBreve", 785), ("UnderBar", 818), ("varepsilon", 949), ("varsigma", 962), ("varphi", 966), ("vartheta", 977), ("Upsi", 978), ("straightphi", 981), ("varpi", 982), ("Gammad", 988), ("digamma", 989), ("varkappa", 1008), ("varrho", 1009), ("straightepsilon", 1013), ("backepsilon", 1014)]

from htmlentitydefs import entitydefs
entitydefs['apos'] = "'" # only XML entity that isn't an HTML entity as well
List_HTML_Entities = []
for name, value in entitydefs.items():
	List_HTML_Entities.append((name, value[2:-1] or str(ord(value))))
for (name, value) in List_MathML2_Entities:
	assert name not in entitydefs
	List_HTML_Entities.append((name, str(value)))

########## CSS properties ##########

List_CSS_Props = "color display font font-family font-size font-style font-weight list-style margin margin-bottom margin-left margin-right margin-top max-width opacity padding padding-bottom padding-left padding-right padding-top page-break-after page-break-before text-align text-decoration text-indent text-underline white-space word-wrap"

########## CSS colors ##########

# array of name/value for css colors, value is what goes inside MKRGB()
# based on https://developer.mozilla.org/en/CSS/color_value
# TODO: add more colors
List_CSS_Colors = [
	("black",        "  0,  0,  0"),
	("white",        "255,255,255"),
	("gray",         "128,128,128"),
	("red",          "255,  0,  0"),
	("green",        "  0,128,  0"),
	("blue",         "  0,  0,255"),
	("yellow",       "255,255,  0"),
];
# fallback is the transparent color MKRGBA(0,0,0,0)

########## main ##########

Template_Lookup_Header = """\
/* Copyright 2014 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

// This file is auto-generated by gen_htmlparserlookup.py

#ifndef HtmlParserLookup_h
#define HtmlParserLookup_h

%(enum_htmltag)s
%(enum_alignattr)s
HtmlTag         FindHtmlTag(const char *name, size_t len);
bool            IsTagSelfClosing(HtmlTag item);
bool            IsInlineTag(HtmlTag item);
AlignAttr       FindAlignAttr(const char *name, size_t len);
uint32_t        FindHtmlEntityRune(const char *name, size_t len);

%(enum_cssprop)s
CssProp         FindCssProp(const char *name, size_t len);

#endif
"""

Template_Lookup_Code = """\
/* Copyright 2014 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

// This file is auto-generated by gen_htmlparserlookup.py

#include "BaseUtil.h"
#include "HtmlParserLookup.h"

%(code_defines)s
%(code_htmltag)s
%(code_selfclosing)s
%(code_inlinetag)s
%(code_alignattr)s
%(code_htmlentity)s
%(code_cssprop)s
"""

def main():
	util2.chdir_top()

	tags = [(name, getEnumName(name, "Tag")) for name in sorted(List_HTML_Tags.split() + List_Other_Tags.split())]
	attrs = [(name, getEnumName(name, "Attr")) for name in sorted(List_HTML_Attrs.split() + List_Other_Attrs.split())]
	aligns = [(name, getEnumName(name, "Align")) for name in sorted(List_Align_Values.split())]
	cssProps = [(name, getEnumName(name, "Css")) for name in sorted(List_CSS_Props.split())]
	cssColors = [(name, "MKRGB(%s)" % value) for (name, value) in sorted(List_CSS_Colors)]

	enum_htmltag = createTypeEnum(tags, "HtmlTag", "Tag_NotFound")
	enum_htmlattr = createTypeEnum(attrs, "HtmlAttr", "Attr_NotFound")
	enum_alignattr = createTypeEnum(aligns, "AlignAttr", "Align_NotFound")
	enum_cssprop = createTypeEnum(cssProps, "CssProp", "Css_Unknown")

	code_defines = Template_Defines
	code_htmltag = createFastFinder(tags, "HtmlTag", "Tag_NotFound", True)
	code_htmlattr = createFastFinder(attrs, "HtmlAttr", "Attr_NotFound", True)
	code_selfclosing = createFastSelector(tags, List_Self_Closing_Tags.split(), "IsTagSelfClosing", "HtmlTag")
	code_inlinetag = createFastSelector(tags, List_Inline_Tags.split(), "IsInlineTag", "HtmlTag")
	code_alignattr = createFastFinder(aligns, "AlignAttr", "Align_NotFound", True)
	code_htmlentity = Template_Entities_Comment + "\n" + createFastFinder(List_HTML_Entities, "uint32_t", "(uint32_t)-1", False, "HtmlEntityRune")
	code_cssprop = createFastFinder(cssProps, "CssProp", "Css_Unknown", True)
	code_csscolor = createFastFinder(cssColors, "ARGB", "MKRGBA(0,0,0,0)", True, "CssColor")

	content = Template_Lookup_Header % locals()
	open("src/utils/HtmlParserLookup.h", "wb").write(content.replace("\n", "\r\n"))
	content = Template_Lookup_Code[:-1] % locals()
	open("src/utils/HtmlParserLookup.cpp", "wb").write(content.replace("\n", "\r\n"))

if __name__ == "__main__":
	main()
