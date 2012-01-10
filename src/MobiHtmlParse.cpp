/* Copyright 2012 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

#include "MobiHtmlParse.h"
#include "Vec.h"
#include "FileUtil.h"

/*
Converts mobi html to our internal format optimized for further layout/display.

Our format can be thought of as a stream of Virtual Machine instructions.

A VM instruction is either a string to be displayed or a formatting code (possibly
with some additional data for this formatting code).

We tokenize strings into words during this process because words are basic units
of layout. During laytou/display, a string implies a space (unless string is followed
by a html tag (formatting code) like Tag_Font etc., in which case it's elided).

Our format is a sequence of instructions encoded as sequence of bytes.

The last bytes are reserved for formatting code. 

Bytes smaller than that encode length of the string followed by bytes of the string.

If string length is >= Tag_First, we emit Tag_First followed by
a 2-byte length (i.e. a string cannot be longer than 65 kB).

Strings are utf8.

Tag is followed by a byte:
* if bit 0 is set, it's an end tag, otherwise a start (or self-closing tag)
* if bit 1 is set, the tag has attributes

If there are attributes, they follow the tag.

TODO: this encoding doesn't support a case of formatting code inside a string e.g.
"partially<i>itallic</i>". We can solve that by introducing a formatting code
denoting a string with embedded formatting codes. It's length would be 16-bit
(for simplicity of construction) telling the total size of nested data. I don't
think it happens very often in practice, though.
*/

static bool gSaveHtml = false;
#define HTML_FILE_NAME _T("mobi-test.html")

struct HtmlToken {
    enum TokenType {
        StartTag,           // <foo>
        EndTag,             // </foo>
        EmptyElementTag,    // <foo/>
        Text,               // <foo>text</foo> => "text"
        Error
    };

    enum ParsingError {
        ExpectedElement,
        NonTagAtEnd,
        UnclosedTag,
        InvalidTag
    };

    bool IsStartTag() const { return type == StartTag; }
    bool IsEndTag() const { return type == EndTag; }
    bool IsEmptyElementEndTag() const { return type == EmptyElementTag; }
    bool IsTag() const { return IsStartTag() || IsEndTag() || IsEmptyElementEndTag(); }
    bool IsText() const { return type == Text; }
    bool IsError() const { return type == Error; }

    void SetError(ParsingError err, uint8_t *errContext) {
        type = Error;
        error = err;
        s = errContext;
    }

    TokenType       type;
    ParsingError    error;
    uint8_t *       s;
    size_t          sLen;
};

/* A very simple pull html parser. Simply call Next() to get the next part of
html, which can be one one of 3 tag types or error. If a tag has attributes,
the caller has to parse them out. */
class HtmlPullParser {
    uint8_t *   s;
    uint8_t *   currPos;

    HtmlToken   currToken;

    HtmlToken * MakeError(HtmlToken::ParsingError err, uint8_t *errContext) {
        currToken.SetError(err, errContext);
        return &currToken;
    }

public:
    HtmlPullParser(uint8_t *s) : s(s), currPos(s) {
    }

    HtmlToken *Next();
};

// TODO: share this and other such functions with TrivialHtmlParser.cpp
static bool SkipUntil(char **sPtr, char c)
{
    char *s = *sPtr;
    while (*s && (*s != c)) {
        ++s;
    }
    *sPtr = s;
    return *s == c;
}

// *sPtr must be zero-terminated
static bool SkipUntil(uint8_t **sPtr, char c)
{
    return SkipUntil((char**)sPtr, c);
}

static bool SkipUntil(uint8_t*& s, uint8_t *end, uint8_t c)
{
    while ((s < end) && (*s != c)) {
        ++s;
    }
    return *s == c;
}


static bool IsWs(uint8_t c) {
    return (' ' == c) || ('\t' == c) || ('\n' == c) || ('\r' == c);
}

static void SkipWs(uint8_t*& s, uint8_t *end)
{
    while ((s < end) && IsWs(*s)) {
        ++s;
    }
}

static void SkipNonWs(uint8_t*& s, uint8_t *end)
{
    while ((s < end) && !IsWs(*s)) {
        ++s;
    }
}

static int IsName(int c)
{
    return c == '.' || c == '-' || c == '_' || c == ':' ||
        (c >= '0' && c <= '9') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= 'a' && c <= 'z');
}

// skip all html tag or attribute chatacters
static void SkipName(uint8_t*& s, uint8_t *end)
{
    while ((s < end) && IsName(*s)) {
        s++;
    }
}

// return true if s consists of only spaces
static bool IsSpaceOnly(uint8_t *s, size_t len)
{
    uint8_t *end = s + len;
    while (s < end) {
        if (*s++ != ' ')
            return false;
    }
    return true;
}

