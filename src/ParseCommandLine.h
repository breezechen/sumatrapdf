/* Copyright 2006-2011 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#ifndef ParseCommandLine_h
#define ParseCommandLine_h

#include "DisplayState.h"
#include "Vec.h"

class CommandLineInfo {
public:
    StrVec      fileNames;
    // filesToBenchmark contain 2 strings per each file to benchmark:
    // - name of the file to benchmark
    // - optional (NULL if not available) string that represents which pages
    //   to benchmark. It can also be a string "loadonly" which means we'll
    //   only benchmark loading of the catalog
    StrVec      filesToBenchmark;
    bool        makeDefault;
    bool        exitOnPrint;
    bool        printDialog;
    TCHAR *     printerName;
    int         bgColor;
    TCHAR *     inverseSearchCmdLine;
    struct {
        int     offset;
        int     width;
        int     color;
        bool    permanent;
    } fwdSearch;
    bool        escToExit;
    bool        reuseInstance;
    char *      lang;
    TCHAR *     destName;
    int         pageNumber;
    bool        restrictedUse;
    bool        invertColors;
    bool        enterPresentation;
    bool        enterFullscreen;
    DisplayMode startView;
    float       startZoom;
    PointI      startScroll;
    bool        showConsole;
    HWND        hwndPluginParent;
    TCHAR *     pluginURL;
    bool        exitImmediately;
    bool        silent;

    // stress-testing related
    TCHAR *     stressTestPath;
    TCHAR *     stressTestFilter; // NULL is equivalent to "*" (i.e. all files)
    TCHAR *     stressTestRanges;
    int         stressTestCycles;

    CommandLineInfo() : makeDefault(false), exitOnPrint(false), printDialog(false),
        printerName(NULL), bgColor(-1), inverseSearchCmdLine(NULL),
        escToExit(false), reuseInstance(false), lang(NULL),
        destName(NULL), pageNumber(-1),
        restrictedUse(false), invertColors(false), pluginURL(NULL),
        enterPresentation(false), enterFullscreen(false), hwndPluginParent(NULL),
        startView(DM_AUTOMATIC), startZoom(INVALID_ZOOM), startScroll(PointI(-1, -1)),
        showConsole(false), exitImmediately(false), silent(false),
        stressTestPath(NULL), stressTestFilter(NULL),
        stressTestRanges(NULL), stressTestCycles(1)
    { }

    ~CommandLineInfo() {
        free(printerName);
        free(inverseSearchCmdLine);
        free(lang);
        free(destName);
        free(stressTestPath);
        free(stressTestRanges);
        free(stressTestFilter);
        free(pluginURL);
    }

    void ParseCommandLine(TCHAR *cmdLine);
};

#endif
