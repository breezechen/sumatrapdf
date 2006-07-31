#include "AppPrefs.h"
#include "dstring.h"
#include "DisplayState.h"
#include "FileHistory.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#define DEFAULT_WINDOW_X     40
#define DEFAULT_WINDOW_Y     20
#define DEFAULT_WINDOW_DX    640
#define DEFAULT_WINDOW_DY    480

BOOL Prefs_Serialize(DisplayState *ds, FileHistoryList **root, DString *strOut)
{
    int     fOk;

    if (ds) {
        DStringSprintf(strOut, "%s:\n", STATE_STR);
        fOk = DisplayState_Serialize(ds, strOut);
        if (!fOk)
            return FALSE;
    }

    fOk = FileHistoryList_Serialize(root, strOut);
    return fOk;
}

static BOOL ParseDisplayMode(const char *txt, DisplayMode *resOut)
{
    return DisplayModeEnumFromName(txt, resOut);
}

static BOOL ParseDouble(const char *txt, double *resOut)
{
    int res = sscanf(txt, "%lf", resOut);
    if (1 != res)
        return FALSE;
    return TRUE;
}

static BOOL ParseInt(const char *txt, int *resOut)
{
    *resOut = atoi(txt);
    return TRUE;
}

static BOOL ParseBool(const char *txt, BOOL *resOut)
{
    int val = atoi(txt);
    if (val)
        *resOut = TRUE;
    else
        *resOut = FALSE;
    return TRUE;
}

enum PrefsParsingState { PPS_START, PPS_IN_STATE, PPS_IN_FILE_HISTORY };

/* Return TRUE if 'str' is a comment line in preferences file.
   Comment lines start with '#'. */
static int Prefs_LineIsComment(const char *str)
{
    if (!str)
        return FALSE;
    if ('#' == *str)
        return TRUE;
    return FALSE;
}

static int Prefs_LineIsStructKey(const char *str)
{
    if (strlen(str) <= 3)
        return FALSE;
    if ((' ' == str[0]) && (' ' == str[1]))
        return TRUE;
    return FALSE;
}

/* Deserialize preferences from text. Put state into 'dsOut' and add all history
   items to file history list 'root'.
   Return FALSE if there was an error.
   An ode to a state machine. */
