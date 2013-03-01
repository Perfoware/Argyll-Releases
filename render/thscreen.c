
/*
 * render2d
 *
 * Threshold screen pixel processing object.
 * (Simplified from DPS code)
 *
 * Author:  Graeme W. Gill
 * Date:    8/9/2005
 * Version: 1.00
 *
 * Copyright 2005, 2012 Graeme W. Gill
 * All rights reserved.
 * This material is licenced under the GNU AFFERO GENERAL PUBLIC LICENSE Version 3 :-
 * see the License.txt file for licencing details.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <fcntl.h>
#include <string.h>
#include <math.h>
#include "aconfig.h"
#include "numlib.h"
//#include "icc.h"
#include "sort.h"
//#include "xcolorants.h"
#include "thscreen.h"

/* Configuration: */
#undef DEBUG

/* ----------------------------------------------------------- */

#ifdef DEBUG
# define DBG(text) printf text ; fflush(stdout);
#else
# define DBG(text) 
#endif

/* ----------------------------------------------------------- */
/* Setup a set of screens */
/* Screen data is used that best matches the requested parameters. */

#include "screens.h"	/* Pre-generated screen patterns */

/* Screen a single color plane */
void screen_thscreens(			/* Pointer to dither function */
	thscreens *t,			/* Screening object pointer */
	int width, int height,	/* Width and height to screen in pixels */
	int xoff, int yoff,		/* Offset into screening pattern */
	unsigned char *in,		/* Input pixel buffer */
	unsigned long ipitch,	/* Increment between input lines */
	unsigned char *out,		/* Output pixel buffer */
	unsigned long opitch	/* Increment between output lines */
) {
	int i;
	for (i = 0; i < t->np; i++)
		t->sc[i]->screen(t->sc[i], width, height, xoff, yoff, in + 2 * i, t->np, ipitch,
		                                                      out + i, t->np, opitch);
}

/* Delete a thscreens */
void del_thscreens(thscreens *t) {
	int i;

	for (i = 0; i < t->np; i++)
		t->sc[i]->del(t->sc[i]);
	free(t->sc);
	free(t);
}

