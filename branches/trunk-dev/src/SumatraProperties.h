/* Copyright 2006-2010 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#ifndef SUMATRA_PDF_PROPERTIES_H_
#define SUMATRA_PDF_PROPERTIES_H_

#define PROPERTIES_CLASS_NAME   _T("SUMATRA_PDF_PROPERTIES")

enum Magnitudes { KB = 1024, MB = 1024 * KB, GB = 1024 * MB };

void FreePdfProperties(WindowInfo *win);
void OnMenuProperties(WindowInfo *win);
void CopyPropertiesToClipboard(WindowInfo *win);
LRESULT CALLBACK WndProcProperties(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

#endif
