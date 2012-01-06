#include <assert.h>
#include "fitz.h"

fz_path *
fz_new_path(fz_context *ctx)
{
	fz_path *path;

	path = fz_malloc_struct(ctx, fz_path);
	path->len = 0;
	path->cap = 0;
	path->items = NULL;

	return path;
}

fz_path *
fz_clone_path(fz_context *ctx, fz_path *old)
{
	fz_path *path;

	assert(old);
	path = fz_malloc_struct(ctx, fz_path);
	fz_try(ctx)
	{
		path->len = old->len;
		path->cap = old->len;
		path->items = fz_malloc_array(ctx, path->cap, sizeof(fz_path_item));
		memcpy(path->items, old->items, sizeof(fz_path_item) * path->len);
	}
	fz_catch(ctx)
	{
		fz_free(ctx, path);
		fz_rethrow(ctx);
	}

	return path;
}

void
fz_free_path(fz_context *ctx, fz_path *path)
{
	if (path == NULL)
		return;
	fz_free(ctx, path->items);
	fz_free(ctx, path);
}

static void
grow_path(fz_context *ctx, fz_path *path, int n)
{
	int newcap = path->cap;
	if (path->len + n <= path->cap)
		return;
	while (path->len + n > newcap)
		newcap = newcap + 36;
	path->items = fz_resize_array(ctx, path->items, newcap, sizeof(fz_path_item));
	path->cap = newcap;
}

void
fz_moveto(fz_context *ctx, fz_path *path, float x, float y)
{
	grow_path(ctx, path, 3);
	path->items[path->len++].k = FZ_MOVETO;
	path->items[path->len++].v = x;
	path->items[path->len++].v = y;
}

void
fz_lineto(fz_context *ctx, fz_path *path, float x, float y)
{
	if (path->len == 0)
	{
		fz_warn(ctx, "lineto with no current point");
		return;
	}
	grow_path(ctx, path, 3);
	path->items[path->len++].k = FZ_LINETO;
	path->items[path->len++].v = x;
	path->items[path->len++].v = y;
}

void
fz_curveto(fz_context *ctx, fz_path *path,
	float x1, float y1,
	float x2, float y2,
	float x3, float y3)
{
	if (path->len == 0)
	{
		fz_warn(ctx, "curveto with no current point");
		return;
	}
	grow_path(ctx, path, 7);
	path->items[path->len++].k = FZ_CURVETO;
	path->items[path->len++].v = x1;
	path->items[path->len++].v = y1;
	path->items[path->len++].v = x2;
	path->items[path->len++].v = y2;
	path->items[path->len++].v = x3;
	path->items[path->len++].v = y3;
}

void
fz_curvetov(fz_context *ctx, fz_path *path, float x2, float y2, float x3, float y3)
{
	float x1, y1;
	if (path->len == 0)
	{
		fz_warn(ctx, "curvetov with no current point");
		return;
	}
	x1 = path->items[path->len-2].v;
	y1 = path->items[path->len-1].v;
	fz_curveto(ctx, path, x1, y1, x2, y2, x3, y3);
}

void
fz_curvetoy(fz_context *ctx, fz_path *path, float x1, float y1, float x3, float y3)
{
	fz_curveto(ctx, path, x1, y1, x3, y3, x3, y3);
}

void
fz_closepath(fz_context *ctx, fz_path *path)
{
	if (path->len == 0)
	{
		fz_warn(ctx, "closepath with no current point");
		return;
	}
	grow_path(ctx, path, 1);
	path->items[path->len++].k = FZ_CLOSE_PATH;
}

static inline fz_rect bound_expand(fz_rect r, fz_point p)
{
	if (p.x < r.x0) r.x0 = p.x;
	if (p.y < r.y0) r.y0 = p.y;
	if (p.x > r.x1) r.x1 = p.x;
	if (p.y > r.y1) r.y1 = p.y;
	return r;
}

