#include <fitz.h>
#include <mupdf.h>

static fz_error *
pdf_loadcompsiteshadefunc(fz_shade *shade, pdf_xref *xref, fz_obj *shading, fz_obj *funcdict, float t0, float t1)
{
	fz_error *error;
	pdf_function *func;
	int i;

	error = pdf_loadfunction(&func, xref, funcdict);
	if (error)
		return fz_rethrow(error, "unable to evaluate shading function");

	for (i = 0; i < 256; ++i)
	{
		float t = t0 + (i / 256.0) * (t1 - t0);

		error = pdf_evalfunction(func, &t, 1, shade->function[i], shade->cs->n);
		if (error)
			return fz_rethrow(error, "unable to evaluate shading function at %g", t);
	}

	pdf_dropfunction(func);

	return fz_okay;
}

static fz_error *
pdf_loadcomponentshadefunc(fz_shade *shade, pdf_xref *xref, fz_obj *shading, fz_obj *funcs, float t0, float t1)
{
	fz_error *error;
	pdf_function **func = nil;
	int i;

	if (fz_arraylen(funcs) != shade->cs->n)
	{
		error = fz_throw("incorrect number of shading functions");
		goto cleanup;
	}

	func = fz_malloc(fz_arraylen(funcs) * sizeof(pdf_function *));
	if (!func)
	{
		error = fz_throw("outofmem: shading function"); 
		goto cleanup;
	}
	memset(func, 0x00, fz_arraylen(funcs) * sizeof(pdf_function *));

	for (i = 0; i < fz_arraylen(funcs); i++)
	{
		fz_obj *obj = nil;

		obj = fz_arrayget(funcs, i);
		if (!obj)
		{
			error = fz_throw("shading function component not found");
			goto cleanup;
		}

		error = pdf_loadfunction(&func[i], xref, obj);
		if (error)
		{
			error = fz_rethrow(error, "unable to evaluate shading function");
			goto cleanup;
		}
	}

	for (i = 0; i < 256; ++i)
	{
		float t = t0 + (i / 256.0) * (t1 - t0);
		int k;

		for (k = 0; k < fz_arraylen(funcs); k++)
		{
			error = pdf_evalfunction(func[k], &t, 1, &shade->function[i][k], 1);
			if (error)
			{
				error = fz_rethrow(error, "unable to evaluate shading function at %g", t);
				goto cleanup;
			}
		}
	}

	for (i = 0; i < fz_arraylen(funcs); i++)
		pdf_dropfunction(func[i]);
	fz_free(func);

	return fz_okay;

cleanup:
	if (func)
	{
		for (i = 0; i < fz_arraylen(funcs); i++)
			if (func[i])
				pdf_dropfunction(func[i]);
		fz_free(func);
	}

	return error;
}

fz_error *
pdf_loadshadefunction(fz_shade *shade, pdf_xref *xref, fz_obj *shading, float t0, float t1)
{
	fz_error *error;
	fz_obj *obj;

	obj = fz_dictgets(shading, "Function");
	error = pdf_resolve(&obj, xref);
	if (error)
		return fz_rethrow(error, "couldn't resolve shading function");
	if (!obj)
		return fz_throw("shading function not found");
	if (!fz_isdict(obj) && !fz_isarray(obj))
	{
		fz_dropobj(obj);
		return fz_throw("invalid shading function");
	}

	shade->usefunction = 1;

	if (fz_isdict(obj))
		error = pdf_loadcompsiteshadefunc(shade, xref, shading, obj, t0, t1);
	else if (fz_isarray(obj))
		error = pdf_loadcomponentshadefunc(shade, xref, shading, obj, t0, t1);

	fz_dropobj(obj);

	if (error)
		return fz_rethrow(error, "couldn't load shading function");

	return fz_okay;
}

void
pdf_setmeshvalue(float *mesh, int i, float x, float y, float t)
{
	mesh[i*3+0] = x;
	mesh[i*3+1] = y;
	mesh[i*3+2] = t;
}