struct AttrInfo {
    uint8_t *   name;
    size_t      nameLen;
    uint8_t *   val;
    size_t      valLen;
};

// at this point we're either after tag name or
// after previous attribute
// We expect: 
// whitespace | attribute name | = | attribute value
// where both attribute name and attribute value can
// be quoted
// Not multi-thread safe (uses static buffer)
AttrInfo *GetNextAttr(uint8_t *&s, uint8_t *end)
{
    static AttrInfo attrInfo;

    // parse attribute name
    SkipWs(s, end);
    if (s == end)
        return NULL;

    attrInfo.name = s;
    SkipName(s, end);
    attrInfo.nameLen = s - attrInfo.name;
    if (0 == attrInfo.nameLen)
        return NULL;
    SkipWs(s, end);
    if ((s == end) || ('=' != *s))
        return NULL;

    // parse attribute value
    ++s; // skip '='
    SkipWs(s, end);
    if (s == end)
        return NULL;
    uint8_t quoteChar = *s;
    if (('\'' == quoteChar) || ('\"' == quoteChar)) {
        ++s;
        attrInfo.val = s;
        if (!SkipUntil(s, end, quoteChar))
            return NULL;
        attrInfo.valLen = s - attrInfo.val;
        ++s;
    } else {
        attrInfo.val = s;
        while ((s < end) && !IsWs(*s)) {
            ++s;
        }
        attrInfo.valLen = s - attrInfo.val;
    }

    // TODO: should I allow empty values?
    if (0 == attrInfo.valLen)
        return NULL;
    return &attrInfo;
}

// Returns next part of html or NULL if finished
HtmlToken *HtmlPullParser::Next()
{
    uint8_t *start;
 
    if (!*currPos)
        return NULL;

    if (s == currPos) {
        // at the beginning, we expect a tag
        if (*currPos != '<')
            return MakeError(HtmlToken::ExpectedElement, currPos);
    }

Next:
    start = currPos;
    if (*currPos != '<') {
        // this must text between tags
        if (!SkipUntil(&currPos, '<')) {
            // text cannot be at the end
            return MakeError(HtmlToken::NonTagAtEnd, start);
        }
        size_t len = currPos - start;
        if (IsSpaceOnly(start, len))
            goto Next;
        currToken.type = HtmlToken::Text;
        currToken.s = start;
        currToken.sLen = len;
        return &currToken;
    }

    // '<' - tag begins
    ++start;

    if (!SkipUntil(&currPos, '>'))
        return MakeError(HtmlToken::UnclosedTag, start);

    CrashIf('>' != *currPos);
    if (currPos == start) {
        // skip empty tags (<>), because we're lenient
        ++currPos;
        goto Next;
    }

    // skip <? and <! (processing instructions and comments)
    if (('?' == *start) || ('!' == *start)) {
        ++currPos;
        goto Next;
    }

    HtmlToken::TokenType type = HtmlToken::StartTag;
    if (('/' == *start) && ('/' == currPos[-1])) {
        // </foo/>
        return MakeError(HtmlToken::InvalidTag, start);
    }
    size_t len = currPos - start;
    if ('/' == *start) {
        // </foo>
        type = HtmlToken::EndTag;
        ++start;
        len -= 1;
    } else if ('/' == currPos[-1]) {
        // <foo/>
        type = HtmlToken::EmptyElementTag;
        len -= 1;
    }
    CrashIf('>' != *currPos);
    ++currPos;
    currToken.type = type;
    currToken.s = start;
    currToken.sLen = len;
    return &currToken;
}

