/* Copyright 2006-2012 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

#include "TrivialHtmlParser.h"

#include "HtmlPullParser.h"
#include "Scoped.h"
#include "StrUtil.h"

/*
Html parser that is good enough for parsing html files
inside CHM archives. Not meant for general use.

name/val pointers inside Element/Attr structs refer to
memory inside HtmlParser::s, so they don't need to be freed.
*/

struct HtmlAttr {
    char *name;
    char *val;
    HtmlAttr *next;
};

static bool IsWs(int c)
{
    return c == ' ' || c == '\r' || c == '\n' || c == '\t';
}

static int IsName(int c)
{
    return c == '.' || c == '-' || c == '_' || c == ':' ||
        (c >= '0' && c <= '9') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= 'a' && c <= 'z');
}

static void SkipWs(char **sPtr)
{
    char *s = *sPtr;
    while (IsWs(*s)) {
        s++;
    }
    *sPtr = s;
}

static void SkipName(char **sPtr)
{
    char *s = *sPtr;
    while (IsName(*s)) {
        s++;
    }
    *sPtr = s;
}

static bool SkipUntil(char **sPtr, char c)
{
    char *s = *sPtr;
    while (*s && (*s != c)) {
        ++s;
    }
    *sPtr = s;
    return *s == c;
}

// 0 if not tag end, 1 if ends with '>' and 2 if ends with "/>"
static int TagEndLen(char *s) {
    if ('>' == *s)
        return 1;
    if ('/' == s[0] && '>' == s[1])
        return 2;
    return 0;
}

static bool IsUnquotedAttrValEnd(char c) {
    return !c || IsWs(c) || c == '/' || c == '>';
}

HtmlElement *HtmlElement::GetChildByName(const char *name, int idx) const
{
    for (HtmlElement *el = down; el; el = el->next) {
        if (str::Eq(name, el->name)) {
            if (0 == idx)
                return el;
            idx--;
        }
    }
    return NULL;
}

static TCHAR IntToChar(int codepoint, UINT codepage)
{
#ifndef UNICODE
    char c = 0;
    WideCharToMultiByte(codepage, 0, &codepoint, 1, &c, 1, NULL, NULL);
    codepoint = c;
#endif
    if (codepoint <= 0 || codepoint >= (1 << (8 * sizeof(TCHAR))))
        return '?';
    return (TCHAR)codepoint;
}

// caller needs to free() the result
static TCHAR *DecodeHtmlEntitites(const char *string, UINT codepage=CP_ACP)
{
    TCHAR *fixed = str::conv::FromCodePage(string, codepage), *dst = fixed;
    const TCHAR *src = fixed;

    while (*src) {
        if (*src != '&') {
            *dst++ = *src++;
            continue;
        }
        src++;
        // numeric entities
        int unicode;
        if (str::Parse(src, _T("%d;"), &unicode) ||
            str::Parse(src, _T("#%x;"), &unicode)) {
            *dst++ = IntToChar(unicode, codepage);
            src = str::FindChar(src, ';') + 1;
            continue;
        }

        // named entities
        int rune = -1;
        const TCHAR *entityEnd = src;
        while (_istalnum(*entityEnd))
            entityEnd++;
        if (entityEnd != src) {
            size_t entityLen = entityEnd - src;
            rune = HtmlEntityNameToRune(src, entityLen);
        }
        if (-1 != rune) {
            *dst++ = IntToChar(rune, codepage);
            src = entityEnd;
            if (*src == _T(';'))
                ++src;
        } else {
            *dst++ = '&';
        }
    }
    *dst = '\0';

    return fixed;
}

HtmlParser::HtmlParser() : html(NULL), freeHtml(false), rootElement(NULL),
    currElement(NULL), elementsCount(0), attributesCount(0),
    error(ErrParsingNoError), errorContext(NULL)
{
}

HtmlParser::~HtmlParser()
{
    if (freeHtml)
        free(html);
}

void HtmlParser::Reset()
{
    if (freeHtml)
        free(html);
    html = NULL;
    freeHtml = false;
    rootElement = currElement = NULL;
    elementsCount = attributesCount = 0;
    error = ErrParsingNoError;
    errorContext = NULL;
    allocator.FreeAll();
}