BOOL Prefs_Deserialize(const char *prefsTxt, DisplayState *dsOut, FileHistoryList **fileHistoryRoot)
{
    PrefsParsingState   state = PPS_START;
    char *              prefsTxtNormalized = NULL;
    char *              line;
    char *              key, *value, *keyToFree = NULL;
    int                 isStructVal;
    DisplayState        currState;
    FileHistoryList *   fileHistoryNode = NULL;
    BOOL                fOk;

    DisplayState_Init(&currState);

    prefsTxtNormalized = Str_NormalizeNewline(prefsTxt, UNIX_NEWLINE);
    if (!prefsTxtNormalized)
        goto Exit;

    for (;;) {
        line = Str_SplitIter(&prefsTxtNormalized, UNIX_NEWLINE_C);
        if (!line)
            break;
        Str_StripWsRight(line);

        /* skip empty and comment lines*/
        if (Str_Empty(line))
            goto Next;
        if (Prefs_LineIsComment(line))
            goto Next;

        /* each line is key/value pair formatted as: "key: value"
           value is optional. If value exists, there must
           be a space after ':' */
        value = line;
        keyToFree = Str_SplitIter(&value, ':');
        key = keyToFree;
        assert(key);
        if (!key)
            goto Next;
        if (Str_Empty(value)) {
            value = NULL; /* there was no value */
        } else {
            assert(' ' == *value);
            if (' ' != *value)
                goto Next;
            value += 1;
        }
        isStructVal = Prefs_LineIsStructKey(key);
        if (isStructVal)
            key += 2;

StartOver:
        switch (state) {
            case PPS_START:
                if (Str_Eq(STATE_STR, key)) {
                    assert(!isStructVal);
                    state = PPS_IN_STATE;
                    assert(!value);
                } else if (Str_Eq(FILE_HISTORY_STR, key)) {
                    assert(!isStructVal);
                    state = PPS_IN_FILE_HISTORY;
                    assert(!fileHistoryNode);
                    assert(!value);
                } else {
                    if (line)
                        DBG_OUT("  in state PPS_START, line='%s'  \n\n", line);
                    else
                        DBG_OUT("  in state PPS_START, line is NULL\n\n");
                    assert(0);
                }
                break;

            case PPS_IN_STATE:
                if (isStructVal)
                    goto ParseStateItem;

                *dsOut = currState;

                DisplayState_Init(&currState);
                state = PPS_START;
                goto StartOver;

            case PPS_IN_FILE_HISTORY:
                if (isStructVal)
                    goto ParseStateItem;

                if (currState.filePath) {
                    fileHistoryNode = FileHistoryList_Node_Create();
                    fileHistoryNode->state = currState;
                    FileHistoryList_Node_Append(fileHistoryRoot, fileHistoryNode);
                    fileHistoryNode = NULL;
                }
                DisplayState_Init(&currState);
                state = PPS_START;
                goto StartOver;

ParseStateItem:
                /* TODO: move this code into a function */
                if (Str_Eq(FILE_STR, key)) {
                    if (!value)
                        goto Next;
                    assert(!currState.filePath);
                    free((void*)currState.filePath);
                    currState.filePath = Str_Dup(value);
                } else if (Str_Eq(DISPLAY_MODE_STR, key)) {
                    fOk = ParseDisplayMode(value, &currState.displayMode);
                    if (!fOk)
                        currState.displayMode = DM_SINGLE_PAGE;
                } else if (Str_Eq(PAGE_NO_STR, key)) {
                    assert(value);
                    if (!value)
                        goto Next;
                    fOk = ParseInt(value, &currState.pageNo);
                    if (!fOk || (currState.pageNo < 1))
                        currState.pageNo = 1;
                } else if (Str_Eq(ZOOM_VIRTUAL_STR, key)) {
                    assert(value);
                    if (!value)
                        goto Next;
                    fOk = ParseDouble(value, &currState.zoomVirtual);
                    if (!fOk || !ValidZoomVirtual(currState.zoomVirtual))
                        currState.zoomVirtual = 100.0;
                } else if (Str_Eq(ROTATION_STR, key)) {
                    assert(value);
                    if (!value)
                        goto Next;
                    fOk = ParseInt(value, &currState.rotation);
                    if (!fOk || !ValidRotation(currState.rotation))
                        currState.rotation = 0;
                } else if (Str_Eq(FULLSCREEN_STR, key)) {
                    assert(value);
                    if (!value)
                        goto Next;
                    fOk = ParseBool(value, &currState.fullScreen);
                    if (!fOk)
                        currState.fullScreen = FALSE;
                } else if (Str_Eq(SCROLL_X_STR, key)) {
                    assert(value);
                    if (!value)
                        goto Next;
                    fOk = ParseInt(value, &currState.scrollX);
                    if (!fOk)
                        currState.scrollX = 0;
                } else if (Str_Eq(SCROLL_Y_STR, key)) {
                    assert(value);
                    if (!value)
                        goto Next;
                    fOk = ParseInt(value, &currState.scrollY);
                    if (!fOk)
                        currState.scrollY = 0;
                } else if (Str_Eq(WINDOW_X_STR, key)) {
                    assert(value);
                    if (!value)
                        goto Next;
                    fOk = ParseInt(value, &currState.windowX);
                    if (!fOk)
                        currState.windowX = DEFAULT_WINDOW_X;
                } else if (Str_Eq(WINDOW_Y_STR, key)) {
                    assert(value);
                    if (!value)
                        goto Next;
                    fOk = ParseInt(value, &currState.windowY);
                    if (!fOk)
                        currState.windowY = DEFAULT_WINDOW_Y;
                } else if (Str_Eq(WINDOW_DX_STR, key)) {
                    assert(value);
                    if (!value)
                        goto Next;
                    fOk = ParseInt(value, &currState.windowDx);
                    if (!fOk)
                        currState.windowDx = DEFAULT_WINDOW_DX;
                } else if (Str_Eq(WINDOW_DY_STR, key)) {
                    assert(value);
                    if (!value)
                        goto Next;
                    fOk = ParseInt(value, &currState.windowDy);
                    if (!fOk)
                        currState.windowDy = DEFAULT_WINDOW_DY;
                } else {
                    assert(0);
                }
                break;

        }
Next:
        free((void*)keyToFree);
        keyToFree = NULL;
        free((void*)line);
        line = NULL;
    }

    if (PPS_IN_FILE_HISTORY == state) {
        if (currState.filePath) {
            fileHistoryNode = FileHistoryList_Node_Create();
            fileHistoryNode->state = currState;
            DisplayState_Init(&currState);
            FileHistoryList_Node_Append(fileHistoryRoot, fileHistoryNode);
            fileHistoryNode = NULL;
        }
    } else if (PPS_IN_STATE == state) {
        *dsOut = currState;
    }

Exit:
    free((void*)prefsTxtNormalized);
    return TRUE;
}

