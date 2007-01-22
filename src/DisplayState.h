#ifndef DISPLAY_STATE_H_
#define DISPLAY_STATE_H_

#include "BaseUtils.h"
#include "dstring.h"

#define ZOOM_FIT_PAGE       -1
#define ZOOM_FIT_WIDTH      -2
#define ZOOM_MAX            1600.0  /* max zoom in % */
#define ZOOM_MIN            10.0    /* min zoom in % */

#define DM_SINGLE_PAGE_STR          "single page"
#define DM_FACING_STR               "facing"
#define DM_CONTINUOUS_STR           "continuous"
#define DM_CONTINUOUS_FACING_STR    "continuous facing"

#define FILE_HISTORY_STR            "File History"

#define FILE_STR                    "File"
#define DISPLAY_MODE_STR            "Display Mode"
#define VISIBLE_STR                 "Visible"
#define PAGE_NO_STR                 "Page"
#define ZOOM_VIRTUAL_STR            "ZoomVirtual"
#define ROTATION_STR                "Rotation"
#define FULLSCREEN_STR              "Fullscreen"
#define SCROLL_X_STR                "Scroll X"
#define SCROLL_Y_STR                "Scroll Y"
#define WINDOW_X_STR                "Window X"
#define WINDOW_Y_STR                "Window Y"
#define WINDOW_DX_STR               "Window DX"
#define WINDOW_DY_STR               "Window DY"
#define SHOW_TOOLBAR_STR            "ShowToolbar"
#define PDF_ASSOCIATE_DONT_ASK_STR  "PdfAssociateDontAskAgain"
#define PDF_ASSOCIATE_ASSOCIATE     "PdfAssociateShouldAssociate"

typedef struct DisplayState {
    const char *        filePath;
    enum DisplayMode    displayMode;
    BOOL                visible;     /* if TRUE, currently shown on the screen */
    int                 scrollX;
    int                 scrollY;
    int                 pageNo;
    double              zoomVirtual;
    int                 rotation;
    BOOL                fullScreen;
    int                 windowX;
    int                 windowY;
    int                 windowDx;
    int                 windowDy;
} DisplayState;

void    NormalizeRotation(int *rotation);
BOOL    ValidRotation(int rotation);
BOOL    ValidZoomVirtual(double zoomVirtual);

const char *      DisplayModeNameFromEnum(DisplayMode var);
BOOL              DisplayModeEnumFromName(const char *txt, DisplayMode *resOut);

void    DisplayState_Init(DisplayState *ds);
void    DisplayState_Free(DisplayState *ds);
BOOL    DisplayState_Serialize(DisplayState *ds, DString *strOut);

#endif

