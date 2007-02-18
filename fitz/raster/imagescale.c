#include "fitz-base.h"
#include "fitz-world.h"
#include "fitz-draw.h"

typedef unsigned char byte;

static FORCEINLINE void srown(byte * restrict src, byte * restrict dst, unsigned w, unsigned denom, unsigned n)
{
	unsigned x, left, k;
	unsigned sum[FZ_MAXCOLORS];

	left = 0;
	for (k = 0; k < n; k++)
		sum[k] = 0;

	for (x = 0; x < w; x++)
	{
		for (k = 0; k < n; k++)
			sum[k] += src[x * n + k];
		if (++left == denom)
		{
			left = 0;
			for (k = 0; k < n; k++)
			{
				dst[k] = sum[k] / denom;
				sum[k] = 0;
			}
			dst += n;
		}
	}

	/* left overs */
	if (left)
		for (k = 0; k < n; k++)
			dst[k] = sum[k] / left;
}

static inline void srownp2(byte * restrict src, byte * restrict dst, unsigned w, unsigned log2denom, unsigned n)
{
	unsigned x, left, k;
	unsigned sum[FZ_MAXCOLORS];

	left = 0;
	for (k = 0; k < n; k++)
		sum[k] = 0;

	for (x = 0; x < w; x++)
	{
		for (k = 0; k < n; k++)
			sum[k] += src[x * n + k];
		if (++left == (1<<log2denom))
		{
			left = 0;
			for (k = 0; k < n; k++)
			{
				dst[k] = sum[k] >> log2denom;
				sum[k] = 0;
			}
			dst += n;
		}
	}

	/* left overs */
	if (left)
		for (k = 0; k < n; k++)
			dst[k] = sum[k] / left;
}

static void srow1(byte *src, byte *dst, int w, int denom)
{
	srown(src, dst, w, denom, 1);
}

static void srow2(byte *src, byte *dst, int w, int denom)
{
#if 0
    srown(src, dst, w, denom, 2);
#else
    unsigned left;
    unsigned sum[2];
    byte *srcend;

    left = 0;
    sum[0] = 0;
    sum[1] = 0;

    srcend = src + (2 * w);
    while (src < srcend)
    {
        sum[0] += *src++;
        sum[1] += *src++;
        if (++left == denom)
        {
            left = 0;
            *dst++ = sum[0] / denom;
            *dst++ = sum[1] / denom;
            sum[0] = 0;
            sum[1] = 0;
        }
    }

    /* left overs */
    if (left)
    {
        dst[0] = sum[0] / left;
        dst[1] = sum[1] / left;
    }
#endif
}

static void srow4(byte *src, byte *dst, int w, int denom)
{
	srown(src, dst, w, denom, 4);
}

static void srow5(byte *src, byte *dst, int w, int denom)
{
	srown(src, dst, w, denom, 5);
}

static void srow5p2(byte * restrict src, byte * restrict dst, int w, int log2denom)
{
    srownp2(src, dst, w, log2denom, 5);        
}

static FORCEINLINE void scoln(byte * restrict src, byte * restrict dst, int w, int denom, int n)
{
	int x, y, k;
	byte *s;
	int sum[FZ_MAXCOLORS];

	for (x = 0; x < w; x++)
	{
		s = src + (x * n);
		for (k = 0; k < n; k++)
			sum[k] = 0;
		for (y = 0; y < denom; y++)
			for (k = 0; k < n; k++)
				sum[k] += s[y * w * n + k];
		for (k = 0; k < n; k++)
			dst[k] = sum[k] / denom;
		dst += n;
	}
}

static inline void scolnp2(byte *src, byte *dst, int w, int log2denom, int n)
{
	int x, y, k;
	byte *s;
	int sum[FZ_MAXCOLORS];

	for (x = 0; x < w; x++)
	{
		s = src + (x * n);
		for (k = 0; k < n; k++)
			sum[k] = 0;
		for (y = 0; y < (1<<log2denom); y++)
			for (k = 0; k < n; k++)
				sum[k] += s[y * w * n + k];
		for (k = 0; k < n; k++)
			dst[k] = sum[k] >> log2denom;
		dst += n;
	}
}

static void scol1(byte *src, byte *dst, int w, int denom)
{
	scoln(src, dst, w, denom, 1);
}

static void scol2(byte *src, byte *dst, int w, int denom)
{
#if 0
    scoln(src, dst, w, denom, 2);
#else
    int y;
    byte *srcend;
    int sum[2];

    srcend = src + w * 2;
    while (src < srcend)
    {
        sum[0] = 0;
        sum[1] = 0;
        for (y = 0; y < denom; y++) {
            sum[0] += src[y * w * 2];
            sum[1] += src[y * w * 2 + 1];
        }
        *dst++ = sum[0] / denom;
        *dst++ = sum[1] / denom;
        src += 2;
    }
#endif
}

