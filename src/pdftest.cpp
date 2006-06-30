#include <config.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>

#ifdef WIN32
#include <windows.h>
#endif

#if 0
#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE 0
#endif
#endif

#include "ErrorCodes.h"
#include "GooString.h"
#include "GooList.h"
#include "GlobalParams.h"
#include "SplashBitmap.h"
#include "PDFDoc.h"

/* Should we record timings? True if -timings command-line argument was given. */
static int g_fShowTimings = FALSE;

/* If true, we use render each page at resolution 'g_resolutionX'/'g_resolutionY'.
   If false, we render each page at its native resolution.
   True if -resolution NxM command-line argument was given. */
static int g_fForceResolution = FALSE;
static int g_resolutionX = 0;
static int g_resolutionY = 0;
/* If NULL, we output the log info to stdout. If not NULL, should be a name
   of the file to which we output log info. 
   Controled by -out command-line argument. */
static char *   g_outFile = NULL;
/* If NULL, we output the error log info to stderr. If not NULL, should be
   a name of the file to which we output error log info.
   Controlled by -err command-line argument */
static char *   g_errFile = NULL;

/* If True and a directory is given as a command-line argument, we'll process
   pdf files in sub-directories as well.
   Controlled by -recursive command-line argument */
static int g_fRecursive = FALSE;

void CDECL error(int pos, char *msg, ...) {
    va_list args;
    char        buf[4096], *p = buf;

    // NB: this can be called before the globalParams object is created
    if (globalParams && globalParams->getErrQuiet()) {
        return;
    }

    if (pos >= 0) {
        p += _snprintf(p, sizeof(buf)-1, "Error (%d): ", pos);
        *p   = '\0';
        OutputDebugString(p);
    } else {
        OutputDebugString("Error: ");
    }

    p = buf;
    va_start(args, msg);
    p += _vsnprintf(p, sizeof(buf) - 1, msg, args);
    while ( p > buf  &&  isspace(p[-1]) )
            *--p = '\0';
    *p++ = '\r';
    *p++ = '\n';
    *p   = '\0';
    OutputDebugString(buf);
    va_end(args);
}

static void usage(void)
{
    printf(": pdftest [-timings] [-resolution 100x200] [-recursive] [-out out.txt] [-errout errout.txt] pdf-files-to-process\n");
}

int main(int argc, char **argv)
{
    usage();

    globalParams = new GlobalParams("");
    if (!globalParams)
        return 1;

    return 0;
}
