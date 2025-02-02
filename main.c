#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <string.h>

#include "benlib.h"
#include "map2mc.h"
#include "libdeflate.h"

int verbose_flag = 0;

int main(int argc, char *argv[]) {
    int opt;

    char *input_file = NULL;
    char *output_dir = NULL;
    char *biome = NULL;
    int water_height = 62;
    int num_threads = 8;

    // CLI parsing
    static struct option long_options[] = {{"verbose", no_argument, NULL, 'v'},
                                           {"help", no_argument, NULL, 'h'},
                                           {"biome", required_argument, NULL, 'b'},
                                           {"water", required_argument, NULL, 'w'}};

    int option_index;
    while ((opt = getopt_long(argc, argv, "wjvobh", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'v':
                verbose_flag = 1;
                break;
            case 'b':
                biome = optarg;
                break;
            case 'j':
                num_threads = strtol(optarg, NULL, 10);
            case 'w':
                water_height = strtol(optarg, NULL, 10);
            case 'h':
                printf(
                    "Usage: %s [options] <input_file> <output_directory>\n\n"
                    "Parameters\n"
                    "  <input_file>            Path to the input file (e.g., a BMP file)\n"
                    "  <output_directory>      Path to the output directory where the world files "
                    "will be saved\n\n"
                    "Options:\n"
                    "  -b, --biome <biome>     Specify the biome file to paint biome map)\n"
                    "  -h, --help              Show this help message and exit\n"
                    "  -j                      Number of threads to use\n"
                    "  -v, --verbose           Enable verbose output\n"
                    "  -w, --water             Water height\n",
                    argv[0]);
                exit(EXIT_SUCCESS);
            case '?':
                break;
            default:
                fprintf(stderr, "Usage: %s [options] <input_file> <output_directory>\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    if (optind < argc) {
        input_file = argv[optind];
        optind++;
    }

    if (optind < argc) {
        output_dir = argv[optind];
        optind++;
    }

    if (input_file == NULL) {
        fprintf(stderr, "Error: Input file is required.\n");
        fprintf(stderr, "Usage: %s [options] <input_file> <output_directory>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    if (output_dir == NULL) {
        fprintf(stderr, "Error: Output directory is required.\n");
        fprintf(stderr, "Usage: %s [options] <input_file> <output_directory>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    i32 width, height, channels;
    image_data i_data;
    unsigned char *img_buffer =
        mmap(0, GIGABYTES(32), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

    if (mkdir(output_dir, 0755) == -1) {
        fprintf(stderr, "Error: Could not create output directory.\n");
        exit(EXIT_FAILURE);
    }

    i32 len_outdir_path = strlen(output_dir);
    i32 len_region_dir_path = strlen(output_dir) + sizeof("region") + 1;
    i32 len_regions_path = strlen(output_dir) + 30; // some arbitrary big value
    i32 len_data_path = strlen(output_dir) + sizeof("level.dat") + 1; // some arbitrary big value

    char region_dir[len_region_dir_path];
    snprintf(region_dir, len_region_dir_path, "%s/region", output_dir);
    mkdir(region_dir, 0755);

    char data_path[len_data_path];
    snprintf(data_path, len_data_path, "%s/level.dat", output_dir);
    FILE *data_file = fopen(data_path, "w");
    char level_data[2048];
    i32 level_data_len = gen_level_data(level_data);
    fwrite(level_data, 1, level_data_len, data_file);
    fclose(data_file);

    load_height_map(input_file, img_buffer, &i_data);

    for (i32 x = -1; x < 1; x++) {
        for (i32 z = -1; z < 1; z++) {
            char *region_file_buffer =
                mmap(0, MEGABYTES(20), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
            i32 size = gen_region(region_file_buffer, &i_data, x, z);
            char region_file_path[len_regions_path];
            snprintf(region_file_path, len_regions_path, "%s/region/r.%d.%d.mca", output_dir, x, z);
            FILE *region_file = fopen(region_file_path, "w");
            fwrite(region_file_buffer, 1, size, region_file);
            fclose(region_file);
            munmap(region_file_buffer, MEGABYTES(20));
        }
    }

    munmap(img_buffer, GIGABYTES(32));

    return EXIT_SUCCESS;
}
