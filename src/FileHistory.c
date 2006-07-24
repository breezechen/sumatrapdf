#include "FileHistory.h"

/* Handling of file history list.

   We keep an infinite list of all (still existing in the file system) PDF
   files that a user has ever opened. For each file we also keep a bunch of
   attributes describing the display state at the time the file was closed.

   We persist this list as serialized text inside preferences file.
   Serialized history list looks like this:

FileHistoryNode:
FilePath: /home/test.pdf
PageNo: 5
ZoomLevel: 123.34
DisplayMode: 1
FullScreen: 1
LastAccessTimeInSecsSinceEpoch: 12341234124314

FileHistoryNode:

etc...

    We deserialize this info at startup and serialize when the application
    quits.
*/

/* This is really a job for dynmic string */
char *FileHistoryList_Serialize(FileHistoryList **root)
{

    return NULL;
}

char *FileHistoryList_SerializeNode(FileHistoryList *node)
{
    return NULL;
}

int FileHistoryList_AppendNode(FileHistoryList **root, FileHistoryList *node)
{
    return FALSE;
}

FileHistoryList *FileHistoryList_FindNodeByFilePath(FileHistoryList **root, const char *filePath)
{
    return NULL;
}

int FileHistoryList_RemoveNode(FileHistoryList **root, FileHistoryList *node)
{

    return FALSE;
}