fz_rect
fz_bound_path(fz_path *path, fz_stroke_state *stroke, fz_matrix ctm)
{
	fz_point p;
	fz_rect r;
	int i = 0;

	/* If the path is empty, return the empty rectangle here - don't wait
	 * for it to be expanded in the stroked case below. */
	if (path->len == 0)
		return fz_empty_rect;

	/* SumatraPDF: ignore spurious/invisible MoveTo instructions */
	if (!stroke || !stroke->dash_len && !stroke->start_cap)
	{
		while (i + 3 < path->len && path->items[i].k == FZ_MOVETO && path->items[i + 3].k == FZ_MOVETO)
			i += 3;
		if (i + 3 == path->len && path->items[i].k == FZ_MOVETO)
			return fz_empty_rect;
	}

	p.x = path->items[i + 1].v;
	p.y = path->items[i + 2].v;
	p = fz_transform_point(ctm, p);
	r.x0 = r.x1 = p.x;
	r.y0 = r.y1 = p.y;

	while (i < path->len)
	{
		switch (path->items[i++].k)
		{
		case FZ_CURVETO:
			p.x = path->items[i++].v;
			p.y = path->items[i++].v;
			r = bound_expand(r, fz_transform_point(ctm, p));
			p.x = path->items[i++].v;
			p.y = path->items[i++].v;
			r = bound_expand(r, fz_transform_point(ctm, p));
			p.x = path->items[i++].v;
			p.y = path->items[i++].v;
			r = bound_expand(r, fz_transform_point(ctm, p));
			break;
		case FZ_MOVETO:
		case FZ_LINETO:
			p.x = path->items[i++].v;
			p.y = path->items[i++].v;
			r = bound_expand(r, fz_transform_point(ctm, p));
			break;
		case FZ_CLOSE_PATH:
			break;
		}
	}

	if (stroke)
	{
		float miterlength = stroke->miterlimit;
		float linewidth = stroke->linewidth;
		float expand = MAX(miterlength, linewidth) * 0.5f;
		r.x0 -= expand;
		r.y0 -= expand;
		r.x1 += expand;
		r.y1 += expand;
	}

	return r;
}

void
fz_transform_path(fz_path *path, fz_matrix ctm)
{
	fz_point p;
	int k, i = 0;

	while (i < path->len)
	{
		switch (path->items[i++].k)
		{
		case FZ_CURVETO:
			for (k = 0; k < 3; k++)
			{
				p.x = path->items[i].v;
				p.y = path->items[i+1].v;
				p = fz_transform_point(ctm, p);
				path->items[i].v = p.x;
				path->items[i+1].v = p.y;
				i += 2;
			}
			break;
		case FZ_MOVETO:
		case FZ_LINETO:
			p.x = path->items[i].v;
			p.y = path->items[i+1].v;
			p = fz_transform_point(ctm, p);
			path->items[i].v = p.x;
			path->items[i+1].v = p.y;
			i += 2;
			break;
		case FZ_CLOSE_PATH:
			break;
		}
	}
}

void
fz_debug_path(fz_path *path, int indent)
{
	float x, y;
	int i = 0;
	int n;
	while (i < path->len)
	{
		for (n = 0; n < indent; n++)
			putchar(' ');
		switch (path->items[i++].k)
		{
		case FZ_MOVETO:
			x = path->items[i++].v;
			y = path->items[i++].v;
			printf("%g %g m\n", x, y);
			break;
		case FZ_LINETO:
			x = path->items[i++].v;
			y = path->items[i++].v;
			printf("%g %g l\n", x, y);
			break;
		case FZ_CURVETO:
			x = path->items[i++].v;
			y = path->items[i++].v;
			printf("%g %g ", x, y);
			x = path->items[i++].v;
			y = path->items[i++].v;
			printf("%g %g ", x, y);
			x = path->items[i++].v;
			y = path->items[i++].v;
			printf("%g %g c\n", x, y);
			break;
		case FZ_CLOSE_PATH:
			printf("h\n");
			break;
		}
	}
}
