/* Copyright Krzysztof Kowalczyk 2006-2009
   License: GPLv3 */
#include "DisplayState.h"
#include "tstr_util.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

void normalizeRotation(int *rotation)
{
    assert(rotation);
    if (!rotation) return;
    *rotation = *rotation % 360;
    if (*rotation < 0)
        *rotation += 360;
}

BOOL validRotation(int rotation)
{
    normalizeRotation(&rotation);
    if ((0 == rotation) || (90 == rotation) ||
        (180 == rotation) || (270 == rotation))
        return TRUE;
    return FALSE;
}

BOOL ValidZoomVirtual(double zoomVirtual)
{
    if ((ZOOM_FIT_PAGE == zoomVirtual) || (ZOOM_FIT_WIDTH == zoomVirtual) || (ZOOM_ACTUAL_SIZE == zoomVirtual))
        return TRUE;
    if ((zoomVirtual < ZOOM_MIN) || (zoomVirtual > ZOOM_MAX)) {
        DBG_OUT("ValidZoomVirtual() invalid zoom: %.4f\n", zoomVirtual);
        return FALSE;
    }
    return TRUE;
}

#define STR_FROM_ENUM(val) \
    if (val == var) \
        return val##_STR;

const char *DisplayModeNameFromEnum(DisplayMode var)
{
    STR_FROM_ENUM(DM_AUTOMATIC)
    STR_FROM_ENUM(DM_SINGLE_PAGE)
    STR_FROM_ENUM(DM_FACING)
    STR_FROM_ENUM(DM_BOOK_VIEW)
    STR_FROM_ENUM(DM_CONTINUOUS)
    STR_FROM_ENUM(DM_CONTINUOUS_FACING)
    STR_FROM_ENUM(DM_CONTINUOUS_BOOK_VIEW)
    return NULL;
}

#define IS_STR_ENUM(enumName) \
    if (str_eq(txt, enumName##_STR)) { \
        *resOut = enumName; \
        return true; \
    }

bool DisplayModeEnumFromName(const char *txt, DisplayMode *resOut)
{
    IS_STR_ENUM(DM_AUTOMATIC)
    IS_STR_ENUM(DM_SINGLE_PAGE)
    IS_STR_ENUM(DM_FACING)
    IS_STR_ENUM(DM_BOOK_VIEW)
    IS_STR_ENUM(DM_CONTINUOUS)
    IS_STR_ENUM(DM_CONTINUOUS_FACING)
    IS_STR_ENUM(DM_CONTINUOUS_BOOK_VIEW)
    assert(0);
    return false;
}

void DisplayState_Init(DisplayState *ds)
{
    ZeroMemory(ds, sizeof(DisplayState));
    ds->displayMode = DM_AUTOMATIC;
    ds->pageNo = 1;
    ds->zoomVirtual = 100.0;
    ds->rotation = 0;
    ds->showToc = TRUE;
}

void DisplayState_Free(DisplayState *ds)
{
    free((void*)ds->filePath);
    free((void*)ds->decryptionKey);
    DisplayState_Init(ds);
}

#if 0
bool DisplayState_Serialize(DisplayState *ds, DString *strOut)
{
    const char *        displayModeName = NULL;

    DStringSprintf(strOut, "  %s: %s\n", FILE_STR, ds->filePath);

    displayModeName = DisplayModeNameFromEnum(ds->displayMode);
    if (displayModeName)
        DStringSprintf(strOut, "  %s: %s\n", DISPLAY_MODE_STR, displayModeName);
    else
        DStringSprintf(strOut, "  %s: %s\n", DISPLAY_MODE_STR, DisplayModeNameFromEnum(DM_AUTOMATIC));

    DStringSprintf(strOut, "  %s: %d\n",   PAGE_NO_STR, ds->pageNo);
    DStringSprintf(strOut, "  %s: %.4f\n", ZOOM_VIRTUAL_STR, ds->zoomVirtual);
    DStringSprintf(strOut, "  %s: %d\n",   ROTATION_STR, ds->rotation);

    DStringSprintf(strOut, "  %s: %d\n",   SCROLL_X_STR, ds->scrollX);
    DStringSprintf(strOut, "  %s: %d\n",   SCROLL_Y_STR, ds->scrollY);

    DStringSprintf(strOut, "  %s: %d\n",   WINDOW_X_STR, ds->windowX);
    DStringSprintf(strOut, "  %s: %d\n",   WINDOW_Y_STR, ds->windowY);
    DStringSprintf(strOut, "  %s: %d\n",   WINDOW_DX_STR, ds->windowDx);
    DStringSprintf(strOut, "  %s: %d\n",   WINDOW_DY_STR, ds->windowDy);
    return TRUE;
}
#endif
