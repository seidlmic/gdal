/******************************************************************************
 *
 * Project:  libgeotiff
 * Purpose:  Code to abstract translation between pixel/line and PCS
 *           coordinates.
 * Author:   Frank Warmerdam, warmerda@home.com
 *
 ******************************************************************************
 * Copyright (c) 1999, Frank Warmerdam
 *
 * SPDX-License-Identifier: MIT
 *****************************************************************************/

#include <math.h>
#include <stddef.h>

#include "geotiff.h"
#include "geo_tiffp.h" /* external TIFF interface */
#include "geo_keyp.h"  /* private interface       */
#include "geokeys.h"

/************************************************************************/
/*                          inv_geotransform()                          */
/*                                                                      */
/*      Invert a 6 term geotransform style matrix.                      */
/************************************************************************/

static int inv_geotransform( double *gt_in, double *gt_out )

{
    /* we assume a 3rd row that is [0 0 1] */

    /* Compute determinate */

    const double det = gt_in[0] * gt_in[4] - gt_in[1] * gt_in[3];

    if( fabs(det) < 0.000000000000001 )
        return 0;

    const double inv_det = 1.0 / det;

    /* compute adjoint, and divide by determinate */

    gt_out[0] =  gt_in[4] * inv_det;
    gt_out[3] = -gt_in[3] * inv_det;

    gt_out[1] = -gt_in[1] * inv_det;
    gt_out[4] =  gt_in[0] * inv_det;

    gt_out[2] = ( gt_in[1] * gt_in[5] - gt_in[2] * gt_in[4]) * inv_det;
    gt_out[5] = (-gt_in[0] * gt_in[5] + gt_in[2] * gt_in[3]) * inv_det;

    return 1;
}

/************************************************************************/
/*                       GTIFTiepointTranslate()                        */
/************************************************************************/

static
int GTIFTiepointTranslate( int gcp_count, double * gcps_in, double * gcps_out,
                           double x_in, double y_in,
                           double *x_out, double *y_out )

{
    (void) gcp_count;
    (void) gcps_in;
    (void) gcps_out;
    (void) x_in;
    (void) y_in;
    (void) x_out;
    (void) y_out;

    /* I would appreciate a _brief_ block of code for doing second order
       polynomial regression here! */
    return FALSE;
}


/************************************************************************/
/*                           GTIFImageToPCS()                           */
/************************************************************************/

/**
 * Translate a pixel/line coordinate to projection coordinates.
 *
 * At this time this function does not support image to PCS translations for
 * tiepoints-only definitions,  only pixelscale and transformation matrix
 * formulations.
 *
 * @param gtif The handle from GTIFNew() indicating the target file.
 * @param x A pointer to the double containing the pixel offset on input,
 * and into which the easting/longitude will be put on completion.
 * @param y A pointer to the double containing the line offset on input,
 * and into which the northing/latitude will be put on completion.
 *
 * @return TRUE if the transformation succeeds, or FALSE if it fails.  It may
 * fail if the file doesn't have properly setup transformation information,
 * or it is in a form unsupported by this function.
 */

int GTIFImageToPCS( GTIF *gtif, double *x, double *y )

{
    tiff_t *tif=gtif->gt_tif;

    int     tiepoint_count;
    double *tiepoints   = 0;
    if (!(gtif->gt_methods.get)(tif, GTIFF_TIEPOINTS,
                              &tiepoint_count, &tiepoints ))
        tiepoint_count = 0;

    int     count;
    double *pixel_scale = 0;
    if (!(gtif->gt_methods.get)(tif, GTIFF_PIXELSCALE, &count, &pixel_scale ))
        count = 0;

    int     transform_count;
    double *transform   = 0;
    if (!(gtif->gt_methods.get)(tif, GTIFF_TRANSMATRIX,
                                &transform_count, &transform ))
        transform_count = 0;

/* -------------------------------------------------------------------- */
/*      If the pixelscale count is zero, but we have tiepoints use      */
/*      the tiepoint based approach.                                    */
/* -------------------------------------------------------------------- */
    int res = FALSE;
    if( tiepoint_count > 6 && count == 0 )
    {
        res = GTIFTiepointTranslate( tiepoint_count / 6,
                                     tiepoints, tiepoints + 3,
                                     *x, *y, x, y );
    }

/* -------------------------------------------------------------------- */
/*	If we have a transformation matrix, use it. 			*/
/* -------------------------------------------------------------------- */
    else if( transform_count == 16 )
    {
        const double x_in = *x;
        const double y_in = *y;

        *x = x_in * transform[0] + y_in * transform[1] + transform[3];
        *y = x_in * transform[4] + y_in * transform[5] + transform[7];

        res = TRUE;
    }

/* -------------------------------------------------------------------- */
/*      For now we require one tie point, and a valid pixel scale.      */
/* -------------------------------------------------------------------- */
    else if( count < 3 || tiepoint_count < 6 )
    {
        res = FALSE;
    }

    else
    {
        *x = (*x - tiepoints[0]) * pixel_scale[0] + tiepoints[3];
        *y = (*y - tiepoints[1]) * (-1 * pixel_scale[1]) + tiepoints[4];

        res = TRUE;
    }

/* -------------------------------------------------------------------- */
/*      Cleanup                                                         */
/* -------------------------------------------------------------------- */
    if(tiepoints)
        _GTIFFree(tiepoints);
    if(pixel_scale)
        _GTIFFree(pixel_scale);
    if(transform)
        _GTIFFree(transform);

    return res;
}

