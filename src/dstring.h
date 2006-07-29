/****************************************************************************
 * Dynamic strings
 ****************************************************************************/

/*
 * tcl.h --
 *
 *      This header file describes the externally-visible facilities
 *      of the Tcl interpreter.
 *
 * Copyright (c) 1987-1994 The Regents of the University of California.
 * Copyright (c) 1994-1996 Sun Microsystems, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * SCCS: @(#) tcl.h 1.283 96/10/02 17:17:39
 */
#ifndef DSTRING_H
#define DSTRING_H

#ifdef __cplusplus
extern "C"
{
#endif

#define kDstringStaticSize 200

typedef struct DString_t {
    char *pString;               /* Points to beginning of string:  either
                                 * staticSpace below or a malloc'ed array. */
    int length;                 /* Number of non-NULL characters in the
                                 * string. */
    int spaceAvl;               /* Total number of bytes available for the
                                 * string and its terminating NULL char. */
    char staticSpace[kDstringStaticSize];
                                /* Space to use in common case where string
                                 * is small. */
} DString_t;

#define DStringLength(dsPtr) ((dsPtr)->length)
#define DStringValue(dsPtr) ((dsPtr)->pString)
#define DStringTrunc DStringSetLength

char*
DStringAppend(DString_t* dsPtr,
                 const char* string,
                 int         length);

void
DStringFree(DString_t* dsPtr);

void
DStringInit(DString_t* dsPtr);

void
DStringSetLength(DString_t* dsPtr,
                    int         length);

void
DStringSprintf(DString_t* pDs,
                   const char* pFormat,
                   ...);
char*
DStringAppendLowerCase(DString_t*   pDs,
                         const char*    pIn,
                         int            length);

#ifdef __cplusplus
}
#endif

#endif
