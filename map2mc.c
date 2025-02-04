#include "map2mc.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "benlib.h"
#include "libdeflate.h"

#define DIRT_MAX (16 * 16 * 16)

extern int verbose_flag, water_level;

#pragma pack(push, 1)
typedef struct {
    u16 signature;
    u32 size;
    u16 reserved1;
    u16 reserved2;
    u32 pixel_offset;
} BMP_file_header;

typedef struct {
    u32 header_size;
    u32 width;
    u32 height;
    u16 color_planes;
    u16 bit_per_color;
} DIB_header_useful;
#pragma pack(pop)

i32 load_height_map(const char *filepath, unsigned char *buffer, image_data *data) {

    FILE *img = fopen(filepath, "rb");
    if (!img) {
        printf("Error loading image %s\n", filepath);
        fclose(img);
        exit(EXIT_FAILURE);
    }

    BMP_file_header file_header;
    DIB_header_useful img_info;

    fread(&file_header, sizeof(BMP_file_header), 1, img);
    if (file_header.signature != 0x4D42) {
        printf("Invalid image format. BMP image required.\n");
        fclose(img);
        exit(EXIT_FAILURE);
    }

    fread(&img_info, sizeof(DIB_header_useful), 1, img);

    data->width = img_info.width;
    data->height = img_info.height;

    i32 rowSize = (img_info.width);
    i32 padding = rowSize % 4;

    fseek(img, file_header.pixel_offset, SEEK_SET);

    for (i32 i = 0; i < img_info.height; i++) {
        fread(buffer + (img_info.height - i - 1) * (rowSize + padding), 1, rowSize, img);
        fseek(img, padding, SEEK_CUR);
    }

    fclose(img);

    data->pixels = (void *)buffer;
    data->origin.x = data->width / 2;
    data->origin.z = data->height / 2;
    return 0;
}

// *****************************************************************
//                        NBT Util Functions
// *****************************************************************

static i32 write_nbt_key(char *buffer, const char *key, u16 len) {
    u16 big_e_len = bswap_16(len);
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
    i16 big_e_val = bswap_16(value);
    buffer[0] = NBT_TAG_Short;
    write_nbt_key(buffer + 1, key, len);
    memcpy(buffer + 3 + len, &big_e_val, sizeof(value));
    return 3 + len + sizeof(value);
}

static i32 write_nbt_int(char *buffer, const char *key, u16 len, i32 value) {
    i32 big_e_val = bswap_32(value);
    buffer[0] = NBT_TAG_Int;
    write_nbt_key(buffer + 1, key, len);
    memcpy(buffer + 3 + len, &big_e_val, sizeof(value));
    return 3 + len + sizeof(value);
}

static i32 write_nbt_long(char *buffer, const char *key, u16 len, i64 value) {
    i64 big_e_val = bswap_64(value);
    buffer[0] = NBT_TAG_Long;
    write_nbt_key(buffer + 1, key, len);
    memcpy(buffer + 3 + len, &big_e_val, sizeof(value));
    return 3 + len + sizeof(value);
}

static i32 write_nbt_float(char *buffer, const char *key, u16 len, f32 value) {
    i32 *ptr = (void *)&value;
    i32 big_e_val = bswap_32(*ptr);
    buffer[0] = NBT_TAG_Float;
    write_nbt_key(buffer + 1, key, len);
    memcpy(buffer + 3 + len, &big_e_val, sizeof(value));
    return 3 + len + sizeof(value);
}

static i32 write_nbt_double(char *buffer, const char *key, u16 len, f64 value) {
    i64 *ptr = (void *)&value;
    i64 big_e_val = bswap_64(*ptr);
    buffer[0] = NBT_TAG_Double;
    write_nbt_key(buffer + 1, key, len);
    memcpy(buffer + 3 + len, &big_e_val, sizeof(value));
    return 3 + len + sizeof(value);
}

static i32 write_nbt_byte_array(char *buffer, const char *key, u16 len, char *arr, i32 size) {
    i32 big_e = bswap_32(size);
    buffer[0] = NBT_TAG_Byte_Array;
    write_nbt_key(buffer + 1, key, len);
    memcpy(buffer + 3 + len, &big_e, sizeof(size));
    memcpy(buffer + 3 + len + sizeof(size), arr, size);
    return 3 + len + sizeof(size) + size;
}

