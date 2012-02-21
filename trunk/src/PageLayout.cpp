/* Copyright 2011-2012 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

#include "DebugLog.h"
#include "Mui.h"
#include "PageLayout.h"
#include "StrUtil.h"

/*
TODO: PageLayout could be split into DrawInstrBuilder which knows pageDx, pageDy
and generates DrawInstr and splits them into pages and a better named class that
does the parsing of the document builds pages by invoking methods on DrawInstrBuilders.

TODO: instead of generating list of DrawInstr objects, we could add neccessary
support to mui and use list of Control objects instead (especially if we slim down
Control objects further to make allocating hundreds of them cheaper or introduce some
other base element(s) with less functionality and less overhead).
*/

static void SkipCharInStr(const char *& s, size_t& left, char c)
{
    while ((left > 0) && (*s == c)) {
        ++s; --left;
    }
}

static bool IsWordBreak(char c)
{
    return (c == ' ') || (c == '\n') || (c == '\r');
}

static void SkipNonWordBreak(const char *& s, size_t& left)
{
    while ((left > 0) && !IsWordBreak(*s)) {
        ++s; --left;
    }
}

static bool IsNewline(const char *s, const char *end)
{
    return (1 == end - s) && ('\n' == s[0]);
}

// return true if s points to "\n", "\r" or "\r\n"
// and advance s/left to skip it
// We don't want to collapse multiple consequitive newlines into
// one as we want to be able to detect paragraph breaks (i.e. empty
// newlines i.e. a newline following another newline)
static bool IsNewlineSkip(const char *& s, size_t& left)
{
    if (0 == left)
        return false;
    if ('\r' == *s) {
        --left; ++s;
        if ((left > 0) && ('\n' == *s)) {
            --left; ++s;
        }
        return true;
    }
    if ('\n' == *s) {
        --left; ++s;
        return true;
    }
    return false;
}

DrawInstr DrawInstr::Str(const char *s, size_t len, RectF bbox)
{
    DrawInstr di(InstrString, bbox);
    di.str.s = s;
    di.str.len = len;
    return di;
}

DrawInstr DrawInstr::SetFont(Font *font)
{
    DrawInstr di(InstrSetFont);
    di.setFont.font = font;
    return di;
}

DrawInstr DrawInstr::Line(RectF bbox)
{
    DrawInstr di(InstrLine, bbox);
    return di;
}

DrawInstr DrawInstr::Space()
{
    return DrawInstr(InstrSpace);
}

PageLayout::PageLayout() : currPage(NULL), gfx(NULL)
{
}

PageLayout::~PageLayout()
{
    // delete all pages that were not consumed by the caller
    for (PageData **p = pagesToSend.IterStart(); p; p = pagesToSend.IterNext()) {
        delete *p;
    }
    delete currPage;
    delete htmlParser;
    mui::FreeGraphicsForMeasureText(gfx);
}

void PageLayout::SetCurrentFont(FontStyle fs)
{
    currFontStyle = fs;
    currFont = mui::GetCachedFont(fontName.Get(), fontSize, fs);
}

static bool ValidFontStyleForChangeFont(FontStyle fs)
{
    if ((FontStyleBold == fs) ||
        (FontStyleItalic == fs) ||
        (FontStyleUnderline == fs) ||
        (FontStyleStrikeout == fs)) {
            return true;
    }
    return false;
}

// change the current font by adding (if addStyle is true) or removing
// a given font style from current font style
// TODO: it doesn't support the case where the same style is nested
// like "<b>fo<b>oo</b>bar</b>" - "bar" should still be bold but wont
// We would have to maintain counts for each style to do it fully right
void PageLayout::ChangeFont(FontStyle fs, bool addStyle)
{
    CrashAlwaysIf(!ValidFontStyleForChangeFont(fs));
    FontStyle newFontStyle = currFontStyle;
    if (addStyle)
        newFontStyle = (FontStyle) (newFontStyle | fs);
    else
        newFontStyle = (FontStyle) (newFontStyle & ~fs);

    if (newFontStyle == currFontStyle)
        return; // a no-op
    SetCurrentFont(newFontStyle);
    EmitSetFont(currFont);
}