/* Create a new thscreens object matching the parameters */
thscreens *new_thscreens(
	int exact,				/* Return only exact matches */
	int nplanes,			/* Number of planes to screen */
	double asp,				/* Target aspect ratio (== dpiX/dpiY) */
	int size,				/* Target size */
	sc_iencoding ie,		/* Input encoding - must be scie_16 */
	int oebpc,				/* Output encoding bits per component - must be 8 */
	int oelev,				/* Output encoding levels. Must be <= 2 ^ oebpc */
	int *oevalues,			/* Optional output encoding values for each level */
							/* Must be oelev entries. Default is 0 .. oelev-1 */
	sc_oorder oo,			/* Output bit ordering */
	double overlap,			/* Overlap between levels, 0 - 1.0 */
	void   **cntx,			/* List of contexts for lookup table callback */
	double (**lutfunc)(void *cntx, double in)	/* List of callback function, NULL if none */
) {
	thscreens *t;
	int i, bi = -1;
	double bamatch;		/* Best aspect match */
	int bsize = 100000;	/* Best size match */
	int swap = 0;		/* width and height will need swapping */
	
	DBG(("thscreens: new called with:\n"));
	DBG((" nplanes = 0x%x\n",nplanes));
	DBG((" asp = %f\n",asp));
	DBG((" ie = %d\n",ie));
	DBG((" oebpc = %d\n",oebpc));
	DBG((" oelev = %d\n",oelev));
	DBG((" oo = %d\n",oo));
	DBG((" overlap = %f\n",overlap));

	if (asp < 1.0) {	/* All screens[] have asp >= 1.0 */
		asp = 1.0/asp;
		swap = 1;
		DBG(("thscreens: aspect swap needed\n"));
	}

	if ((t = (thscreens *)calloc(1, sizeof(thscreens))) == NULL) {
		DBG(("thscreens: malloc of thscreens failed\n"));
		return NULL;
	}

	t->np = nplanes; 	/* Number of planes */

	DBG(("thscreens no planes = %d\n",t->np));

	t->screen = screen_thscreens;
	t->del = del_thscreens;

	if ((t->sc = malloc(sizeof(thscreen *) * t->np)) == NULL) {
		free(t);
		DBG(("thscreens: malloc of thscreens->sc[] failed\n"));
		return NULL;
	}

	DBG(("thscreens: searching amongst %d screens, exact = %d\n",NO_SCREENS,exact));

	DBG(("thscreens: looking for non-exact match\n"));

	/* Synthesise a set of screens from what's there */
	/* (Don't bother with matching the colorspace) */
	for (i = 0;i < NO_SCREENS; i++) {
		double thamatch;	/* This aspect match */
		int thsize;			/* This size match */

		thamatch = asp/screens[i].asp;
		if (thamatch < 1.0)
			thamatch = 1.0/thamatch;

		if (bi < 0 || (thamatch < bamatch)) {	/* No best or new best */
			bamatch = thamatch;
			bi = i;
			DBG(("thscreens: new best with aspmatch %f\n",bamatch));
			continue;						/* On to next */
		}
		if (thamatch > bamatch) 			/* Worse aspect match */
			continue;
		/* Same on aspect ratio. Check size */
		thsize = size - screens[i].size;
		if (thsize < 0)
			thsize = -thsize;
		if (thsize < bsize) {				/* New better size match */
			bsize = thsize;
			bi = i;
			DBG(("thscreens: new best with size %d\n",bsize));
		}
	}

	if (bi < 0)			/* Strange */
		return NULL;

	/* Create each screening object from one defined screen. */
	/* Use the 0'th plane screen */
	/* Stagger the screens with a round of 9 offset */
	for (i = 0; i < t->np; i++) {
		int xoff = ((i % 3) * screens[bi].width)/3;
		int yoff = (((i/3) % 3) * screens[bi].height)/3;
		void   *cx = NULL;
		double (*lf)(void *cntx, double in) = 0;
		if (cntx != NULL)
			cx = cntx[i];
		if (lutfunc != NULL)
			lf = lutfunc[i];

		DBG(("thscreens: creating plane %d/%d thscreen, offset %d %d\n",i,t->np,xoff,yoff));
		if ((t->sc[i] = new_thscreen(screens[bi].width, screens[bi].height, xoff, yoff,
		                          screens[bi].asp, swap, screens[bi].list[0],
		                          ie, oebpc, oelev, oevalues, oo, overlap,
		                          cx, lf)) == NULL) {
			for (--i; i >= 0; i--)
				t->sc[i]->del(t->sc[i]);
			free(t->sc);
			free(t);
			DBG(("thscreens: new_thscreen() failed\n"));
			return NULL;
		}
	}
	DBG(("thscreens: returning nonexact match\n"));
	return t;
}

/* ----------------------------------------------------------- */
/* The kernel screening routin */

void thscreen16_8(
	struct _thscreen *t,	/* Screening object pointer */
	int width, int height,	/* Width and height to screen in pixels */
	int xoff, int yoff,		/* Offset into screening pattern (must be +ve) */
	unsigned char *_in,		/* Input pixel buffer */
	unsigned long ipinc,	/* Increment between input pixels */
	unsigned long ipitch,	/* Increment between input lines */
	unsigned char *out,		/* Output pixel buffer */
	unsigned long opinc,	/* Increment between output pixels */
	unsigned long opitch	/* Increment between output lines */
) {
	unsigned short *in = (unsigned short *)_in;	/* Pointer to input pixel sized values */
	int *lut = t->lut;			/* Copy of 8 or 16 -> 16 bit lookup table */
	unsigned char **oth, **eth; /* Current lines start, origin and end in screening table. */
	int thtsize;				/* Overall size of threshold table */
	unsigned char **eeth;		/* Very end of threshold table */
	unsigned short *ein = in + height * ipitch;	/* Vertical end pixel marker */
	unsigned short *ein1;				/* Horizontal end pixel markers */

	{
		unsigned char **sth;				/* Start point of line intable */
		sth = t->thp + (yoff % t->sheight) * t->twidth;
		oth = sth + (xoff % t->swidth);		/* Orgin of pattern to start from */
		eth = sth + t->swidth;				/* Ending point to wrap back */
		thtsize = t->twidth * t->theight;
		eeth = t->thp + thtsize;			/* very end of table */
	}

	ein1 = in + ipinc * width;
	
	/* For each line: */
	for (; in < ein; in += ipitch, ein1 += ipitch, out += opitch) {
		unsigned char **th = oth;	/* Threshold table origin */
		unsigned short *ip = in;	/* Horizontal input pointer */
		unsigned char *op = out;	/* Horizontal output pointer */

		/* Do pixels one output byte at a time */
		for (; ip < ein1; ip += ipinc, op += opinc) {
			int tt = lut[*ip];
			*op = (unsigned char)th[0][tt];
			if (++th >= eth)
				th -= t->swidth;
		}

		/* Advance screen table pointers with vertical wrap */
		oth += t->twidth;
		eth += t->twidth;
		if (eth > eeth) {
			oth -= thtsize;
			eth -= thtsize; 
		} 
	}
}

