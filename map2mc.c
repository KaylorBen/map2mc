#include "map2mc.h"

#include <time.h>

#include "benlib.h"
#include "endian.h"
#include "libdeflate.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

extern int verbose_flag;

i32 load_height_map(const char *filepath, unsigned char *buffer, image_data *data) {
    buffer = stbi_load(filepath, &data->width, &data->height, &data->channels, 3);
    if (buffer == NULL) {
        printf("Error loading image %s\n", filepath);
        return -1;
    }
    data->pixels = (void *)buffer;
    data->origin.x = data->width / 2;
    data->origin.z = data->height / 2;
    return 0;
}

// *****************************************************************
//                        NBT Util Functions
// *****************************************************************

static i32 write_nbt_key(char *buffer, const char *key, u16 len) {
    u16 big_e_len = __bswap_16(len);
    memcpy(buffer, &big_e_len, sizeof(len));
    memcpy(buffer + 2, key, len);
    return len + 2;
}

static i32 write_nbt_end(char *buffer) {
    buffer[0] = NBT_TAG_End;
    return 1;
}

static i32 write_nbt_byte(char *buffer, const char *key, u16 len, char value) {
    buffer[0] = NBT_TAG_Byte;
    write_nbt_key(buffer + 1, key, len);
    buffer[3 + len] = value;
    return 3 + len + sizeof(value);
}

static i32 write_nbt_short(char *buffer, const char *key, u16 len, i16 value) {
    i16 big_e_val = __bswap_16(value);
    buffer[0] = NBT_TAG_Short;
    write_nbt_key(buffer + 1, key, len);
    memcpy(buffer + 3 + len, &big_e_val, sizeof(value));
    return 3 + len + sizeof(value);
}

static i32 write_nbt_int(char *buffer, const char *key, u16 len, i32 value) {
    i32 big_e_val = __bswap_32(value);
    buffer[0] = NBT_TAG_Int;
    write_nbt_key(buffer + 1, key, len);
    memcpy(buffer + 3 + len, &big_e_val, sizeof(value));
    return 3 + len + sizeof(value);
}

static i32 write_nbt_long(char *buffer, const char *key, u16 len, i64 value) {
    i64 big_e_val = __bswap_64(value);
    buffer[0] = NBT_TAG_Long;
    write_nbt_key(buffer + 1, key, len);
    memcpy(buffer + 3 + len, &big_e_val, sizeof(value));
    return 3 + len + sizeof(value);
}

static i32 write_nbt_float(char *buffer, const char *key, u16 len, f32 value) {
    i32 *ptr = (void *)&value;
    f32 big_e_val = __bswap_32(*ptr);
    buffer[0] = NBT_TAG_Float;
    write_nbt_key(buffer + 1, key, len);
    memcpy(buffer + 3 + len, &big_e_val, sizeof(value));
    return 3 + len + sizeof(value);
}

static i32 write_nbt_double(char *buffer, const char *key, u16 len, f64 value) {
    i64 *ptr = (void *)&value;
    f64 big_e_val = __bswap_64(*ptr);
    buffer[0] = NBT_TAG_Double;
    write_nbt_key(buffer + 1, key, len);
    memcpy(buffer + 3 + len, &big_e_val, sizeof(value));
    return 3 + len + sizeof(value);
}

static i32 write_nbt_byte_array(char *buffer, const char *key, u16 len, char *arr, i32 size) {
    i32 big_e = __bswap_32(size);
    buffer[0] = NBT_TAG_Byte_Array;
    write_nbt_key(buffer + 1, key, len);
    memcpy(buffer + 3 + len, &big_e, sizeof(size));
    memcpy(buffer + 3 + len + sizeof(size), arr, size);
    return 3 + len + sizeof(size) + size;
}

static i32 write_nbt_string(char *buffer, const char *key, u16 len, char *str, u16 str_len) {
    u16 big_e = __bswap_16(str_len);
    buffer[0] = NBT_TAG_String;
    write_nbt_key(buffer + 1, key, len);
    memcpy(buffer + 3 + len, &big_e, sizeof(str_len));
    memcpy(buffer + 3 + len + sizeof(str_len), str, str_len);
    return 3 + len + sizeof(str_len) + str_len;
}