DrawInstr *PageLayout::GetInstructionsForCurrentLine(DrawInstr *& endInst) const
{
    size_t len = currPage->Count() - currLineInstrOffset;
    DrawInstr *ret = currPage->drawInstructions.AtPtr(currLineInstrOffset);
    endInst = ret + len;
    return ret;
}

PageData *PageLayout::IterStart(LayoutInfo* layoutInfo)
{
    pageDx = (REAL)layoutInfo->pageDx;
    pageDy = (REAL)layoutInfo->pageDy;
    pageSize.dx = pageDx;
    pageSize.dy = pageDy;
    textAllocator = layoutInfo->textAllocator;

    CrashIf(gfx);
    gfx = mui::AllocGraphicsForMeasureText();
    fontName.Set(str::Dup(layoutInfo->fontName));
    fontSize = layoutInfo->fontSize;
    htmlParser = new HtmlPullParser(layoutInfo->htmlStr, layoutInfo->htmlStrLen);

    finishedParsing = false;
    currJustification = Align_Justify;
    SetCurrentFont(FontStyleRegular);
    InitFontMetricsCache(&fontMetrics, gfx, currFont);

    CrashIf(currPage);
    lineSpacing = currFont->GetHeight(gfx);
    // note: this is a heuristic, using 
    spaceDx = fontSize / 2.5f;
    float spaceDx2 = GetSpaceDx(gfx, currFont);
    if (spaceDx2 < spaceDx)
        spaceDx = spaceDx2;
    StartNewPage();
    return IterNext();
}

void PageLayout::StartNewPage()
{
    if (currPage)
        pagesToSend.Append(currPage);
    currPage = new PageData;
    currX = currY = 0;
    newLinesCount = 0;
    // instructions for each page need to be self-contained
    // so we have to carry over some state like the current font
    CrashIf(!currFont);
    EmitSetFont(currFont);
    currLineInstrOffset = currPage->Count();
}

REAL PageLayout::GetCurrentLineDx()
{
    REAL dx = 0;
    DrawInstr *end;
    DrawInstr *i = GetInstructionsForCurrentLine(end);
    while (i < end) {
        if (InstrString == i->type) {
            dx += i->bbox.Width;
        } else if (InstrSpace == i->type) {
            dx += spaceDx;
        }
        ++i;
    }
    return dx;
}

void PageLayout::LayoutLeftStartingAt(REAL offX)
{
    currX = offX;
    DrawInstr *end;
    DrawInstr *i = GetInstructionsForCurrentLine(end);
    while (i < end) {
        if (InstrString == i->type) {
            // currInstr Width and Height are already set
            i->bbox.X = currX;
            i->bbox.Y = currY;
            currX += i->bbox.Width;
        } else if (InstrSpace == i->type) {
            currX += spaceDx;
        }
        ++i;
    }
}

// Redistribute extra space in the line equally among the spaces
void PageLayout::JustifyLineBoth()
{
    REAL margin = pageSize.dx - GetCurrentLineDx();
    LayoutLeftStartingAt(0);
    DrawInstr *end;
    DrawInstr *start = GetInstructionsForCurrentLine(end);
    size_t spaces = 0;
    for (DrawInstr *i = start; i < end; i++) {
        if (InstrSpace == i->type)
            ++spaces;
    }
    REAL extraSpaceDx = margin;
    if (spaces > 0)
        extraSpaceDx = margin / spaces;
    REAL extraSpaceDxTotal = 0;
    for (DrawInstr *i = start; i < end; i++) {
        i->bbox.X += extraSpaceDxTotal;
        if (InstrSpace == i->type)
            extraSpaceDxTotal += extraSpaceDx;
    }
}