static int FindStrPos(const char *strings, char *str, size_t len)
{
    const char *curr = strings;
    char *end = str + len;
    char firstChar = *str;
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
            if (*s++ != *curr++)
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

const char *gTags = "a\0b\0blockquote\0body\0br\0div\0font\0guide\0h2\0head\0html\0i\0img\0li\0mbp:pagebreak\0ol\0p\0reference\0span\0sup\0table\0td\0tr\0u\0ul\0";

HtmlTag FindTag(char *tag, size_t len)
{
    return (HtmlTag)FindStrPos(gTags, tag, len);
}

const char *gAttrs = "align\0height\0width\0";

static HtmlAttr FindAttr(char *attr, size_t len)
{
    return (HtmlAttr)FindStrPos(gAttrs, attr, len);
}

static bool IsSelfClosingTag(HtmlTag tag)
{
    // TODO: add more tags
    static HtmlTag selfClosingTags[] = { Tag_Br, Tag_Img, Tag_NotFound };
    HtmlTag *tags = selfClosingTags;
    while (*tags != -1) {
        if (tag == *tags)
            return true;
        ++tags;
    }
    return false;
}

#if 0
void DumpTag(HtmlToken *t)
{
    char buf[1024];
    if (t->sLen + 3 > dimof(buf))
        return;
    memcpy(buf, t->s, t->sLen);
    char *end = buf + t->sLen;
    char *tmp = buf;
    while ((tmp < end) && (*tmp != ' ')) {
        ++tmp;
    }
    *tmp++ = '\n';
    *tmp = 0;
    printf(buf);
}
#endif

static size_t GetTagLen(uint8_t *s, size_t len)
{
    uint8_t *end = s + len;
    uint8_t *curr = s;
    while (curr < end) {
        if (!IsName(*curr))
            return curr - s;
        ++curr;
    }
    return len;
}

static void EmitByte(Vec<uint8_t> *out, uint8_t v)
{
    out->Append(v);
}

static void EmitTag(Vec<uint8_t> *out, HtmlTag tag, bool isEndTag, bool hasAttributes)
{
    CrashAlwaysIf(hasAttributes && isEndTag);
    out->Append((uint8_t)255-tag);
    uint8_t tagPostfix = 0;
    if (isEndTag)
        tagPostfix |= IS_END_TAG_MASK;
    if (hasAttributes)
        tagPostfix |= HAS_ATTR_MASK;
    out->Append(tagPostfix);
}

static void EmitWord(Vec<uint8_t>& out, uint8_t *s, size_t len)
{
    // TODO: convert html entities to text
    if (len >= Tag_First) {
        // TODO: emit Tag_First and then len as 2 bytes
        return;
    }
    out.Append((uint8_t)len);
    out.Append(s, len);
}

// Tokenize text into words and serialize those words to
// our layout/display format
// Note: I'm collapsing multiple whitespaces. This might
// not be the right thing to do (e.g. when showing source
// code snippets)
static void EmitText(Vec<uint8_t>& out, HtmlToken *t)
{
    CrashIf(!t->IsText());
    uint8_t *end = t->s + t->sLen;
    uint8_t *curr = t->s;
    SkipWs(curr, end);
    while (curr < end) {
        uint8_t *currStart = curr;
        SkipNonWs(curr, end);
        size_t len = curr - currStart;
        if (len > 0)
            EmitWord(out, currStart, len);
        SkipWs(curr, end);
    }
}

static bool IsAllowedTag(HtmlAttr *allowed, HtmlAttr attr)
{
    while (-1 != *allowed) {
        if (attr == *allowed)
            return true;
        ++allowed;
    }
    return false;
}

static void EmitAttribute(Vec<uint8_t> *out, AttrInfo *attrInfo)
{
    // TODO: write me

}

static void EmitAttributes(Vec<uint8_t> *out, HtmlToken *t, HtmlAttr *allowedAttributes)
{
    AttrInfo *attrInfo;
    HtmlAttr attr;
    uint8_t *s = t->s;
    uint8_t *end = s + t->sLen;
    s += GetTagLen(s, t->sLen);

    // we need to cache attribute info because we first
    // need to emit attribute count and we don't know it
    // yet her
    Vec<uint8_t> attributesEncoded;
    size_t attrCount = 0;

    for (;;) {
        attrInfo = GetNextAttr(s, end);
        if (!attrInfo)
            break;
        attr = FindAttr((char*)attrInfo->name, attrInfo->nameLen);
        CrashAlwaysIf(Attr_NotFound == attr);
        if (!IsAllowedTag(allowedAttributes, attr))
            continue;
        ++attrCount;
    }
    // TODO: emit attrCount
    // TODO: add attributesEncoded to out
}

static void EmitTagP(Vec<uint8_t>* out, HtmlToken *t)
{
    static HtmlAttr validPAttrs[] = { Attr_Align, Attr_NotFound };
    if (t->IsEmptyElementEndTag()) {
        // TODO: should I generate both start and end tags?
        return;
    }
    if (t->IsEndTag()) {
        EmitTag(out, Tag_P, t->IsEndTag(), false);
    } else {
        CrashAlwaysIf(!t->IsStartTag());
        //Vec<uint8_t> attrs;
        //bool hasAttributes = EmitAttributes(&attrs, t, validPAttrs);
        bool hasAttributes = false;
        EmitTag(out, Tag_P, t->IsEndTag(), hasAttributes);
        
        /*if (hasAttributes) {
            // TODO: append atttrs to otu
        } */
    }
}

// Represents a tag in the tag nesting stack. Most of the time
// the tag wil be one of the known tags but we also allow for
// a possibility of unknown tags. We can tell them apart because
// TagEnum is a small integer and a pointer will never be that small.
// TODO: alternatively we could not record unknown tags - we ignore
// them anyway. That way we could encode the tag as a single byte.
union TagInfo {
    HtmlTag     tagEnum;
    uint8_t *   tagAddr;
};

struct ConverterState {
    Vec<uint8_t> *out;
    Vec<TagInfo> *tagNesting;
    Vec<uint8_t> *html; // prettified html
};

// record the tag for the purpose of building current state
// of html tree
static void RecordStartTag(Vec<TagInfo>* tagNesting, HtmlTag tag)
{
    if (IsSelfClosingTag(tag))
        return;
    TagInfo ti;
    ti.tagEnum = tag;
    tagNesting->Append(ti);
}

// remove the tag from state of html tree
static void RecordEndTag(Vec<TagInfo> *tagNesting, HtmlTag tag)
{
    // TODO: this logic might need to be a bit more complicated
    // e.g. when closing a tag, if the top tag doesn't match
    // but there are only potentially self-closing tags
    // on the stack between the matching tag, we should pop
    // all of them
    if (tagNesting->Count() > 0)
        tagNesting->Pop();
}

static void HandleTag(ConverterState *state, HtmlToken *t)
{
    CrashIf(!t->IsTag());

    // HtmlToken string includes potential attributes,
    // get the length of just the tag
    size_t tagLen = GetTagLen(t->s, t->sLen);
    HtmlTag tag = FindTag((char*)t->s, tagLen);
    CrashIf(tag == Tag_NotFound);

    // update the current state of html tree
    if (t->IsStartTag())
        RecordStartTag(state->tagNesting, tag);
    else if (t->IsEndTag())
        RecordEndTag(state->tagNesting, tag);

    if (Tag_P == tag) {
        EmitTagP(state->out, t);
        return;
    }
    // TODO: handle other tags
    EmitTag(state->out, tag, t->IsEndTag(), false);
}

static void HtmlAddWithNesting(Vec<uint8_t>* html, size_t nesting, uint8_t *s, size_t sLen)
{
    for (size_t i = 0; i < nesting; i++) {
        html->Append(' ');
    }
    html->Append(s, sLen);
    html->Append('\n');
}

static void PrettifyHtml(ConverterState *state, HtmlToken *t)
{
    if (!gSaveHtml)
        return;
    size_t nesting = state->tagNesting->Count();
    if (t->IsText()) {
        HtmlAddWithNesting(state->html, nesting, t->s, t->sLen);
        return;
    }

    if (!t->IsTag())
        return;

    if (t->IsEmptyElementEndTag()) {
        HtmlAddWithNesting(state->html, nesting, t->s - 1, t->sLen + 3);
        return;
    }

    if (t->IsStartTag()) {
        HtmlAddWithNesting(state->html, nesting, t->s - 1, t->sLen + 2);
        return;
    }

    if (t->IsEndTag()) {
        if (nesting > 0)
            --nesting;
        HtmlAddWithNesting(state->html, nesting, t->s - 2, t->sLen + 3);
    }
}

static void SavePrettifiedHtml(Vec<uint8_t>* html)
{
    if (!gSaveHtml)
        return;

    file::WriteAll(HTML_FILE_NAME, (void*)html->LendData(), html->Count());
}

// convert mobi html to a format optimized for layout/display
// returns NULL in case of an error
// caller has to free() the result
uint8_t *MobiHtmlToDisplay(uint8_t *s, size_t sLen, size_t& lenOut)
{
    Vec<uint8_t> res(sLen); // avoid re-allocations by pre-allocating expected size

    // tagsStack represents the current html tree nesting at current
    // parsing point. We add open tag to the stack when we encounter
    // them and we remove them from the stack when we encounter
    // its closing tag.
    Vec<TagInfo> tagNesting(256);

    Vec<uint8_t> html(sLen * 2);

    ConverterState state = { &res, &tagNesting, &html };

    HtmlPullParser parser(s);

    for (;;)
    {
        HtmlToken *t = parser.Next();
        if (!t)
            break;
        if (t->IsError())
            return NULL;
        PrettifyHtml(&state, t);
        // TODO: interpret the tag
        if (t->IsTag()) {
            HandleTag(&state, t);
        } else {
            // TODO: make sure valid utf8 or convert to utf8
            EmitText(res, t);
        }
#if 0
        if (t->IsTag()) {
            DumpTag(t);
        }
#endif
    }
    SavePrettifiedHtml(state.html);
    lenOut = res.Size();
    uint8_t *resData = res.StealData();
    return resData;
}
