#ifndef APP_PREFS_H_
#define APP_PREFS_H_

#include "DisplayState.h"
#include "FileHistory.h"

#include "BaseUtils.h"

BOOL    Prefs_Serialize(DisplayState *ds, FileHistoryList **root, DString *strOut);
BOOL    Prefs_Deserialize(const char *prefsTxt, DisplayState *dsOut, FileHistoryList **fileHistoryRoot);

#endif