HtmlAttr *HtmlParser::AllocAttr(char *name, HtmlAttr *next)
{
    HtmlAttr *attr = allocator.AllocStruct<HtmlAttr>();
    attr->name = name;
    attr->next = next;
    ++attributesCount;
    return attr;
}

// caller needs to free() the result
TCHAR *HtmlElement::GetAttribute(const char *name) const
{
    for (HtmlAttr *attr = firstAttr; attr; attr = attr->next) {
        if (str::EqI(attr->name, name))
            return DecodeHtmlEntitites(attr->val, codepage);
    }
    return NULL;
}

HtmlElement *HtmlParser::AllocElement(char *name, HtmlElement *parent)
{
    HtmlElement *el = allocator.AllocStruct<HtmlElement>();
    el->name = name;
    el->up = parent;
    el->codepage = codepage;
    ++elementsCount;
    return el;
}

HtmlElement *HtmlParser::FindParent(char *tagName)
{
    if (str::Eq(tagName, "li")) {
        // make a list item the child of the closest list
        for (HtmlElement *el = currElement; el; el = el->up) {
            if (str::Eq(el->name, "ul") || str::Eq(el->name, "ol"))
                return el;
        }
    }

    return currElement;
}

void HtmlParser::StartTag(char *tagName)
{
    str::ToLower(tagName);
    HtmlElement *parent = FindParent(tagName);
    currElement = AllocElement(tagName, parent);
    if (NULL == rootElement)
        rootElement = currElement;

    if (!parent) {
        // if this isn't the root tag, this tag
        // and all its children will be ignored
    } else if (NULL == parent->down) {
        // parent has no children => set as a first child
        parent->down = currElement;
    } else {
        // parent has children => set as a sibling
        HtmlElement *tmp = parent->down;
        while (tmp->next) {
            tmp = tmp->next;
        }
        tmp->next = currElement;
    }
}

void HtmlParser::CloseTag(char *tagName)
{
    str::ToLower(tagName);
    // to allow for lack of closing tags, e.g. in case like
    // <a><b><c></a>, we look for the first parent with matching name
    for (HtmlElement *el = currElement; el; el = el->up) {
        if (str::Eq(el->name, tagName)) {
            currElement = el->up;
            return;
        }
    }
    // ignore the unexpected closing tag
}

void HtmlParser::StartAttr(char *name)
{
    str::ToLower(name);
    currElement->firstAttr = AllocAttr(name, currElement->firstAttr);
}

void HtmlParser::SetAttrVal(char *val)
{
    currElement->firstAttr->val = val;
}

static char *ParseAttrValue(char **sPtr)
{
    char *attrVal = NULL;
    char *s = *sPtr;
    SkipWs(&s);
    char quoteChar = *s;
    if (quoteChar == '"' || quoteChar == '\'') {
        ++s;
        attrVal = s;
        if (!SkipUntil(&s, quoteChar))
            return NULL;
        *s++ = 0;
    } else {
        attrVal = s;
        while (!IsUnquotedAttrValEnd(*s)) {
            ++s;
        }
        if (!IsWs(*s) && TagEndLen(s) == 0)
            return NULL;
        if (IsWs(*s))
            *s = 0;
    }
    *sPtr = s;
    return attrVal;
}

static char *ParseAttrName(char **sPtr)
{
    char *s = *sPtr;
    char *attrName = s;
    SkipName(&s);
    char *attrNameEnd = s;
    SkipWs(&s);
    if (*s != '=')
        return NULL;
    *attrNameEnd = 0; // terminate attribute name
    *sPtr = ++s;
    return attrName;
}

