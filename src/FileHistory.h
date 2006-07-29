#ifndef FILE_HISTORY_H_
#define FILE_HISTORY_H_

#include "BaseUtils.h"
#include "DisplayModel.h"
#include "dstring.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* TODO: those should be in a different file, since those are strings used
         for serializing stuff */
#define DM_CONTINUOUS_STR           "continuous"
#define DM_SINGLE_PAGE_STR          "single page"
#define DM_FACING_STR               "facing"
#define DM_CONTINUOUS_FACING_STR    "continuous facing"

#define FILE_STR                    "File"
#define STATE_STR                   "State"
#define DISPLAY_MODE_STR            "Display Mode"
#define PAGE_NO_STR                 "Page"
#define ZOOM_STR                    "Zoom"
#define ROTATION_STR                "Rotation"
#define FULLSCREEN_STR              "Fullscreen"

#define FILE_HISTORY_STR            "File History"

#define INVALID_MENU_ID (unsigned int)-1

typedef struct FileHistoryList {
    struct FileHistoryList *next;
    unsigned int            menuId;
    const char *            filePath;

#if 0
    /* TODO: add those when I feel like it */
    DisplayMode         displayMode;
    int                 scrollX;
    int                 scrollY;
    int                 pageNo;
    double              zoomLevel;
    int                 rotation;
    int                 fullScreen;
#endif
} FileHistoryList;

const char *DisplayModeNameFromEnum(DisplayMode var);
BOOL        DisplayModeEnumFromName(const char *txt, DisplayMode *resOut);

FileHistoryList * FileHistoryList_Node_Create(void);
FileHistoryList * FileHistoryList_Node_CreateFromFilePath(const char *filePath);

void              FileHistoryList_Node_Free(FileHistoryList *node);
void              FileHistoryList_Free(FileHistoryList **root);

void              FileHistoryList_Node_InsertHead(FileHistoryList **root, FileHistoryList *node);
void              FileHistoryList_Node_Append(FileHistoryList **root, FileHistoryList *node);

FileHistoryList * FileHistoryList_Node_FindByFilePath(FileHistoryList **root, const char *filePath);
BOOL              FileHistoryList_Node_RemoveAndFree(FileHistoryList **root, FileHistoryList *node);

BOOL              FileHistoryList_Node_RemoveByFilePath(FileHistoryList **root, const char *filePath);

BOOL              FileHistoryList_Serialize(FileHistoryList **root, DString_t *strOut);

#ifdef __cplusplus
}
#endif

#endif