static i32 write_nbt_string(char *buffer, const char *key, u16 len, char *str, u16 str_len) {
    u16 big_e = bswap_16(str_len);
    buffer[0] = NBT_TAG_String;
    write_nbt_key(buffer + 1, key, len);
    memcpy(buffer + 3 + len, &big_e, sizeof(str_len));
    memcpy(buffer + 3 + len + sizeof(str_len), str, str_len);
    return 3 + len + sizeof(str_len) + str_len;
}

// only writes the list header, need to add list_len elements of type content as well after
static i32 write_nbt_list(char *buffer, const char *key, u16 len, char content, i32 list_len) {
    i32 big_e = bswap_32(list_len);
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
    i32 big_e_val = bswap_32(size);
    buffer[0] = NBT_TAG_Int_Array;
    write_nbt_key(buffer + 1, key, len);
    memcpy(buffer + 3 + len, &big_e_val, sizeof(size));
    i32 val;
    for (i32 i = 0; i < size; i++) {
        val = arr[i];
        big_e_val = bswap_32(val);
        memcpy(buffer + 3 + len + sizeof(size) + (i * sizeof(i32)), &big_e_val, sizeof(val));
    }
    return 3 + len + sizeof(size) + (size * sizeof(val));
}

// only writes the array header, need to add size TAG_Long elements after
static i32 write_nbt_long_array(char *buffer, const char *key, u16 len, i64 *arr, i32 size) {
    i32 big_e = bswap_32(size);
    buffer[0] = NBT_TAG_Long_Array;
    write_nbt_key(buffer + 1, key, len);
    memcpy(buffer + 3 + len, &big_e, sizeof(size));
    i64 val, big_e_val;
    for (i32 i = 0; i < size; i++) {
        val = arr[i];
        big_e_val = bswap_64(val);
        memcpy(buffer + 3 + len + sizeof(size) + (i * sizeof(i64)), &big_e_val, sizeof(val));
    }
    return 3 + len + sizeof(size) + (size * sizeof(val));
}

static i32 write_nbt_list_str(char *buffer, char *str, u16 str_len) {
    u16 big_e = bswap_16(str_len);
    memcpy(buffer, &big_e, sizeof(str_len));
    memcpy(buffer + 2, str, str_len);
    return 2 + str_len;
}

static i32 write_nbt_list_long(char *buffer, i64 value) {
    i64 big_e_val = bswap_64(value);
    memcpy(buffer, &big_e_val, sizeof(value));
    return sizeof(value);
}

static i32 write_nbt_list_double(char *buffer, double value) {
    i64 *ptr = (void *)&value;
    i64 big_e_val = bswap_64(*ptr);
    memcpy(buffer, &big_e_val, sizeof(value));
    return sizeof(value);
}

// *****************************************************************
//                    Block Data Gen
// *****************************************************************