// only writes the list header, need to add list_len elements of type content as well after
static i32 write_nbt_list(char *buffer, const char *key, u16 len, char content, i32 list_len) {
    i32 big_e = __bswap_32(list_len);
    buffer[0] = NBT_TAG_List;
    write_nbt_key(buffer + 1, key, len);
    buffer[3 + len] = content;
    memcpy(buffer + 3 + len + sizeof(content), &big_e, sizeof(list_len));
    return 3 + len + sizeof(content) + sizeof(list_len);
}

static i32 write_nbt_compound(char *buffer, const char *key, u16 len) {
    buffer[0] = NBT_TAG_Compound;
    return write_nbt_key(buffer + 1, key, len) + 1;
}

// only writes the array header, need to add size TAG_Int elements after
static i32 write_nbt_int_array(char *buffer, const char *key, u16 len, i32 *arr, i32 size) {
    i32 big_e_val = __bswap_32(size);
    buffer[0] = NBT_TAG_Int_Array;
    write_nbt_key(buffer + 1, key, len);
    memcpy(buffer + 3 + len, &big_e_val, sizeof(size));
    i32 val;
    for (i32 i = 0; i < size; i++) {
        val = arr[i];
        big_e_val = __bswap_32(val);
        memcpy(buffer + 3 + len + sizeof(size) + (i * sizeof(i32)), &big_e_val, sizeof(val));
    }
    return 3 + len + sizeof(size) + (size * sizeof(val));
}

// only writes the array header, need to add size TAG_Long elements after
static i32 write_nbt_long_array(char *buffer, const char *key, u16 len, i64 *arr, i32 size) {
    i32 big_e = __bswap_32(size);
    buffer[0] = NBT_TAG_Long_Array;
    write_nbt_key(buffer + 1, key, len);
    memcpy(buffer + 3 + len, &big_e, sizeof(size));
    i64 val, big_e_val;
    for (i32 i = 0; i < size; i++) {
        val = arr[i];
        big_e_val = __bswap_64(val);
        memcpy(buffer + 3 + len + sizeof(size) + (i * sizeof(i64)), &big_e_val, sizeof(val));
    }
    return 3 + len + sizeof(size) + (size * sizeof(val));
}

static i32 write_nbt_list_str(char *buffer, char *str, u16 str_len) {
    u16 big_e = __bswap_16(str_len);
    memcpy(buffer, &big_e, sizeof(str_len));
    memcpy(buffer + 2, str, str_len);
    return 2 + str_len;
}

static i32 write_nbt_list_long(char *buffer, i64 size) {
    i64 big_e_val = __bswap_64(size);
    memcpy(buffer, &big_e_val, sizeof(size));
    return sizeof(size);
}

// *****************************************************************
//                    Block Data Gen
// *****************************************************************

static i32 write_section(char *section_buffer, image_data *image, i32 x, i32 z, i32 y) {
    i32 section_write_count = 0;
    section_write_count += write_nbt_compound(&section_buffer[section_write_count], STR("biomes"));
    section_write_count +=
        write_nbt_list(&section_buffer[section_write_count], STR("palette"), NBT_TAG_String, 1);
    // TODO for now biomes are static
    section_write_count +=
        write_nbt_list_str(&section_buffer[section_write_count], STR("minecraft:plains"));
    section_write_count += write_nbt_end(&section_buffer[section_write_count]);  // end biome
    section_write_count +=
        write_nbt_compound(&section_buffer[section_write_count], STR("block_states"));
    section_write_count +=
        write_nbt_list(&section_buffer[section_write_count], STR("palette"), NBT_TAG_Compound, 2);
    section_write_count +=
        write_nbt_string(&section_buffer[section_write_count], STR("Name"), STR("minecraft:air"));
    section_write_count += write_nbt_end(&section_buffer[section_write_count]);
    section_write_count +=
        write_nbt_string(&section_buffer[section_write_count], STR("Name"), STR("minecraft:dirt"));
    section_write_count += write_nbt_end(&section_buffer[section_write_count]);
    i64 block_data[256];
    coord chunkPos = {image->origin.x + (x * 16), image->origin.z + (z * 16)};
    u64 block_set;
    for (i32 yPos = 0; yPos < 16; yPos++) {
        for (i32 zPos = 0; zPos < 16; zPos++) {
            block_set = 0;
            for (i32 xPos = 0; xPos < 16; xPos++) {
                if (image->pixels[chunkPos.z * image->width + chunkPos.x].red > (y * 16) + yPos) {
                    block_set += 1;
                }
                block_set <<= 4;
            }
            block_data[yPos * 16 + zPos] = block_set;
        }
    }
    section_write_count += write_nbt_long_array(&section_buffer[section_write_count], STR("data"),
                                                block_data, ARRAY_COUNT(block_data));
    section_write_count += write_nbt_end(&section_buffer[section_write_count]);  // block_states
    section_write_count += write_nbt_byte(&section_buffer[section_write_count], STR("Y"), y);
    section_write_count += write_nbt_end(&section_buffer[section_write_count]);  // section
    return section_write_count;
}

