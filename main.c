#include <getopt.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "benlib.h"
#include "map2mc.h"

int verbose_flag = 0;
int water_level = 62;

typedef struct _worker_args {
    i32 x;
    i32 z;
    i32 len_regions_path;
    char *output_dir;
    image_data *i_data;
    struct _worker_args *next_task;
} worker_args;

void *worker(void *arg) {
    worker_args *args = (worker_args *)arg;

    char *region_file_buffer = mmap(0, MEGABYTES(20), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    while (args) {
        i32 size = gen_region(region_file_buffer, args->i_data, args->x, args->z);
        char region_file_path[args->len_regions_path];
        snprintf(region_file_path, args->len_regions_path, "%s/region/r.%d.%d.mca", args->output_dir, args->x, args->z);
        FILE *region_file = fopen(region_file_path, "w");
        fwrite(region_file_buffer, 1, size, region_file);
        fclose(region_file);

        args = args->next_task;
        if (verbose_flag) printf("Wrote region: (%d, %d) \n", args->x, args->z);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    int opt;

    char *input_file = NULL;
    char *output_dir = NULL;
    char *biome = NULL;
    i32 num_threads = 8;

    // CLI parsing
    static struct option long_options[] = {{"verbose", no_argument, NULL, 'v'},
                                           {"help", no_argument, NULL, 'h'},
                                           {"biome", required_argument, NULL, 'b'},
                                           {"threads", required_argument, NULL, 't'},
                                           {"water", required_argument, NULL, 'w'}};

    int option_index;
    while ((opt = getopt_long(argc, argv, "wvobht", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'v':
                verbose_flag = 1;
                break;
            case 'b':
                biome = optarg;
                break;
            case 't':
                num_threads = atoi(optarg);
                break;
            case 'w':
                water_level = atoi(optarg);
                break;
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
                    "      --threads           Number of threads to use\n"
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

    image_data i_data;
    unsigned char *img_buffer = mmap(0, GIGABYTES(32), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

    if (mkdir(output_dir, 0755) == -1) {
        fprintf(stderr, "Error: Could not create output directory.\n");
        exit(EXIT_FAILURE);
    }

    i32 len_outdir_path = strlen(output_dir);
    i32 len_region_dir_path = len_outdir_path + sizeof("region") + 1;
    i32 len_regions_path = len_outdir_path + 30;  // some arbitrary big value
    i32 len_data_path = len_outdir_path + sizeof("level.dat") + 1;

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

    worker_args *thread_tasks[num_threads];

    i32 idx;
    i32 region_width = i_data.width / 16 / 32;
    i32 region_height = i_data.height / 16 / 32;
    i32 num_regions = region_width * region_height;
    worker_args jobs[num_regions];
    struct timeval start_elapsed_time;
    gettimeofday(&start_elapsed_time, NULL);
    for (i32 x = 0; x < region_width; x++) {  // populate worker lists
        for (i32 z = 0; z < region_height; z++) {
            idx = x * region_height + z;
            jobs[idx].x = -(region_width / 2) + x;
            jobs[idx].z = -(region_height / 2) + z;
            jobs[idx].i_data = &i_data;
            jobs[idx].output_dir = output_dir;
            jobs[idx].len_regions_path = len_regions_path;
            jobs[idx].next_task = ((idx + num_threads) < num_regions) ? &jobs[idx + num_threads] : NULL;
        }
    }

    pthread_t threads[num_threads];
    for (i32 i = 0; i < num_threads; i++) {
        thread_tasks[i] = &jobs[i];
        pthread_create(&threads[i], NULL, worker, thread_tasks[i]);
    }

    for (i32 i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    struct timeval end_elapsed_time;
    gettimeofday(&end_elapsed_time, NULL);

    double seconds_elapsed = (double)(end_elapsed_time.tv_sec - start_elapsed_time.tv_sec) +
                             (double)(end_elapsed_time.tv_usec - start_elapsed_time.tv_usec) / 1000000.0;
    printf("Successfully wrote %ld blocks in %lf seconds.", ((long)num_regions * 32 * 32 * 16 * 16 * (-64 + 320)),
           seconds_elapsed);

    return EXIT_SUCCESS;
}
