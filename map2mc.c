#include "map2mc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "benlib.h"
#include "byteswap.h"
#include "endian.h"
#include "libdeflate.h"

#define DIRT_MAX (16 * 16 * 16)

#define ENDIAN_CORRECT_16(var, endianness) (endianness == LITTLE_ENDIAN) ? var : bswap_16(var)
#define ENDIAN_CORRECT_64(var, endianness) (endianness == LITTLE_ENDIAN) ? var : bswap_64(var)

#define IMG_TO_MINECRAFT_RATIO (1536.0 / (f64)U16_MAX)

extern int verbose_flag, water_level;

// *****************************************************************
//                         Image Processing
// *****************************************************************

#pragma GCC diagnostic ignored "-Wmultichar"

typedef enum : short {
    ImageWidth = 0x100,
    ImageLength = 0x101,
    BitsPerSample = 0x102,
    Compression = 0x103,
    PhotometricInterpretation = 0x106,
    StripOffsets = 0x111,
    RowsPerStrip = 0x116,
    StripByteCounts = 0x117,
    XResolution = 0x11A,
    YResolution = 0x11B,
    ResolutionUnit = 0x128,
} TIFF_entry_tag;

typedef enum : short {
    BYTE = 1,
    ASCII,
    SHORT,
    LONG,
    RATIONAL,
    SBYTE,
    UNDEFINED,
    SSHORT,
    SLONG,
    SRATIONAL,
    FLOAT,
    DOUBLE,
    LONG8 = 16,
    SLONG8 = 17,
    IFD8 = 18,
} TIFF_entry_type;

#pragma pack(push, 1)
typedef struct {
    u16 byte_order;    // Byte order ("II" or "MM")
    u16 magic_number;  // Magic Number, should be 0x002B
    u16 offset_size;   // Size of offset in bytes (0x0008)
    u16 constant;      // should be 0x0000
    u64 ifd_offset;    // offset to the first ifd
} BigTIFF_file_header;

// IFD Entry
typedef struct {
    TIFF_entry_tag tag_id;  // The tag ID
    u16 type;               // The type of the value
    u64 count;              // The number of values
    u64 value;              // Offset or value depending on the type
} BigTIFF_ifd_entry;

// TIFF IFD Header
typedef struct {
    u64 num_entries;  // Number of entires in the IFD
} BigTIFF_ifd_header;
#pragma pack(pop)

static void *handle_entry(Arena *arena, image_data *data, BigTIFF_ifd_entry *entry, FILE *img) {
    void *mem;
    usize size;
    switch (entry->tag_id) {
        case ImageWidth:
            data->width = entry->value;
            break;
        case ImageLength:
            data->height = entry->value;
            break;
        case BitsPerSample:
            if (entry->value != 16) {
                printf("Big TIFF image should be 16 bit greyscale\n");
                // should close the image, but Im lazy and I like my nicer function call api
                exit(EXIT_FAILURE);
            }
            break;
        case Compression:
            if (entry->value != 1) {
                printf("Big TIFF image should be uncompressed\n");
                // should close the image, but Im lazy and I like my nicer function call api
                exit(EXIT_FAILURE);
            }
            break;
        case PhotometricInterpretation:
            if (entry->value != 1) {
                printf("Big TIFF image should have PhotometricInterpretation of 1 (black is 0)\n");
                // should close the image, but Im lazy and I like my nicer function call api
                exit(EXIT_FAILURE);
            }
            break;
        case StripOffsets:
            size = (entry->type == SHORT) * 2 | (entry->type == LONG) * 4 | (entry->type == LONG8) * 8;
            fseek(img, entry->value, SEEK_SET);
            mem = ArenaAlloc(arena, size * entry->count);
            fread(mem, size, entry->count, img);
            return mem;
            break;
        case RowsPerStrip:
            break;
        case StripByteCounts:
            size = (entry->type == SHORT) * 2 | (entry->type == LONG) * 4 | (entry->type == LONG8) * 8;
            fseek(img, entry->value, SEEK_SET);
            mem = ArenaAlloc(arena, size * entry->count);
            fread(mem, size, entry->count, img);
            return mem;
            break;
        case XResolution:
            break;
        case YResolution:
            break;
        case ResolutionUnit:
            break;
        default:
            break;
    }
    return NULL;
}