static void scol4(byte *src, byte *dst, int w, int denom)
{
	scoln(src, dst, w, denom, 4);
}

static void scol5(byte *src, byte *dst, int w, int denom)
{
	scoln(src, dst, w, denom, 5);
}

static void scol5p2(byte *src, byte *dst, int w, int denom)
{
	scolnp2(src, dst, w, denom, 5);
}

void (*fz_srown)(byte *src, byte *dst, int w, int denom, int n) = srown;
void (*fz_srow1)(byte *src, byte *dst, int w, int denom) = srow1;
void (*fz_srow2)(byte *src, byte *dst, int w, int denom) = srow2;
void (*fz_srow4)(byte *src, byte *dst, int w, int denom) = srow4;
void (*fz_srow5)(byte *src, byte *dst, int w, int denom) = srow5;

void (*fz_scoln)(byte *src, byte *dst, int w, int denom, int n) = scoln;
void (*fz_scol1)(byte *src, byte *dst, int w, int denom) = scol1;
void (*fz_scol2)(byte *src, byte *dst, int w, int denom) = scol2;
void (*fz_scol4)(byte *src, byte *dst, int w, int denom) = scol4;
void (*fz_scol5)(byte *src, byte *dst, int w, int denom) = scol5;

fz_error *
fz_scalepixmap(fz_pixmap **dstp, fz_pixmap *src, int xdenom, int ydenom)
{
	fz_error *error;
	fz_pixmap *dst;
	unsigned char *buf;
	int y, iy, oy;
	int ow, oh, n;
        int ydenom2 = ydenom;

	void (*srowx)(byte *src, byte *dst, int w, int denom) = nil;
	void (*scolx)(byte *src, byte *dst, int w, int denom) = nil;

	ow = (src->w + xdenom - 1) / xdenom;
	oh = (src->h + ydenom - 1) / ydenom;
	n = src->n;

	buf = fz_malloc(ow * n * ydenom);
	if (!buf)
		return fz_outofmem;

	error = fz_newpixmap(&dst, 0, 0, ow, oh, src->n);
	if (error)
	{
		fz_free(buf);
		return error;
	}

	switch (n)
	{
	case 1: srowx = fz_srow1; scolx = fz_scol1; break;
	case 2: srowx = fz_srow2; scolx = fz_scol2; break;
	case 4: srowx = fz_srow4; scolx = fz_scol4; break;
	case 5: srowx = fz_srow5; scolx = fz_scol5;
                if (!(xdenom & (xdenom - 1)))
                {
                        unsigned v = xdenom;
                        xdenom = 0;
                        while ((v >>= 1)) xdenom++;
                        srowx = srow5p2;
                }
                if (!(ydenom & (ydenom - 1)))
                {
                        unsigned v = ydenom2;
                        ydenom2 = 0;
                        while ((v >>= 1)) ydenom2++;
                        scolx = scol5p2;
                }
                
                break;
	}

	if (srowx && scolx)
	{
		for (y = 0, oy = 0; y < (src->h / ydenom) * ydenom; y += ydenom, oy++)
		{
			for (iy = 0; iy < ydenom; iy++)
				srowx(src->samples + (y + iy) * src->w * n,
						 buf + iy * ow * n,
						 src->w, xdenom);
			scolx(buf, dst->samples + oy * dst->w * n, dst->w, ydenom2);
		}

		ydenom = src->h - y;
		if (ydenom)
		{
			for (iy = 0; iy < ydenom; iy++)
				srowx(src->samples + (y + iy) * src->w * n,
						 buf + iy * ow * n,
						 src->w, xdenom);
			scolx(buf, dst->samples + oy * dst->w * n, dst->w, ydenom2);
		}
	}

	else
	{
		for (y = 0, oy = 0; y < (src->h / ydenom) * ydenom; y += ydenom, oy++)
		{
			for (iy = 0; iy < ydenom; iy++)
				fz_srown(src->samples + (y + iy) * src->w * n,
						 buf + iy * ow * n,
						 src->w, xdenom, n);
			fz_scoln(buf, dst->samples + oy * dst->w * n, dst->w, ydenom2, n);
		}

		ydenom = src->h - y;
		if (ydenom)
		{
			for (iy = 0; iy < ydenom; iy++)
				fz_srown(src->samples + (y + iy) * src->w * n,
						 buf + iy * ow * n,
						 src->w, xdenom, n);
			fz_scoln(buf, dst->samples + oy * dst->w * n, dst->w, ydenom2, n);
		}
	}

	fz_free(buf);
	*dstp = dst;
	return nil;
}

