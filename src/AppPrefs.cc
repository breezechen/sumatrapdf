#include "AppPrefs.h"
#include "dstring.h"
#include "DisplayState.h"
#include "FileHistory.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <assert.h>

#define DEFAULT_WINDOW_X     40
#define DEFAULT_WINDOW_Y     20
#define DEFAULT_WINDOW_DX    640
#define DEFAULT_WINDOW_DY    480

static BOOL FileExists(const char *fileName)
{
  struct stat buf;
  int         res;
  
  res = stat(fileName, &buf);
  if (0 != res)
    return FALSE;
  return TRUE;
}

BOOL Prefs_Serialize(FileHistoryList **root, DString *strOut)
{
    assert(0 == strOut->length);
    return FileHistoryList_Serialize(root, strOut);
}

static BOOL ParseDisplayMode(const char *txt, DisplayMode *resOut)
{
    assert(txt);
    if (!txt) return FALSE;
    return DisplayModeEnumFromName(txt, resOut);
}

static BOOL ParseDouble(const char *txt, double *resOut)
{
    int res;

    assert(txt);
    if (!txt) return FALSE;

    res = sscanf(txt, "%lf", resOut);
    if (1 != res)
        return FALSE;
    return TRUE;
}

static BOOL ParseInt(const char *txt, int *resOut)
{
    assert(txt);
    if (!txt) return FALSE;
    *resOut = atoi(txt);
    return TRUE;
}

static BOOL ParseBool(const char *txt, BOOL *resOut)
{
    assert(txt);
    if (!txt) return FALSE;
    int val = atoi(txt);
    if (val)
        *resOut = TRUE;
    else
        *resOut = FALSE;
    return TRUE;
}

enum PrefsParsingState { PPS_START, PPS_IN_FILE_HISTORY };

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

static void ParseKeyValue(char *key, char *value, DisplayState *dsOut)
{
    BOOL    fOk;

    assert(key);
    assert(value);
    assert(dsOut);
    if (!key || !value || !dsOut)
        return;

    if (Str_Eq(FILE_STR, key)) {
        assert(value);
        if (!value) return;
        assert(!dsOut->filePath);
        free((void*)dsOut->filePath);
        dsOut->filePath = Str_Dup(value);
        return;
    }

    if (Str_Eq(DISPLAY_MODE_STR, key)) {
        dsOut->displayMode = DM_SINGLE_PAGE;
        fOk = ParseDisplayMode(value, &dsOut->displayMode);
        assert(fOk);
        return;
    }

    if (Str_Eq(PAGE_NO_STR, key)) {
        fOk = ParseInt(value, &dsOut->pageNo);
        assert(fOk);
        if (!fOk || (dsOut->pageNo < 1))
            dsOut->pageNo = 1;
        return;
    }

    if (Str_Eq(ZOOM_VIRTUAL_STR, key)) {
        fOk = ParseDouble(value, &dsOut->zoomVirtual);
        assert(fOk);
        if (!fOk || !ValidZoomVirtual(dsOut->zoomVirtual))
            dsOut->zoomVirtual = 100.0;
        return;
    }

    if (Str_Eq(ROTATION_STR, key)) {
        fOk = ParseInt(value, &dsOut->rotation);
        assert(fOk);
        if (!fOk || !ValidRotation(dsOut->rotation))
            dsOut->rotation = 0;
        return;
    }

    if (Str_Eq(VISIBLE_STR, key)) {
        dsOut->visible= FALSE;
        fOk = ParseBool(value, &dsOut->visible);
        assert(fOk);
        return;
    }

    if (Str_Eq(FULLSCREEN_STR, key)) {
        dsOut->fullScreen = FALSE;
        fOk = ParseBool(value, &dsOut->fullScreen);
        assert(fOk);
        return;
    }

    if (Str_Eq(SCROLL_X_STR, key)) {
        dsOut->scrollX = 0;
        fOk = ParseInt(value, &dsOut->scrollX);
        assert(fOk);
        return;
    }

    if (Str_Eq(SCROLL_Y_STR, key)) {
        dsOut->scrollY = 0;
        fOk = ParseInt(value, &dsOut->scrollY);
        assert(fOk);
        return;
    }

    if (Str_Eq(WINDOW_X_STR, key)) {
        dsOut->windowX = DEFAULT_WINDOW_X;
        fOk = ParseInt(value, &dsOut->windowX);
        assert(fOk);
        return;
    } 

    if (Str_Eq(WINDOW_Y_STR, key)) {
        dsOut->windowY = DEFAULT_WINDOW_Y;
        fOk = ParseInt(value, &dsOut->windowY);
        assert(fOk);
        return;
    }

    if (Str_Eq(WINDOW_DX_STR, key)) {
        dsOut->windowDx = DEFAULT_WINDOW_DX;
        fOk = ParseInt(value, &dsOut->windowDx);
        assert(fOk);
        return;
    }

    if (Str_Eq(WINDOW_DY_STR, key)) {
        dsOut->windowDy = DEFAULT_WINDOW_DY;
        fOk = ParseInt(value, &dsOut->windowDy);
        assert(fOk);
        return;
    }
    assert(0);
}

void FileHistory_Add(FileHistoryList **fileHistoryRoot, DisplayState *state)
{
    FileHistoryList *   fileHistoryNode = NULL;
    if (!FileExists(state->filePath)) {
        DBG_OUT("FileHistory_Add() file '%s' doesn't exist anymore\n", state->filePath);
        return;
    }

    fileHistoryNode = FileHistoryList_Node_Create();
    fileHistoryNode->state = *state;
    FileHistoryList_Node_Append(fileHistoryRoot, fileHistoryNode);
    fileHistoryNode = NULL;
}

/* Deserialize preferences from text. Put state into 'dsOut' and add all history
   items to file history list 'root'.
   Return FALSE if there was an error.
   An ode to a state machine. */
BOOL Prefs_Deserialize(const char *prefsTxt, FileHistoryList **fileHistoryRoot)
{
    PrefsParsingState   state = PPS_START;
    char *              prefsTxtNormalized = NULL;
    char *              line;
    char *              key, *value, *keyToFree = NULL;
    int                 isStructVal;
    DisplayState        currState;

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
                if (Str_Eq(FILE_HISTORY_STR, key)) {
                    assert(!isStructVal);
                    state = PPS_IN_FILE_HISTORY;
                    assert(!value);
                } else {
                    if (line)
                        DBG_OUT("  in state PPS_START, line='%s'  \n\n", line);
                    else
                        DBG_OUT("  in state PPS_START, line is NULL\n\n");
                    assert(0);
                }
                break;

            case PPS_IN_FILE_HISTORY:
                if (isStructVal) {
                    ParseKeyValue(key, value, &currState);
                } else {
                    if (currState.filePath) {
                        FileHistory_Add(fileHistoryRoot, &currState);
                        DisplayState_Init(&currState);
                    }
                    state = PPS_START;
                    goto StartOver;
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
            if (FileExists(currState.filePath))
                FileHistory_Add(fileHistoryRoot, &currState);
        }
    }
Exit:
    free((void*)prefsTxtNormalized);
    return TRUE;
}

