#ifndef BIGTIFF_H
#define BIGTIFF_H

#include "benlib.h"

typedef struct _coord {
    i64 x;
    i64 z;
} coord;

typedef struct _image_data {
    unsigned short *pixels;
    i64 width;
    i64 height;
    coord origin;
} image_data;

i32 load_height_map(const char *filepath, unsigned char *buffer, image_data *data);

#endif // !!BIGTIFF_H