static i32 write_section(char *section_buffer, image_data *image, i32 x, i32 z, i32 y) {
    i32 section_write_count = 0;
    section_write_count += write_nbt_compound(&section_buffer[section_write_count], STR("biomes"));
    section_write_count += write_nbt_list(&section_buffer[section_write_count], STR("palette"), NBT_TAG_String, 1);
    // TODO for now biomes are static
    section_write_count += write_nbt_list_str(&section_buffer[section_write_count], STR("minecraft:plains"));
    section_write_count += write_nbt_end(&section_buffer[section_write_count]);  // end biome
    section_write_count += write_nbt_compound(&section_buffer[section_write_count], STR("block_states"));
    i64 block_data[256];
    i64 sky_light[256];
    i64 sky_light_set;
    coord chunk_pos = {image->origin.x + (x * 16), image->origin.z + (z * 16)};
    u64 block_count = 0;
    u64 block_set;
    u32 greyscale_lvl, y_lvl;
    for (i32 yPos = 0; yPos < 16; yPos++) {
        for (i32 zPos = 0; zPos < 16; zPos++) {
            block_set = 0;
            sky_light_set = 0;
            for (i32 xPos = 15; xPos >= 0; xPos--) {
                greyscale_lvl = image->pixels[(chunk_pos.z + zPos) * image->width + (chunk_pos.x + xPos)];
                y_lvl = (y * 16) + yPos;
                if (y_lvl <= greyscale_lvl) {
                    block_set++;
                    block_count++;
                } else {
                    if (y_lvl <= water_level) {
                        block_set += 2;
                        block_count += DIRT_MAX + 1;  // min value required so a chunk with any water is always larger
                                                      // than a chunk of all dirt
                        sky_light_set += 0xF - MIN((water_level - y_lvl), 0xF);
                    } else {
                        sky_light_set += 0xF;
                    }
                }
                if (xPos != 0) {
                    block_set <<= 4;
                    sky_light_set <<= 4;
                }
            }
            block_data[yPos * 16 + zPos] = block_set;
            sky_light[yPos * 16 + zPos] = sky_light_set;
        }
    }
    section_write_count += write_nbt_list(&section_buffer[section_write_count], STR("palette"), NBT_TAG_Compound,
                                          (block_count != 0) + (block_count != DIRT_MAX) + (block_count > DIRT_MAX));
    if (block_count > DIRT_MAX) {
        section_write_count +=
            write_nbt_string(&section_buffer[section_write_count], STR("Name"), STR("minecraft:air"));
        section_write_count += write_nbt_end(&section_buffer[section_write_count]);
        section_write_count +=
            write_nbt_string(&section_buffer[section_write_count], STR("Name"), STR("minecraft:dirt"));
        section_write_count += write_nbt_end(&section_buffer[section_write_count]);
        section_write_count +=
            write_nbt_string(&section_buffer[section_write_count], STR("Name"), STR("minecraft:water"));
        section_write_count += write_nbt_end(&section_buffer[section_write_count]);
    } else {
        if (block_count != DIRT_MAX) {
            section_write_count +=
                write_nbt_string(&section_buffer[section_write_count], STR("Name"), STR("minecraft:air"));
            section_write_count += write_nbt_end(&section_buffer[section_write_count]);
        }
        if (block_count != 0) {
            section_write_count +=
                write_nbt_string(&section_buffer[section_write_count], STR("Name"), STR("minecraft:dirt"));
            section_write_count += write_nbt_end(&section_buffer[section_write_count]);
        }
    }
    if (block_count != 0 && block_count != DIRT_MAX) {
        section_write_count += write_nbt_long_array(&section_buffer[section_write_count], STR("data"), block_data,
                                                    ARRAY_COUNT(block_data));
    }
    section_write_count += write_nbt_end(&section_buffer[section_write_count]);  // block_states
    section_write_count += write_nbt_byte_array(&section_buffer[section_write_count], STR("SkyLight"),
                                                (char *)sky_light, sizeof(sky_light));
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
    write_count += write_nbt_list(&chunk_buffer[write_count], STR("sections"), NBT_TAG_Compound, 24);
    for (i32 y = -4; y < 20; y++) {
        write_count += write_section(&chunk_buffer[write_count], image, x, z, y);
    }
    write_count += write_nbt_long(&chunk_buffer[write_count], STR("LastUpdate"), 0);
    write_count += write_nbt_end(&chunk_buffer[write_count]);  // ""
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
    if (verbose_flag == 1) printf("Generating Region: (%d, %d)\n", x, z);
    i32 chunk_size, big_e;
    i32 chunk_count = 0;
    i32 sector_counter = 2;
    i64 file_pos;
    u64 destLen;
    location loc;
    timestamp t;
    t.time = 1000 * (unsigned)time(NULL);
    char *locations_header = region_file_buffer;
    char *timestamps_header = region_file_buffer + 0x1000;
    char chunk_buffer[MEGABYTES(1)];
    i32 chunk_x, chunk_z;
    for (i32 i = 0; i < 32; i++) {
        for (i32 j = 0; j < 32; j++) {
            file_pos = sector_counter * 0x1000;
            memset(chunk_buffer, 0x00, MEGABYTES(1));
            chunk_x = x * 32 + i;
            chunk_z = z * 32 + j;
            chunk_size = write_chunk(chunk_buffer, image, chunk_x, chunk_z);
            destLen = libdeflate_zlib_compress(compressor, (chunk_buffer), chunk_size,
                                               (region_file_buffer + file_pos + 5), MEGABYTES(1));
            big_e = bswap_32((i32)destLen - 1);
            memcpy(region_file_buffer + file_pos, &big_e, sizeof(big_e));
            region_file_buffer[file_pos + sizeof(big_e)] = 0x02;  // compression type
            loc.offset = (bswap_32(sector_counter) >> 8);
            loc.sector_count = (destLen / 4096) + 1;
            sector_counter += loc.sector_count;
            memcpy(&locations_header[4 * ((chunk_x & 31) + (chunk_z & 31) * 32)], &loc, sizeof(loc));
            memcpy(&timestamps_header[4 * ((chunk_x & 31) + (chunk_z & 31) * 32)], &t, sizeof(t));
            chunk_count++;
        }
    }
    libdeflate_free_compressor(compressor);
    return sector_counter * 4096;
}

