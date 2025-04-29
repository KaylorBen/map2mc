#include "bigtiff.h"

#include <string.h>

#include "endian.h"

#define ENDIAN_CORRECT_16(var, endianness) (endianness == LITTLE_ENDIAN) ? var : bswap_16(var)
#define ENDIAN_CORRECT_64(var, endianness) (endianness == LITTLE_ENDIAN) ? var : bswap_64(var)

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
