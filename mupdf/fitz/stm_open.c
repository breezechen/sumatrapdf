/*
 * Creation and destruction.
 */

#include "fitz.h"

static fz_stream *
newstm(int kind)
{
	fz_stream *stm;

	stm = fz_malloc(sizeof(fz_stream));

	stm->refs = 1;
	stm->kind = kind;
	stm->dead = 0;
	stm->error = fz_okay;
	stm->buffer = nil;

	stm->chain = nil;
	stm->filter = nil;
	stm->file = -1;

	return stm;
}

fz_stream *
fz_keepstream(fz_stream *stm)
{
	stm->refs ++;
	return stm;
}

void
fz_dropstream(fz_stream *stm)
{
	stm->refs --;
	if (stm->refs == 0)
	{
		if (stm->error)
		{
			fz_catch(stm->error, "dropped unhandled ioerror");
			stm->error = fz_okay;
		}

		switch (stm->kind)
		{
		case FZ_SFILE:
			close(stm->file);
			break;
		case FZ_SFILTER:
			fz_dropfilter(stm->filter);
			fz_dropstream(stm->chain);
			break;
		case FZ_SBUFFER:
			break;
		}

		fz_dropbuffer(stm->buffer);
		fz_free(stm);
	}
}

#ifdef WIN32
#include <windows.h>

static int open_utf8(char *utf8path, int oflag, int pmode)
{
	int pathlen = strlen(utf8path) + 1;
	wchar_t *wpath = fz_malloc(pathlen * 2);
	int result;

	/* Convert UTF-8 to UTF-16 */
	result = MultiByteToWideChar(CP_UTF8, 0, utf8path, -1, wpath, pathlen * 2);
	/* (and if the path isn't UTF-8 after all, fall back to the ANSI code page) */
	if (result == ERROR_NO_UNICODE_TRANSLATION)
		MultiByteToWideChar(CP_ACP, 0, utf8path, -1, wpath, pathlen * 2);
#ifdef _UNICODE
	result = _wopen(wpath, oflag, pmode);
#else
	{
		/* Convert UTF-16 to the ANSI code page */
		char *path = fz_malloc(pathlen);
		WideCharToMultiByte(CP_ACP, 0, wpath, -1, path, pathlen, NULL, NULL);
		result = open(path, oflag, pmode);
		fz_free(path);
	}
#endif
	fz_free(wpath);

	return result;
}

/* redefine open(...) below, as the path is supposed to be UTF-8 */
#define open(path, oflag, pmode) open_utf8(path, oflag, pmode);
#endif

fz_error fz_openrfile(fz_stream **stmp, char *path)
{
	fz_stream *stm;

	stm = newstm(FZ_SFILE);

	stm->buffer = fz_newbuffer(FZ_BUFSIZE);

	stm->file = open(path, O_BINARY | O_RDONLY, 0666);
	if (stm->file < 0)
	{
		fz_dropbuffer(stm->buffer);
		fz_free(stm);
		return fz_throw("syserr: open '%s': %s", path, strerror(errno));
	}

	*stmp = stm;
	return fz_okay;
}

fz_stream * fz_openrfilter(fz_filter *flt, fz_stream *src)
{
	fz_stream *stm;

	stm = newstm(FZ_SFILTER);
	stm->buffer = fz_newbuffer(FZ_BUFSIZE);
	stm->chain = fz_keepstream(src);
	stm->filter = fz_keepfilter(flt);

	return stm;
}

fz_stream * fz_openrbuffer(fz_buffer *buf)
{
	fz_stream *stm;

	stm = newstm(FZ_SBUFFER);
	stm->buffer = fz_keepbuffer(buf);
	stm->buffer->eof = 1;

	return stm;
}

fz_stream * fz_openrmemory(unsigned char *mem, int len)
{
	fz_buffer *buf;
	fz_stream *stm;

	buf = fz_newbufferwithmemory(mem, len);
	stm = fz_openrbuffer(buf);
	fz_dropbuffer(buf);

	return stm;
}
