#ifndef FILE_HISTORY_H_
#define FILE_HISTORY_H_

#include "BaseUtils.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct FileHistoryList {
    struct FileHistoryList *next;

    const char *    pdfFilePath;
    enum PdfDisplayMode  displayMode;
    int             scrollX;
    int             scrollY;
    int             pageNo;
    double          zoomLevel;
    int             rotation;
    int             fullScreen;
    double          lastAccessTimeInSecsSinceEpoch;
} FileHistoryList;

char *            FileHistoryList_Serialize(FileHistoryList **root);
char *            FileHistoryList_SerializeNode(FileHistoryList *node);
int               FileHistoryList_AppendNode(FileHistoryList **root, FileHistoryList *node);
FileHistoryList * FileHistoryList_FindNodeByFilePath(FileHistoryList **root, const char *filePath);
int               FileHistoryList_RemoveNode(FileHistoryList **root, FileHistoryList *node);


#ifdef __cplusplus
}
#endif

#endif