i32 load_height_map(const char *filepath, unsigned char *buffer, image_data *data) {
    Arena local_arena;
    ArenaInitVM(&local_arena, MEGABYTES(10));
    FILE *img = fopen(filepath, "rb");
    if (!img) {
        printf("Error loading image %s\n", filepath);
        fclose(img);
        exit(EXIT_FAILURE);
    }

    BigTIFF_file_header file_header;
    fread(&file_header, sizeof(BigTIFF_file_header), 1, img);

    u32 endianness = 0;
    if (file_header.byte_order == 'II') {
        // Little Endian
        endianness = LITTLE_ENDIAN;
    } else if (file_header.byte_order == 'MM') {
        // Big Endian
        endianness = BIG_ENDIAN;
        printf("Currently unsupported, cope\n");
        fclose(img);
        exit(EXIT_FAILURE);
    } else {
        printf("Invalid TIFF byte order, %X\n", file_header.byte_order);
        fclose(img);
        exit(EXIT_FAILURE);
    }

    u16 magic_number = ENDIAN_CORRECT_16(file_header.magic_number, endianness);
    if (magic_number != 0x002B) {
        printf("Invalid BigTIFF file (incorrect magic number)\n");
        fclose(img);
        exit(EXIT_FAILURE);
    }

    if (file_header.constant != 0x0000) {
        printf("Invalid BigTIFF file (bytes 6-7 must be zero)\n");
        fclose(img);
        exit(EXIT_FAILURE);
    }

    u16 off_size = ENDIAN_CORRECT_16(file_header.offset_size, endianness);
    if (off_size != 0x0008) {
        printf("Invalid BigTIFF file (invalid offset size)\n");
        fclose(img);
        exit(EXIT_FAILURE);
    }

    fseek(img, file_header.ifd_offset, SEEK_SET);
    BigTIFF_ifd_header ifd_header;
    fread(&ifd_header.num_entries, sizeof(u64), 1, img);

    u64 num_entries = ENDIAN_CORRECT_64(ifd_header.num_entries, endianness);
    BigTIFF_ifd_entry entries[num_entries];
    fread(entries, sizeof(BigTIFF_ifd_entry), num_entries, img);

    BigTIFF_ifd_entry cur_entry;
    void *result;
    void *offsets;
    void *counts;
    TIFF_entry_type offset_type;
    u64 offset_count;
    TIFF_entry_type count_type;
    for (int i = 0; i < num_entries; i++) {
        cur_entry = entries[i];
        result = handle_entry(&local_arena, data, &cur_entry, img);
        if (result) {
            if (cur_entry.tag_id == StripOffsets) {
                offset_type = cur_entry.type;
                offset_count = cur_entry.count;
                offsets = result;
            } else if (cur_entry.tag_id == StripByteCounts) {
                count_type = cur_entry.type;
                counts = result;
            }
        }
    }

    u64 pos = 0;
    u64 off_type_size = (offset_type == SHORT) * 2 | (offset_type == LONG) * 4 | (offset_type == LONG8) * 8;
    u64 count_type_size = (count_type == SHORT) * 2 | (count_type == LONG) * 4 | (count_type == LONG8) * 8;
    u64 off, byte_count;
    for (u64 i = 0; i < offset_count; i++) {
        off = 0, byte_count = 0;
        memcpy(&off, offsets + i * off_type_size, off_type_size);
        memcpy(&byte_count, counts + i * count_type_size, count_type_size);
        fseek(img, off, SEEK_SET);
        fread(buffer + pos, byte_count, i, img);
        pos += byte_count;
    }

    fclose(img);

    data->pixels = (void *)buffer;
    data->origin.x = data->width / 2;
    data->origin.z = data->height / 2;
    ArenaFreeVM(&local_arena);
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
    i32 greyscale_lvl, y_lvl;
    for (i32 yPos = 0; yPos < 16; yPos++) {
        for (i32 zPos = 0; zPos < 16; zPos++) {
            block_set = 0;
            sky_light_set = 0;
            for (i32 xPos = 15; xPos >= 0; xPos--) {
                greyscale_lvl = image->pixels[(chunk_pos.z + zPos) * image->width + (chunk_pos.x + xPos)];
                greyscale_lvl = (u32)((f64)greyscale_lvl * IMG_TO_MINECRAFT_RATIO);
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
    write_count += write_nbt_list(&chunk_buffer[write_count], STR("sections"), NBT_TAG_Compound, 132);
    for (i32 y = -4; y < 128; y++) {
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

i32 gen_level_data(char *level_data_dest, char *world_name) {
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
    data_size += write_nbt_string(&level_data_buffer[data_size], STR("LevelName"), world_name, strlen(world_name));
    data_size += write_nbt_int(&level_data_buffer[data_size], STR("DataVersion"), 3105);
    data_size += write_nbt_byte(&level_data_buffer[data_size], STR("allowCommands"), 1);
    data_size += write_nbt_int(&level_data_buffer[data_size], STR("MapHeight"), 2048);
    data_size += write_nbt_end(&level_data_buffer[data_size]);  // Data
    data_size += write_nbt_end(&level_data_buffer[data_size]);  // ""
    i32 compressed_size = libdeflate_gzip_compress(compressor, level_data_buffer, data_size, level_data_dest, 2048);
    libdeflate_free_compressor(compressor);
    return compressed_size;
}