void PageLayout::JustifyLine(AlignAttr mode)
{
    if (IsCurrentLineEmpty())
        return;

    switch (mode) {
    case Align_Left:
        LayoutLeftStartingAt(0);
        break;
    case Align_Right:
        LayoutLeftStartingAt(pageSize.dx - GetCurrentLineDx());
        break;
    case Align_Center:
        LayoutLeftStartingAt((pageSize.dx - GetCurrentLineDx()) / 2.f);
        break;
    case Align_Justify:
        JustifyLineBoth();
        break;
    default:
        CrashIf(true);
        break;
    }
    currLineInstrOffset = currPage->Count();
}

void PageLayout::StartNewLine(bool isParagraphBreak)
{
    // don't put empty lines at the top of the page
    if ((0 == currY) && IsCurrentLineEmpty())
        return;

    if (isParagraphBreak && Align_Justify == currJustification)
        JustifyLine(Align_Left);
    else
        JustifyLine(currJustification);

    currX = 0;
    currY += lineSpacing;
    currLineInstrOffset = currPage->Count();
    if (currY + lineSpacing > pageDy)
        StartNewPage();
}

#if 0
struct KnownAttrInfo {
    HtmlAttr        attr;
    const char *    val;
    size_t          valLen;
};

static bool IsAllowedAttribute(HtmlAttr* allowedAttributes, HtmlAttr attr)
{
    while (Attr_NotFound != *allowedAttributes) {
        if (attr == *allowedAttributes++)
            return true;
    }
    return false;
}

static void GetKnownAttributes(HtmlToken *t, HtmlAttr *allowedAttributes, Vec<KnownAttrInfo> *out)
{
    out->Reset();
    AttrInfo *attrInfo;
    size_t tagLen = GetTagLen(t);
    const char *curr = t->s + tagLen;
    const char *end = t->s + t->sLen;
    for (;;) {
        attrInfo = t->NextAttr();
        if (NULL == attrInfo)
            break;
        HtmlAttr attr = FindAttr(attrInfo);
        if (!IsAllowedAttribute(allowedAttributes, attr))
            continue;
        KnownAttrInfo knownAttr = { attr, attrInfo->val, attrInfo->valLen };
        out->Append(knownAttr);
    }
}
#endif

void PageLayout::EmitSetFont(Font *font)
{
    currPage->Append(DrawInstr::SetFont(font));
}

// add horizontal line (<hr> in html terms)
void PageLayout::EmitLine()
{
    // hr creates an implicit paragraph break
    StartNewLine(true);
    currX = 0;
    // height of hr is lineSpacing. If drawing a line would cause
    // current position to exceed page bounds, go to another page
    if (currY + lineSpacing > pageDy)
        StartNewPage();

    RectF bbox(currX, currY, pageDx, lineSpacing);
    currPage->Append(DrawInstr::Line(bbox));
    StartNewLine(true);
}

void PageLayout::EmitSpace()
{
    // don't put spaces at the beginnng of the line
    if (0 == currX)
        return;
    // don't allow consequitive spaces
    if (LastInstrIsSpace())
        return;
    // don't add a space if it would cause creating a new line, but
    // do create a new line
    if (currX + spaceDx > pageDx) {
        StartNewLine(false);
        return;
    }
    currPage->drawInstructions.Append(DrawInstr::Space());
}

bool PageLayout::LastInstrIsSpace() const
{
    if (0 == currPage->Count())
        return false;
    DrawInstr& di = currPage->drawInstructions.Last();
    return InstrSpace == di.type;
}