static fz_error *
loadshadedict(fz_shade **shadep, pdf_xref *xref, fz_obj *dict, fz_obj *ref, fz_matrix matrix)
{
	fz_error *error;
	fz_shade *shade;
	fz_obj *obj;
	int type;
	int i;

	pdf_logshade("load shade dict %d %d {\n", fz_tonum(ref), fz_togen(ref));

	shade = fz_malloc(sizeof(fz_shade));
	if (!shade)
		return fz_outofmem;

	shade->refs = 1;
	shade->usebackground = 0;
	shade->usefunction = 0;
	shade->matrix = matrix;
	shade->bbox = fz_infiniterect;

	shade->meshlen = 0;
	shade->meshcap = 0;
	shade->mesh = nil;

	obj = fz_dictgets(dict, "ShadingType");
	type = fz_toint(obj);
	pdf_logshade("type %d\n", type);

	/* TODO: flatten indexed... */
	obj = fz_dictgets(dict, "ColorSpace");
	if (obj)
	{
		shade->cs = pdf_finditem(xref->store, PDF_KCOLORSPACE, obj);
		if (shade->cs)
			fz_keepcolorspace(shade->cs);
		else
		{
			error = pdf_resolve(&obj, xref);
			if (error)
				return fz_rethrow(error, "couldn't resolve colorspace");
			error = pdf_loadcolorspace(&shade->cs, xref, obj);
			if (error)
				return fz_rethrow(error, "could not load colorspace");
			fz_dropobj(obj);
		}
	}
	pdf_logshade("colorspace %s\n", shade->cs->name);

	obj = fz_dictgets(dict, "Background");
	if (obj)
	{
		pdf_logshade("background\n");
		shade->usebackground = 1;
		for (i = 0; i < shade->cs->n; i++)
			shade->background[i] = fz_toreal(fz_arrayget(obj, i));
	}

	obj = fz_dictgets(dict, "BBox");
	if (fz_isarray(obj))
	{
		shade->bbox = pdf_torect(obj);
		pdf_logshade("bbox [%g %g %g %g]\n",
				shade->bbox.x0, shade->bbox.y0,
				shade->bbox.x1, shade->bbox.y1);
	}

	switch(type)
	{
	case 1:
		error = pdf_loadtype1shade(shade, xref, dict, ref);
		if (error) goto cleanup;
		break;
	case 2:
		error = pdf_loadtype2shade(shade, xref, dict, ref);
		if (error) goto cleanup;
		break;
	case 3:
		error = pdf_loadtype3shade(shade, xref, dict, ref);
		if (error) goto cleanup;
		break;
	case 4:
		error = pdf_loadtype4shade(shade, xref, dict, ref);
		if (error) goto cleanup;
		break;
	case 5:
		error = pdf_loadtype5shade(shade, xref, dict, ref);
		if (error) goto cleanup;
		break;
	case 6:
		error = pdf_loadtype6shade(shade, xref, dict, ref);
		if (error) goto cleanup;
		break;
	case 7:
		error = pdf_loadtype7shade(shade, xref, dict, ref);
		if (error) goto cleanup;
		break;
	default:
		fz_warn("syntaxerror: unknown shading type: %d", type);
		break;
	};

	pdf_logshade("}\n");

	*shadep = shade;
	return fz_okay;

cleanup:
	fz_dropshade(shade);
	return fz_rethrow(error, "could not load shading");
}

fz_error *
pdf_loadshade(fz_shade **shadep, pdf_xref *xref, fz_obj *dict, fz_obj *ref)
{
	fz_error *error;
	fz_matrix mat;
	fz_obj *obj;
	fz_obj *shd;

	if ((*shadep = pdf_finditem(xref->store, PDF_KSHADE, ref)))
		return fz_okay;

	/*
	 * Type 2 pattern dictionary
	 */
	if (fz_dictgets(dict, "PatternType"))
	{
		pdf_logshade("load shade pattern %d %d {\n", fz_tonum(ref), fz_togen(ref));

		obj = fz_dictgets(dict, "Matrix");
		if (obj)
		{
			mat = pdf_tomatrix(obj);
			pdf_logshade("matrix [%g %g %g %g %g %g]\n",
					mat.a, mat.b, mat.c, mat.d, mat.e, mat.f);
		}
		else
		{
			mat = fz_identity();
		}

		obj = fz_dictgets(dict, "ExtGState");
		if (obj)
		{
			pdf_logshade("extgstate ...\n");
		}

		obj = fz_dictgets(dict, "Shading");
		if (!obj)
			return fz_throw("syntaxerror: missing shading dictionary");

		shd = obj;
		error = pdf_resolve(&shd, xref);
		if (error)
			return fz_rethrow(error, "could not resolve shading dictionary");
		error = loadshadedict(shadep, xref, shd, obj, mat);
		fz_dropobj(shd);
		if (error)
			return fz_rethrow(error, "could not load shading dictionary");

		pdf_logshade("}\n");
	}

	/*
	 * Naked shading dictionary
	 */
	else
	{
		error = loadshadedict(shadep, xref, dict, ref, fz_identity());
		if (error)
			return fz_rethrow(error, "could not load shading dictionary");
	}

	error = pdf_storeitem(xref->store, PDF_KSHADE, ref, *shadep);
	if (error)
	{
		fz_dropshade(*shadep);
		return fz_rethrow(error, "could not put shading dictionary in store");
	}

	return fz_okay;
}