// Parse s in place i.e. we assume we can modify it. Must be 0-terminated.
// The caller owns the memory for s.
HtmlElement *HtmlParser::ParseInPlace(char *s, UINT codepage)
{
    char *tagName, *attrName, *attrVal, *tagEnd;
    int tagEndLen;

    if (this->html)
        Reset();

    this->html = s;
    this->codepage = codepage;

ParseText:
    if (!SkipUntil(&s, '<')) {
        // Note: I think we can be in an inconsistent state here
        // (unclosed tags) but not sure if we should care
        return rootElement;
    }
    // TODO: if within a tag, set this as tag value
    // Note: even then it won't handle cases where value
    // spans multiple parts as in:
    // "<a>foo<b/>bar</a>", where value of tag a should be "foobar"
    ++s;

    if (*s == '!' || *s == '?') {
        ++s;
        goto ParseExclOrPi;
    }

    if (*s == '/') {
        ++s;
        goto ParseClosingElement;
    }

    // parse element name
    errorContext = s;
    SkipWs(&s);
    if (!IsName(*s))
        return ParseError(ErrParsingElement);
    tagName = s;
    SkipName(&s);
    tagEnd = s;
    SkipWs(&s);
    tagEndLen = TagEndLen(s);
    if (tagEndLen > 0) {
        *tagEnd = 0;
        StartTag(tagName);
        if (tagEndLen == 2 || IsTagSelfClosing(tagName))
            CloseTag(tagName);
        s += tagEndLen;
        goto ParseText;
    }
    if (IsWs(*tagEnd)) {
        *tagEnd = 0;
        StartTag(tagName);
        goto ParseAttributes;
    }
    return ParseError(ErrParsingElementName);

ParseClosingElement: // "</"
    errorContext = s;
    SkipWs(&s);
    if (!IsName(*s))
        return ParseError(ErrParsingClosingElement);
    tagName = s;
    SkipName(&s);
    tagEnd = s;
    SkipWs(&s);
    if (*s != '>')
        return ParseError(ErrParsingClosingElement);
    *tagEnd = 0;
    CloseTag(tagName);
    ++s;
    goto ParseText;

ParseAttributes:
    errorContext = s;
    SkipWs(&s);
    if (IsName(*s))
        goto ParseAttributeName;
    tagEndLen = TagEndLen(s);
    if (0 == tagEndLen)
        return ParseError(ErrParsingAttributes);

FoundElementEnd:
    if (tagEndLen == 2 || IsTagSelfClosing(tagName))
        CloseTag(tagName);
    s += tagEndLen;
    goto ParseText;

ParseAttributeName:
    errorContext = s;
    attrName = ParseAttrName(&s);
    if (!attrName)
        return ParseError(ErrParsingAttributeName);
    StartAttr(attrName);

    // parse attribute value
    errorContext = s;
    attrVal = ParseAttrValue(&s);
    if (!attrVal)
        return ParseError(ErrParsingAttributeValue);
    tagEndLen = TagEndLen(s);
    if (tagEndLen > 0) {
        *s = 0;
        SetAttrVal(attrVal);
        goto FoundElementEnd;
    }
    SetAttrVal(attrVal);
    s++;
    goto ParseAttributes;

ParseExclOrPi: // "<!" or "<?"
    // might be a <!DOCTYPE ..>, a <!-- comment ->, a <? processing instruction >
    // or really anything. We're very lenient and consider it a success
    // if we find a terminating '>'
    errorContext = s;
    if (!SkipUntil(&s, '>'))
        return ParseError(ErrParsingExclOrPI);
    ++s;
    goto ParseText;
}

HtmlElement *HtmlParser::Parse(const char *s, UINT codepage)
{
    HtmlElement *root = ParseInPlace(str::Dup(s), codepage);
    freeHtml = true;
    return root;
}

// Does a depth-first search of element tree, looking for an element with
// a given name. If from is NULL, it starts from rootElement otherwise
// it starts from *next* element in traversal order, which allows for
// easy iteration over elements.
// Note: name must be lower-case
HtmlElement *HtmlParser::FindElementByName(const char *name, HtmlElement *from)
{
    HtmlElement *el = from;
    if (!from) {
        if (!rootElement)
            return NULL;
        if (str::Eq(name, rootElement->name))
            return rootElement;
        el = rootElement;
    }
Next:
    if (el->down) {
        el = el->down;
        goto FoundNext;
    }
    if (el->next) {
        el = el->next;
        goto FoundNext;
    }
    // backup in the tree
    HtmlElement *parent = el->up;
    while (parent) {
        if (parent->next) {
            el = parent->next;
            goto FoundNext;
        }
        parent = parent->up;
    }
    return NULL;
FoundNext:
    if (str::Eq(el->name, name))
        return el;
    goto Next;
}

#ifdef DEBUG
#include "TrivialHtmlParser_ut.cpp"
#endif