// a text rune is a string of consequitive text with uniform style
void PageLayout::EmitTextRune(const char *s, const char *end)
{
    // collapse multiple, consequitive white-spaces into a single space
    if (IsSpaceOnly(s, end)) {
        EmitSpace();
        return;
    }

    if (IsNewline(s, end)) {
        // a single newline is considered "soft" and ignored
        // two or more consequitive newlines are considered a
        // single paragraph break
        newLinesCount++;
        if (2 == newLinesCount) {
            bool needsTwo = (currX != 0);
            StartNewLine(true);
            if (needsTwo)
                StartNewLine(true);
        }
        return;
    }
    newLinesCount = 0;

    const char *tmp = ResolveHtmlEntities(s, end, textAllocator);
    if (tmp != s) {
        s = tmp;
        end = s + str::Len(s);
    }

    size_t strLen = str::Utf8ToWcharBuf(s, end - s, buf, dimof(buf));
#if 0 // TODO: cache disabled because produces bad metrics
    // with fontMetrics cache
    if (currFont == fontMetrics.font)
        bbox = MeasureText(gfx, currFont, &fontMetrics, buf, strLen);
    else
        bbox = MeasureText(gfx, currFont, buf, strLen);
#else
    RectF bbox = MeasureText(gfx, currFont, buf, strLen);
#endif
    REAL dx = bbox.Width;
    if (currX + dx > pageDx) {
        // start new line if the new text would exceed the line length
        StartNewLine(false);
        // TODO: handle a case where a single word is bigger than the whole
        // line, in which case it must be split into multiple lines
    }
    bbox.Y = currY;
    currPage->Append(DrawInstr::Str(s, end - s, bbox));
    currX += dx;
}

#if 0
void DumpAttr(uint8 *s, size_t sLen)
{
    static Vec<char *> seen;
    char *sCopy = str::DupN((char*)s, sLen);
    bool didSee = false;
    for (size_t i = 0; i < seen.Count(); i++) {
        char *tmp = seen.At(i);
        if (str::EqI(sCopy, tmp)) {
            didSee = true;
            break;
        }
    }
    if (didSee) {
        free(sCopy);
        return;
    }
    seen.Append(sCopy);
    printf("%s\n", sCopy);
}
#endif

// tags that I want to explicitly ignore and not define
// HtmlTag enums for them
// One file has a bunch of st1:* tags (st1:city, st1:place etc.)
static bool IgnoreTag(const char *s, size_t sLen)
{
    if (sLen >= 4 && s[3] == ':' && s[0] == 's' && s[1] == 't' && s[2] == '1')
        return true;
    // no idea what "o:p" is
    if (sLen == 3 && s[1] == ':' && s[0] == 'o'  && s[2] == 'p')
        return true;
    return false;
}

void PageLayout::HandleHtmlTag(HtmlToken *t)
{
    CrashAlwaysIf(!t->IsTag());

    // HtmlToken string includes potential attributes,
    // get the length of just the tag
    size_t tagLen = GetTagLen(t);
    if (IgnoreTag(t->s, tagLen))
        return;

    HtmlTag tag = FindTag(t);
    // TODO: ignore instead of crashing once we're satisfied we covered all the tags
    CrashIf(tag == Tag_NotFound);

    if (Tag_P == tag) {
        StartNewLine(true);
        currJustification = Align_Justify;
        if (t->IsStartTag() || t->IsEmptyElementEndTag()) {
            AttrInfo *attrInfo = t->GetAttrByName("align");
            if (attrInfo)
                currJustification = FindAlignAttr(attrInfo->val, attrInfo->valLen);
        }
    } else if (Tag_Hr == tag) {
        EmitLine();
    } else if ((Tag_B == tag) || (Tag_Strong == tag)) {
        ChangeFont(FontStyleBold, t->IsStartTag());
    } else if ((Tag_I == tag) || (Tag_Em == tag)) {
        ChangeFont(FontStyleItalic, t->IsStartTag());
    } else if (Tag_U == tag) {
        ChangeFont(FontStyleUnderline, t->IsStartTag());
    } else if (Tag_Strike == tag) {
        ChangeFont(FontStyleStrikeout, t->IsStartTag());
    } else if (Tag_Mbp_Pagebreak == tag) {
        JustifyLine(currJustification);
        StartNewPage();
    } else if (Tag_Br == tag) {
        StartNewLine(false);
    }
}

void PageLayout::EmitText(HtmlToken *t)
{
    CrashIf(!t->IsText());
    const char *end = t->s + t->sLen;
    const char *curr = t->s;
    const char *currStart;
    // break text into runes i.e. chunks taht are either all
    // whitespace or all non-whitespace
    while (curr < end) {
        currStart = curr;
        SkipWs(curr, end);
        if (curr > currStart)
            EmitTextRune(currStart, curr);
        currStart = curr;
        SkipNonWs(curr, end);
        if (curr > currStart)
            EmitTextRune(currStart, curr);
    }
}

