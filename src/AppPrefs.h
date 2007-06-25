/* Copyright Krzysztof Kowalczyk 2006-2007
   License: GPLv2 */
#ifndef APP_PREFS_H_
#define APP_PREFS_H_

#include "DisplayState.h"
#include "FileHistory.h"

BOOL    Prefs_Serialize(FileHistoryList **root, DString *strOut);
BOOL    Prefs_Deserialize(const char *prefsTxt, FileHistoryList **fileHistoryRoot);

#endif