// *****************************************************************
//                       Level Data Gen
// *****************************************************************

i32 gen_level_data(char *level_data_dest) {
    char level_data_buffer[MEGABYTES(2)];
    struct libdeflate_compressor *compressor = libdeflate_alloc_compressor(1);
    i32 data_size = 0;
    data_size += write_nbt_compound(&level_data_buffer[data_size], STR(""));
    data_size += write_nbt_compound(&level_data_buffer[data_size], STR("Data"));
    data_size += write_nbt_compound(&level_data_buffer[data_size], STR("Player"));
    data_size += write_nbt_int(&level_data_buffer[data_size], STR("SpawnX"), 0);
    data_size += write_nbt_int(&level_data_buffer[data_size], STR("SpawnY"), 100);
    data_size += write_nbt_int(&level_data_buffer[data_size], STR("SpawnZ"), 0);
    data_size += write_nbt_list(&level_data_buffer[data_size], STR("Pos"), NBT_TAG_Double, 3);
    data_size += write_nbt_list_double(&level_data_buffer[data_size], 0.5);
    data_size += write_nbt_list_double(&level_data_buffer[data_size], 100);
    data_size += write_nbt_list_double(&level_data_buffer[data_size], 0.5);
    data_size += write_nbt_byte(&level_data_buffer[data_size], STR("SpawnForced"), 1);
    data_size += write_nbt_end(&level_data_buffer[data_size]);  // Player
    data_size += write_nbt_compound(&level_data_buffer[data_size], STR("Version"));
    data_size += write_nbt_byte(&level_data_buffer[data_size], STR("Snapshot"), 0);
    data_size += write_nbt_int(&level_data_buffer[data_size], STR("Id"), 3105);
    data_size += write_nbt_string(&level_data_buffer[data_size], STR("Name"), STR("map2mc"));
    data_size += write_nbt_end(&level_data_buffer[data_size]);  // Version
    data_size += write_nbt_compound(&level_data_buffer[data_size], STR("WorldGenSettings"));
    data_size += write_nbt_long(&level_data_buffer[data_size], STR("seed"), 27594263);
    data_size += write_nbt_byte(&level_data_buffer[data_size], STR("generate_features"), 0);
    data_size += write_nbt_compound(&level_data_buffer[data_size], STR("dimensions"));
    data_size += write_nbt_compound(&level_data_buffer[data_size], STR("minecraft:overworld"));
    data_size += write_nbt_compound(&level_data_buffer[data_size], STR("generator"));
    data_size += write_nbt_string(&level_data_buffer[data_size], STR("settings"), STR("minecraft:large_biomes"));
    data_size += write_nbt_long(&level_data_buffer[data_size], STR("seed"), 27594263);
    data_size += write_nbt_compound(&level_data_buffer[data_size], STR("biome_source"));
    data_size += write_nbt_string(&level_data_buffer[data_size], STR("preset"), STR("minecraft:overworld"));
    data_size += write_nbt_string(&level_data_buffer[data_size], STR("type"), STR("minecraft:multi_noise"));
    data_size += write_nbt_end(&level_data_buffer[data_size]);  // biome_source
    data_size += write_nbt_string(&level_data_buffer[data_size], STR("type"), STR("minecraft:noise"));
    data_size += write_nbt_end(&level_data_buffer[data_size]);  // generator
    data_size += write_nbt_string(&level_data_buffer[data_size], STR("type"), STR("minecraft:overworld"));
    data_size += write_nbt_end(&level_data_buffer[data_size]);  // minecraft:overworld
    data_size += write_nbt_compound(&level_data_buffer[data_size], STR("minecraft:the_nether"));
    data_size += write_nbt_compound(&level_data_buffer[data_size], STR("generator"));
    data_size += write_nbt_string(&level_data_buffer[data_size], STR("settings"), STR("minecraft:nether"));
    data_size += write_nbt_long(&level_data_buffer[data_size], STR("seed"), 27594263);
    data_size += write_nbt_compound(&level_data_buffer[data_size], STR("biome_source"));
    data_size += write_nbt_string(&level_data_buffer[data_size], STR("preset"), STR("minecraft:nether"));
    data_size += write_nbt_string(&level_data_buffer[data_size], STR("type"), STR("minecraft:multi_noise"));
    data_size += write_nbt_end(&level_data_buffer[data_size]);  // biome_source
    data_size += write_nbt_string(&level_data_buffer[data_size], STR("type"), STR("minecraft:noise"));
    data_size += write_nbt_end(&level_data_buffer[data_size]);  // generator
    data_size += write_nbt_string(&level_data_buffer[data_size], STR("type"), STR("minecraft:the_nether"));
    data_size += write_nbt_end(&level_data_buffer[data_size]);  // minecraft:the_nether
    data_size += write_nbt_compound(&level_data_buffer[data_size], STR("minecraft:the_end"));
    data_size += write_nbt_compound(&level_data_buffer[data_size], STR("generator"));
    data_size += write_nbt_string(&level_data_buffer[data_size], STR("settings"), STR("minecraft:end"));
    data_size += write_nbt_long(&level_data_buffer[data_size], STR("seed"), 27594263);
    data_size += write_nbt_compound(&level_data_buffer[data_size], STR("biome_source"));
    data_size += write_nbt_long(&level_data_buffer[data_size], STR("seed"), 27594263);
    data_size += write_nbt_string(&level_data_buffer[data_size], STR("type"), STR("minecraft:the_end"));
    data_size += write_nbt_end(&level_data_buffer[data_size]);  // biome_source
    data_size += write_nbt_string(&level_data_buffer[data_size], STR("type"), STR("minecraft:noise"));
    data_size += write_nbt_end(&level_data_buffer[data_size]);  // generator
    data_size += write_nbt_string(&level_data_buffer[data_size], STR("type"), STR("minecraft:the_end"));
    data_size += write_nbt_end(&level_data_buffer[data_size]);  // minecraft:the_end
    data_size += write_nbt_end(&level_data_buffer[data_size]);  // dimensions
    data_size += write_nbt_end(&level_data_buffer[data_size]);  // WorldGenSettings
    data_size += write_nbt_int(&level_data_buffer[data_size], STR("SpawnX"), 0);
    data_size += write_nbt_int(&level_data_buffer[data_size], STR("SpawnY"), 69);
    data_size += write_nbt_int(&level_data_buffer[data_size], STR("SpawnZ"), 0);
    data_size += write_nbt_byte(&level_data_buffer[data_size], STR("hardcore"), 0);
    data_size += write_nbt_byte(&level_data_buffer[data_size], STR("Difficulty"), 0);
    data_size += write_nbt_long(&level_data_buffer[data_size], STR("BorderSizeLerpTime"), 0);
    data_size += write_nbt_int(&level_data_buffer[data_size], STR("GameType"), 1);
    data_size += write_nbt_double(&level_data_buffer[data_size], STR("BorderCenterX"), 0);
    data_size += write_nbt_double(&level_data_buffer[data_size], STR("BorderCenterZ"), 0);
    data_size += write_nbt_double(&level_data_buffer[data_size], STR("BorderSafeZone"), 5);
    data_size += write_nbt_double(&level_data_buffer[data_size], STR("BorderWarningBlocks"), 5);
    data_size += write_nbt_double(&level_data_buffer[data_size], STR("BorderDamagePerBlock"), 0.2);
    data_size += write_nbt_double(&level_data_buffer[data_size], STR("BorderWarningTime"), 15);
    data_size += write_nbt_double(&level_data_buffer[data_size], STR("BorderSizeLerpTarget"), 60000000);
    data_size += write_nbt_double(&level_data_buffer[data_size], STR("BorderSize"), 60000000);
    data_size += write_nbt_int(&level_data_buffer[data_size], STR("version"), 19133);
    data_size += write_nbt_long(&level_data_buffer[data_size], STR("LastPlayed"), 1000 * time(NULL));
    data_size += write_nbt_string(&level_data_buffer[data_size], STR("LevelName"), STR("Test World"));
    data_size += write_nbt_int(&level_data_buffer[data_size], STR("DataVersion"), 3105);
    data_size += write_nbt_byte(&level_data_buffer[data_size], STR("allowCommands"), 1);
    data_size += write_nbt_int(&level_data_buffer[data_size], STR("MapHeight"), 320);
    data_size += write_nbt_end(&level_data_buffer[data_size]);  // Data
    data_size += write_nbt_end(&level_data_buffer[data_size]);  // ""
    i32 compressed_size = libdeflate_gzip_compress(compressor, level_data_buffer, data_size, level_data_dest, 2048);
    libdeflate_free_compressor(compressor);
    return compressed_size;
}
