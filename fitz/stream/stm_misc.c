/*
 * Miscellaneous I/O functions
 */

#include "fitz-base.h"
#include "fitz-stream.h"

int fz_tell(fz_stream *stm)
{
	if (stm->mode == FZ_SREAD)
		return fz_rtell(stm);
	return fz_wtell(stm);
}

int fz_seek(fz_stream *stm, int offset, int whence)
{
	if (stm->mode == FZ_SREAD)
		return fz_rseek(stm, offset, whence);
	return fz_wseek(stm, offset, whence);
}

/*
 * Read a line terminated by LF or CR or CRLF.
 */

int fz_readline(fz_stream *stm, char *mem, int n)
{
	char *s = mem;
	int c = EOF;
	while (n > 1)
	{
		c = fz_readbyte(stm);
		if (c == EOF)
			break;
		if (c == '\r') {
			c = fz_peekbyte(stm);
			if (c == '\n')
				c = fz_readbyte(stm);
			break;
		}
		if (c == '\n')
			break;
		*s++ = c;
		n--;
	}
	if (n)
		*s = '\0';
	return s - mem;
}

struct fz_linkedbuf_s
{
	int	len;
	struct fz_linkedbuf_s *	next;
};

typedef struct fz_linkedbuf_s fz_linkedbuf;

fz_error *fz_newlinkedbuf(fz_linkedbuf **bufp, int len, unsigned char **data)
{
	fz_linkedbuf *buf;

	buf = *bufp = fz_malloc(sizeof(fz_linkedbuf) + len);
	if (!buf) return fz_outofmem;
	buf->next = nil;
	buf->len = len;
	*data = (unsigned char*)buf + sizeof(fz_linkedbuf);
	return nil;
}

fz_error *fz_growlinkedbuf(fz_linkedbuf *buf, int len, unsigned char **data)
{
	fz_linkedbuf *newbuf;
	fz_error *error;

	error = fz_newlinkedbuf(&newbuf, len, data);
	if (error) return error;
	while (buf->next)
		buf = buf->next;
	buf->next = newbuf;
	return nil;
}

void fz_droplinkedbuf(fz_linkedbuf *buf)
{
	fz_linkedbuf *next;
	while (buf) {
		next = buf->next;
		fz_free(buf);
		buf = next;
	}
}

int fz_linkedbuflen(fz_linkedbuf *buf)
{
	int	len = 0;
	while (buf) {
		len += buf->len;
		buf = buf->next;
	}
	return len;
}

fz_error *fz_linearizelinkedbuf(fz_linkedbuf *buf, int len, unsigned char **datap)
{
	unsigned char *data, *bufdata, *bufdataend;
	int tocopy;
	int buflen = fz_linkedbuflen(buf);
	assert(len <= buflen);
	data = *datap = fz_malloc(len);
	if (!data) return fz_outofmem;

	while (len > 0)
	{
		bufdata = (unsigned char*)buf + sizeof(fz_linkedbuf);
		tocopy = MIN(len, buf->len);
		bufdataend = bufdata + tocopy;
		while (bufdata < bufdataend)
			*data++ = *bufdata++;
		buf = buf->next;
		len -= tocopy;
	}
	return nil;
}

/*
 * Utility function to consume all the contents of an input stream into
 * a freshly allocated buffer; realloced and trimmed to size.
 */

enum { CHUNKSIZE = 1024 * 4 };

int fz_readall(fz_buffer **bufp, fz_stream *stm)
{
	fz_buffer *real;
	fz_error *error;
	fz_linkedbuf *buf;
	unsigned char *data;
	int totallen;
	int n;

	*bufp = nil;

	totallen = 0;
	error = fz_newlinkedbuf(&buf, CHUNKSIZE, &data);
	if (error)
		return -1;

	while (1)
	{
		n = fz_read(stm, data, CHUNKSIZE);
		if (n < 0)
		{
			fz_free(buf);
			return -1;
		}

		totallen += n;
		if (n == CHUNKSIZE) 
		{
			error = fz_growlinkedbuf(buf, CHUNKSIZE, &data);
			if (error) { fz_droplinkedbuf(buf); return -1; }
			continue;
		}

		error = fz_linearizelinkedbuf(buf, totallen, &data);
		fz_droplinkedbuf(buf);
		if (error) return -1;

		real = *bufp = fz_malloc(sizeof(fz_buffer));
		if (!real)
		{
			fz_droplinkedbuf(buf);
			return -1;
		}

		real->refs = 1;
		real->ownsdata = 1;
		real->bp = data;
		real->rp = data;
		real->wp = data + totallen;
		real->ep = data + totallen;
		real->eof = 1;
		return totallen;
	}
}

