/*
 * img.h -- raster images for figures.
 */
#ifndef IMG_H
#define IMG_H

#include "tekx.h"

typedef struct {
	int w, h;	/* pixels */
	int comps;	/* 1 gray, 3 RGB */
	bool jpeg;	/* data is a JPEG file, for DCTDecode */
	u8 *data;	/* 8-bit samples, or the JPEG file */
	size_t len;
	u8 *alpha;	/* 8-bit alpha samples, or NULL */
	size_t alen;
} Image;

Image *image_load(const char *path, char **err);

#endif
