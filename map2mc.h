#ifndef MAP2MC_H
#define MAP2MC_H

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
i32 gen_level_data(char *level_data_dest, char *world_name);

#endif  // !MAP2MC_H
