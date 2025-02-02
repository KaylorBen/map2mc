#ifndef MAP2MC_H
#define MAP2MC_H

#include "benlib.h"

typedef struct _pixel3 {  // for some reason heightmap is 3 channels, so doing this for now
    u8 red;
    u8 green;
    u8 blue;
} pixel3;

typedef struct _thread_input_data {
    pixel3 *imgStart;
    i32 imgWidth;
} thread_input_data;

typedef struct _coord {
    i32 x;
    i32 z;
} coord;

typedef struct _image_data {
    pixel3 *pixels;
    i32 width;
    i32 height;
    i32 channels;
    coord origin;
} image_data;

enum : u8 {
    NBT_TAG_End,
    NBT_TAG_Byte,
    NBT_TAG_Short,
    NBT_TAG_Int,
    NBT_TAG_Long,
    NBT_TAG_Float,
    NBT_TAG_Double,
    NBT_TAG_Byte_Array,
    NBT_TAG_String,
    NBT_TAG_List,
    NBT_TAG_Compound,
    NBT_TAG_Int_Array,
    NBT_TAG_Long_Array,
};

i32 load_height_map(const char *filepath, unsigned char *buffer, image_data *data);
i32 gen_region(char *region_file_buffer, image_data *image, i32 x, i32 z);
i32 gen_level_data(char *level_data_dest);

#endif  // !MAP2MC_H