/* ----------------------------------------------------------- */

/* We're done with the screening object */
static void th_del(
	thscreen *t
) {
	if (t->lut != NULL)
		free(t->lut);
	if (t->thp != NULL)
		free(t->thp);
	free(t);
}

/* Create a new thscreen object */
/* Return NULL on error */
thscreen *new_thscreen(
	int width,					/* width in pixels of screen */
	int height,					/* Height in pixels of screen */
	int xoff, int yoff,			/* Pattern offsets into width & height */
	double asp,					/* Aspect ratio of screen (== dpiX/dpiY) */
	int swap,					/* Swap X & Y to invert aspect ratio  & swap width/height */
	ccoord *thli,				/* Pointer to list of threshold coordinates */
	sc_iencoding ie,			/* Input encoding - must be scie_16 */
	int oebpc,					/* Output encoding bits per component - must be 8 */
	int oelev,					/* Output encoding levels. Must be <= 2 ^ oebpc */
	int *oevalues,				/* Optional output encoding values for each level */
								/* Must be oelev entries. Default is 0 .. oelev-1 */
	sc_oorder oo,				/* Output bit ordering */
	double olap,				/* Overlap between levels, 0 - 1.0 */
	void   *cntx,				/* Context for LUT table callback */
	double (*lutfunc)(void *cntx, double in)	/* Callback function, NULL if none */
) {
	thscreen *t;		/* Object being created */
	int npix;			/* Total pixels in screen */
	double mrang;		/* threshold modulation range */ 
	double **fthr;		/* Floating point threshold array */
	int i, j;

	DBG(("new_thscreen() called, oebpc = %d\n",oebpc));
	DBG(("new_thscreen() called, oelev = %d\n",oelev));

	/* Sanity check overlap */
	if (olap < 0.0)
		olap = 0.0;
	else if (olap > 1.0)
		olap = 1.0;

	/* Sanity check parameters */
	if (ie != scie_16) { 
		DBG(("new_thscreen() ie %d != scie_16\n",ie));
		return NULL;
	}
	if (oebpc != 8) { 
		DBG(("new_thscreen() oebpc %d != 8\n",oebpc));
		return NULL;
	}

	if (oelev < 2 || oelev > (1 << oebpc)) { 
		DBG(("new_thscreen() oelev %d > 2^%d = %d\n",oelev,1 << oebpc,oebpc));
		return NULL;
	}

	if ((t = (thscreen *)calloc(1, sizeof(thscreen))) == NULL) {
		DBG(("new_thscreen() calloc failed\n"));
		return NULL;
	}

	/* Instantiation parameters */
	t->ie = ie;
	t->oebpc = oebpc;
	t->oelev = oelev;
	if (oevalues != NULL) {
		for (i = 0; i < t->oelev; i++) {
			if (oevalues[i] >= (1 << t->oebpc)) {
				DBG(("new_thscreen() oevalues[%d] value %d can't fit in %d bits\n",i,oevalues[i],t->oebpc));
				free(t);
				return NULL;
			}
			t->oevalues[i] = oevalues[i];
		}
	} else {
		for (i = 0; i < t->oelev; i++)
			t->oevalues[i] = i;
	}

	t->oo = oo;
	t->overlap = olap;

	/* Create a suitable LUT from the given function */
	/* Input is either 8 or 16 bits, output is always 16 bits */
	DBG(("new_thscreen() about to create LUT\n"));
	if ((t->lut = (int *)malloc(sizeof(int) * 65536)) == NULL) {
		free(t);
		DBG(("new_thscreen() malloc of 16 bit LUT failed\n"));
		return NULL;
	}
	for (i = 0; i < 65536; i++) {
		if (lutfunc != NULL) {
			double v = i/65535.0;
			v = lutfunc(cntx, v);
			t->lut[i] = (int)(v * 65535.0 + 0.5);
		} else
			t->lut[i] = i;
	}

	/* Screen definition parameters */
	if (swap) {
		t->asp = 1.0/asp;
		t->swidth = height;
		t->sheight = width;
	} else {
		t->asp = asp;
		t->swidth = width;
		t->sheight = height;
	}
	DBG(("new_thscreen() target width %d, height %d, asp %f\n",t->swidth,t->sheight,t->asp));
	DBG(("new_thscreen() given width %d, height %d, asp %f\n",width,height,asp));

	npix = t->swidth * t->sheight;	/* Total pixels */
							/* Allow for read of a words worth of pixels from within screen: */
	DBG(("new_thscreen() tot pix %d, lev %d, bpp %d\n",npix,t->oelev,t->oebpc));

	t->twidth = t->swidth + (8/t->oebpc) -1;
	t->theight = t->sheight;

	DBG(("new_thscreen() th table size = %d x %d\n",t->twidth,t->theight));
	DBG(("new_thscreen() about to turn screen list into float threshold matrix\n"));

	/* Convert the list of screen cells into a floating point threshold array */
	fthr = dmatrix(0, t->sheight-1, 0, t->swidth-1);	/* Temporary matrix */
	if (swap) {
		double tt = xoff;	/* Swap offsets to align with orientation */
		xoff = yoff;
		yoff = tt;
		for (i = 0; i < npix; i++)
			fthr[thli[i].x][thli[i].y] = (double)(i/(npix - 1.0));
	} else {
		for (i = 0; i < npix; i++)
			fthr[thli[i].y][thli[i].x] = (double)(i/(npix - 1.0));
	}

	/* The range that the screen has to modulate */
	/* over to cross all the thresholds evenly. */
	mrang = 65535.0/(t->oelev - 1.0); 
	DBG(("new_thscreen() raw modulation rande = %f\n",mrang));

	/* Modify the modulation range to accomodate any level overlap */
	if (olap > 0.0 && t->oelev > 2) {
		mrang = ((t->oelev - 2.0) * olap * mrang + 65535.0)/(t->oelev - 1.0);
		DBG(("new_thscreen() modulation adjusted for overlap = %f\n",mrang));
	}

	/* Init the threshold table. It holds the quantized, encoded output */
	/* values, allowing an input value offset by the screen to be */
	/* thresholded directly into the output value. We allow a guard band at */
	/* each end for the effects of the screen modulating the input value. */

	DBG(("new_thscreen() about to init threshold table\n"));
	t->tht = &t->_tht[32768];	/* base allows for -ve & +ve range */
	for (i = -32768; i < (2 * 65536) + 32768; i++) {
		if (i < mrang) {				/* Lower guard band */
			t->tht[i] = t->oevalues[0];
		} else if (i >= 65535) {		/* Upper guard band */
			t->tht[i] = t->oevalues[t->oelev-1];
		} else {						/* Middle range */
			t->tht[i] = t->oevalues[1 + (int)((t->oelev - 2.0) * (i - mrang)/(65535.0 - mrang))];
		}
	}

	/* Allocate the 2D table of pointers into the */
	/* threshold table that encodes the screen offset. */
	if ((t->thp = (unsigned char **)malloc(sizeof(unsigned char *)
	                                       * t->twidth * t->theight)) == NULL) {
		free_dmatrix(fthr, 0, t->sheight-1, 0, t->swidth-1);
		free(t->lut);
		free(t);
		DBG(("new_thscreen() malloc of threshold pointer matrix failed\n"));
		return NULL;
	}

	/* Setup the threshold pointer array to point into the apropriate */
	/* point into the threshold array itself. This implicitly adds */
	/* the screen pattern offset value to the input before thresholding it. */
	/* The input screen offsets are applied at this point too. */
	DBG(("new_thscreen() about to init threshold pointer table\n"));
	for (i = 0; i < t->twidth; i++) {
		for (j = 0; j < t->theight; j++) {
			double sov = fthr[(j+yoff) % t->sheight][(i+xoff) % t->swidth];
			int    tho = (int)((mrang - 1.0) * (1.0 - sov) + 0.5);
			t->thp[j * t->twidth + i] = &t->tht[tho];
		}
	}
	free_dmatrix(fthr, 0, t->sheight-1, 0, t->swidth-1);

	DBG(("new_thscreen() about to setup method pointers\n"));

	/* Methods */
	t->screen = thscreen16_8;
	t->del = th_del;

	DBG(("new_thscreen() done\n"));
	return t;
}



