// *****************************************************************
//                       Chunk Gen
// *****************************************************************

static i32 write_chunk(char *chunk_buffer, image_data *image, i32 x, i32 z) {
    i32 write_count = 0;
    // standardized fields
    write_count += write_nbt_compound(&chunk_buffer[write_count], STR(""));
    write_count += write_nbt_int(&chunk_buffer[write_count], STR("DataVersion"), 3465);
    write_count += write_nbt_int(&chunk_buffer[write_count], STR("xPos"), x);
    write_count += write_nbt_int(&chunk_buffer[write_count], STR("zPos"), z);
    write_count += write_nbt_int(&chunk_buffer[write_count], STR("yPos"), -4);
    write_count += write_nbt_string(&chunk_buffer[write_count], STR("Status"), STR("full"));
    write_count += write_nbt_list(&chunk_buffer[write_count], STR("block_entities"), 0, 0);
    write_count += write_nbt_byte(&chunk_buffer[write_count], STR("isLightOn"), 1);
    write_count += write_nbt_long(&chunk_buffer[write_count], STR("InhabitedTime"), 0);
    write_count += write_nbt_compound(&chunk_buffer[write_count], STR("Heightmaps"));
    write_count += write_nbt_end(&chunk_buffer[write_count]);  // "Heightmaps"
    write_count +=
        write_nbt_list(&chunk_buffer[write_count], STR("sections"), NBT_TAG_Compound, 24);
    for (i32 y = -4; y < 20; y++) {
        write_count += write_section(&chunk_buffer[write_count], image, x, z, y);
    }
    write_count += write_nbt_long(&chunk_buffer[write_count], STR("LastUpdate"), 0);
    write_count += write_nbt_end(&chunk_buffer[write_count]);  // ""
    if (verbose_flag == 1) printf("Wrote chunk (%d, %d)\n", x, z);
    return write_count;
}

// *****************************************************************
//                       Region Gen
// *****************************************************************

typedef struct _location {
    u32 offset : 24;
    u32 sector_count : 8;
} location;

typedef struct _timestamp {
    u32 time;
} timestamp;

i32 gen_region(char *region_file_buffer, image_data *image, i32 x, i32 z) {
    struct libdeflate_compressor *compressor = libdeflate_alloc_compressor(1);
    if (verbose_flag == 1)
        printf("Generating Region: (%d, %d)\n", x, z);
    i32 chunk_size, big_e;
    i32 chunk_count = 0;
    i32 sector_counter = 2;
    i64 file_pos;
    u64 destLen;
    location loc;
    timestamp t;
    t.time = (unsigned)time(NULL);
    char *locations_header = region_file_buffer;
    char *timestamps_header = region_file_buffer + 0x1000;
    char chunk_buffer[MEGABYTES(1)];
    for (i32 i = 0; i < 32; i++) {
        for (i32 j = 0; j < 32; j++) {
            file_pos = sector_counter * 0x1000;
            memset(chunk_buffer, 0x00, MEGABYTES(1));
            chunk_size = write_chunk(chunk_buffer, image, x * 32 + i, z * 32 + j);
            destLen = libdeflate_zlib_compress(compressor, (chunk_buffer), chunk_size, (region_file_buffer + file_pos + 5), MEGABYTES(1));
            // compress((unsigned char *)(region_file_buffer + file_pos + 5), &destLen,
            //            (unsigned char *)chunk_buffer, chunk_size);
            big_e = __bswap_32((i32)destLen - 1);
            memcpy(region_file_buffer + file_pos, &big_e, sizeof(big_e));
            region_file_buffer[file_pos + sizeof(big_e)] = 0x02;  // compression type
            loc.offset = (__bswap_32(sector_counter) >> 8);
            loc.sector_count = (destLen / 4096) + 1;
            sector_counter += loc.sector_count;
            memcpy(&locations_header[chunk_count * 4], &loc, sizeof(loc));
            memcpy(&timestamps_header[chunk_count * 4], &t, sizeof(t));
            chunk_count++;
        }
    }
    libdeflate_free_compressor(compressor);
    return sector_counter * 4096;
}