// The first instruction in a page is always SetFont() instruction
// so to determine if a page is empty, we check if there's at least
// one "visible" instruction.
static bool IsEmptyPage(PageData *p)
{
    if (!p)
        return true;
    DrawInstr *i;
    for (i = p->drawInstructions.IterStart(); i; i = p->drawInstructions.IterNext()) {
        switch (i->type) {
            case InstrSetFont:
            case InstrLine:
            case InstrSpace:
                // those are "invisible" instruction. I consider a line invisible
                // (which is different to what Kindel app does), but a page
                // with just lines is not very interesting
                break;
            default:
                return false;
        }
    }
    // all instructions were invisible
    return true;
}

// Return the next parsed page. Returns NULL if finished parsing.
// For simplicity of implementation, we parse xml text node or
// xml element at a time. This might cause a creation of one
// or more pages, which we remeber and send to the caller
// if we detect accumulated pages.
PageData *PageLayout::IterNext()
{
    for (;;)
    {
        // send out all pages accumulated so far
        while (pagesToSend.Count() > 0) {
            PageData *ret = pagesToSend.At(0);
            pagesToSend.RemoveAt(0);
            if (!IsEmptyPage(ret))
                return ret;
            else
                delete ret;
        }
        // we can call ourselves recursively to send outstanding
        // pages after parsing has finished so this is to detect
        // that case and really end parsing
        if (finishedParsing)
            return NULL;
        HtmlToken *t = htmlParser->Next();
        if (!t || t->IsError())
            break;

        if (t->IsTag())
            HandleHtmlTag(t);
        else
            EmitText(t);
    }
    // force layout of the last line
    StartNewLine(true);

    pagesToSend.Append(currPage);
    currPage = NULL;
    // call ourselves recursively to return accumulated pages
    finishedParsing = true;
    return IterNext();
}

Vec<PageData*> *LayoutHtml(LayoutInfo* li)
{
    Vec<PageData*> *pages = new Vec<PageData*>();
    PageLayout l;
    for (PageData *pd = l.IterStart(li); pd; pd = l.IterNext()) {
        pages->Append(pd);
    }
    return pages;
}

void DrawPageLayout(Graphics *g, Vec<DrawInstr> *drawInstructions, REAL offX, REAL offY, bool showBbox)
{
    StringFormat sf(StringFormat::GenericTypographic());
    //SolidBrush br(Color(0,0,0));
    SolidBrush br(Color(0x5F, 0x4B, 0x32)); // this color matches Kindle app
    Pen pen(Color(255, 0, 0), 1);
    Pen blackPen(Color(0, 0, 0), 1);

    Font *font = NULL;

    WCHAR buf[512];
    PointF pos;
    DrawInstr *i;
    for (i = drawInstructions->IterStart(); i; i = drawInstructions->IterNext()) {
        RectF bbox = i->bbox;
        bbox.X += offX;
        bbox.Y += offY;
        if (InstrLine == i->type) {
            // hr is a line drawn in the middle of bounding box
            REAL y = bbox.Y + bbox.Height / 2.f;
            PointF p1(bbox.X, y);
            PointF p2(bbox.X + bbox.Width, y);
            if (showBbox) {
                //g->FillRectangle(&br, bbox);
                g->DrawRectangle(&pen, bbox);
            }
            g->DrawLine(&blackPen, p1, p2);
        } else if (InstrString == i->type) {
            size_t strLen = str::Utf8ToWcharBuf(i->str.s, i->str.len, buf, dimof(buf));
            bbox.GetLocation(&pos);
            if (showBbox) {
                //g->FillRectangle(&br, bbox);
                g->DrawRectangle(&pen, bbox);
            }
            g->DrawString(buf, strLen, font, pos, NULL, &br);
        } else if (InstrSetFont == i->type) {
            font = i->setFont.font;
        } else if (InstrSpace == i->type) {
            // ignore
        } else {
            CrashIf(true);
        }
    }
}