/************************************************************************/
/*                           GTIFPCSToImage()                           */
/************************************************************************/

/**
 * Translate a projection coordinate to pixel/line coordinates.
 *
 * At this time this function does not support PCS to image translations for
 * tiepoints-only based definitions, only matrix and pixelscale/tiepoints
 * formulations are supposed.
 *
 * @param gtif The handle from GTIFNew() indicating the target file.
 * @param x A pointer to the double containing the pixel offset on input,
 * and into which the easting/longitude will be put on completion.
 * @param y A pointer to the double containing the line offset on input,
 * and into which the northing/latitude will be put on completion.
 *
 * @return TRUE if the transformation succeeds, or FALSE if it fails.  It may
 * fail if the file doesn't have properly setup transformation information,
 * or it is in a form unsupported by this function.
 */

int GTIFPCSToImage( GTIF *gtif, double *x, double *y )

{
    tiff_t 	*tif=gtif->gt_tif;
    int		result = FALSE;

/* -------------------------------------------------------------------- */
/*      Fetch tiepoints and pixel scale.                                */
/* -------------------------------------------------------------------- */
    double 	*tiepoints = NULL;
    int 	tiepoint_count;
    if (!(gtif->gt_methods.get)(tif, GTIFF_TIEPOINTS,
                              &tiepoint_count, &tiepoints ))
        tiepoint_count = 0;

    int 	count;
    double	*pixel_scale = NULL;
    if (!(gtif->gt_methods.get)(tif, GTIFF_PIXELSCALE, &count, &pixel_scale ))
        count = 0;

    int 	transform_count = 0;
    double 	*transform   = NULL;
    if (!(gtif->gt_methods.get)(tif, GTIFF_TRANSMATRIX,
                                &transform_count, &transform ))
        transform_count = 0;

/* -------------------------------------------------------------------- */
/*      If the pixelscale count is zero, but we have tiepoints use      */
/*      the tiepoint based approach.                                    */
/* -------------------------------------------------------------------- */
    if( tiepoint_count > 6 && count == 0 )
    {
        result = GTIFTiepointTranslate( tiepoint_count / 6,
                                        tiepoints + 3, tiepoints,
                                        *x, *y, x, y );
    }

/* -------------------------------------------------------------------- */
/*      Handle matrix - convert to "geotransform" format, invert and    */
/*      apply.                                                          */
/* -------------------------------------------------------------------- */
    else if( transform_count == 16 )
    {
        const double x_in = *x;
	const double y_in = *y;
        double gt_in[6];
        gt_in[0] = transform[0];
        gt_in[1] = transform[1];
        gt_in[2] = transform[3];
        gt_in[3] = transform[4];
        gt_in[4] = transform[5];
        gt_in[5] = transform[7];

        double gt_out[6];
        if( !inv_geotransform( gt_in, gt_out ) )
            result = FALSE;
        else
        {
            *x = x_in * gt_out[0] + y_in * gt_out[1] + gt_out[2];
            *y = x_in * gt_out[3] + y_in * gt_out[4] + gt_out[5];

            result = TRUE;
        }
    }

/* -------------------------------------------------------------------- */
/*      For now we require one tie point, and a valid pixel scale.      */
/* -------------------------------------------------------------------- */
    else if( count >= 3 && tiepoint_count >= 6 )
    {
        *x = (*x - tiepoints[3]) / pixel_scale[0] + tiepoints[0];
        *y = (*y - tiepoints[4]) / (-1 * pixel_scale[1]) + tiepoints[1];

        result = TRUE;
    }

/* -------------------------------------------------------------------- */
/*      Cleanup.                                                        */
/* -------------------------------------------------------------------- */
    if(tiepoints)
        _GTIFFree(tiepoints);
    if(pixel_scale)
        _GTIFFree(pixel_scale);
    if(transform)
        _GTIFFree(transform);

    return result;
}
