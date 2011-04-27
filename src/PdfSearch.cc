#include "PdfSearch.h"
#include <shlwapi.h>

#define SkipWhitespace(c) for (; _istspace(*(c)); (c)++)
#define islatinalnum(c) (_istalnum(c) && (unsigned short)(c) < 256)
#define iswordchar(c) _istalnum(c)

PdfSearch::PdfSearch(PdfEngine *engine, PdfSearchTracker *tracker) : PdfSelection(engine),
    tracker(tracker), text(NULL), anchor(NULL), pageText(NULL),
    caseSensitive(false), wholeWords(false), forward(true),
    findPage(0), findIndex(0) { }

PdfSearch::~PdfSearch()
{
    Clear();
}

void PdfSearch::Reset()
{
    if (pageText) {
        free(pageText);
        pageText = NULL;
    }
    PdfSelection::Reset();
}

void PdfSearch::SetText(TCHAR *text)
{
    this->Clear();

    // all whitespace characters before the first word will be ignored
    // (we're similarly fuzzy about whitespace as Adobe Reader in this regard)
    SkipWhitespace(text);
    this->text = tstr_dup(text);

    // extract anchor string (the first word or the first symbol) for faster searching
    if (islatinalnum(*text)) {
        TCHAR *end;
        for (end = text; islatinalnum(*end); end++);
        this->anchor = tstr_dupn(text, end - text);
    }
    else
        this->anchor = tstr_dupn(text, 1);

    // search text ending in a single space enables the 'Whole words' option
    // (that behavior already "kind of" exists without special treatment, but
    // usually is not quite what a user expects, so let's try to be cleverer)
    this->wholeWords = false;
    if (tstr_endswith(text, _T(" "))) {
        this->wholeWords = !tstr_endswith(text, _T("  "));
        this->text[tstr_len(this->text) - 1] = '\0';
    }
}

void PdfSearch::SetDirection(bool forward)
{
    if (forward == this->forward)
        return;
    this->forward = forward;
    findIndex += lstrlen(text) * (forward ? 1 : -1);
}

// try to match "text" from "start" with whitespace tolerance
// (ignore all whitespace except after alphanumeric characters)
int PdfSearch::MatchLen(TCHAR *start)
{
    TCHAR *match = text, *end = start;
    assert(!_istspace(*end));

    if (wholeWords && start > pageText && iswordchar(start[-1]) && iswordchar(start[0]))
        return -1;

    while (*match) {
        if (!*end)
            return -1;
        if (caseSensitive ? *match != *end : _totlower(*match) != _totlower(*end))
            return -1;
        match++;
        end++;
        if (!islatinalnum(*(match - 1)) || _istspace(*(match - 1)) && _istspace(*(end - 1))) {
            SkipWhitespace(match);
            SkipWhitespace(end);
        }
    }

    if (wholeWords && end > pageText && iswordchar(end[-1]) && iswordchar(end[0]))
        return -1;

    return end - start;
}

// TODO: use Boyer-Moore algorithm here (if it proves to be faster)
bool PdfSearch::FindTextInPage(int pageNo)
{
    if (tstr_empty(anchor))
        return false;
    if (!pageNo)
        pageNo = findPage;
    findPage = pageNo;

    TCHAR *found;
    int length;
    do {
        if (forward)
            found = (caseSensitive ? StrStr : StrStrI)(pageText + findIndex, anchor);
        else
            found = StrRStrI(pageText, pageText + findIndex, anchor);
        if (!found)
            return false;
        findIndex = found - pageText + (forward ? 1 : 0);
        length = MatchLen(found);
    } while (length <= 0);

    StartAt(pageNo, found - pageText);
    SelectUpTo(pageNo, found - pageText + length);
    findIndex = found - pageText + (forward ? length : 0);

    // try again if the found text is completely outside the page's mediabox
    if (result.len == 0)
        return FindTextInPage(pageNo);

    return true;
}

bool PdfSearch::FindStartingAtPage(int pageNo)
{
    if (tstr_empty(anchor))
        return false;

    int total = engine->pageCount();
    while (1 <= pageNo && pageNo <= total && UpdateTracker(pageNo, total)) {
        Reset();

        fz_bbox **pcoords = !coords[pageNo - 1] ? &coords[pageNo - 1] : NULL;
        pageText = engine->ExtractPageText(pageNo, _T(" "), pcoords);
        findIndex = forward ? 0 : lstrlen(pageText);

        if (pageText && FindTextInPage(pageNo))
            return true;

        pageNo += forward ? 1 : -1;
    }
    
    // allow for the first/last page to be included in the next search
    findPage = forward ? total + 1 : 0;

    return false;
}

bool PdfSearch::FindFirst(int page, TCHAR *text)
{
    SetText(text);

    return FindStartingAtPage(page);
}

bool PdfSearch::FindNext()
{
    if (FindTextInPage())
        return true;

    int newPage = findPage + (forward ? 1 : -1);
    return FindStartingAtPage(newPage);
}
