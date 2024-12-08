#define FUSE_USE_VERSION 30

#include "wfs.h"
#include "fuse_ops.h"
#include "globals.h"
#include "raid.h"
#include <fcntl.h>
#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static void print_usage(const char *progname) {
  DEBUG_LOG("Usage: %s disk1 [disk2 ...] [FUSE options] mount_point\n",
            progname);
  DEBUG_LOG("Ensure WFS is initialized using mkfs with RAID mode and disks.\n");
}

static int parse_args(int argc, char *argv[], char ***disk_paths,
                      int *num_disks, char ***fuse_args, int *fuse_argc,
                      char **mount_point) {
  DEBUG_LOG("Parsing command-line arguments.");

  *num_disks = 0;
  int i = 1;
  *disk_paths = NULL;

  while (i < argc && strncmp(argv[i], "-", 1) != 0 &&
         access(argv[i], F_OK) == 0) {
    *disk_paths = realloc(*disk_paths, (*num_disks + 1) * sizeof(char *));
    if (*disk_paths == NULL) {
      ERROR_LOG("Error allocating memory for disk paths");
      return -1;
    }
    (*disk_paths)[*num_disks] = argv[i];
    (*num_disks)++;
    i++;
  }

  if (*num_disks < 2) {
    ERROR_LOG("At least two disk must be provided.\n");
    return -1;
  }

  if (i < argc) {
    *mount_point = argv[argc - 1];
    if (access(*mount_point, F_OK) != 0) {
      ERROR_LOG("Invalid mount point: %s\n", *mount_point);
      return -1;
    }
  } else {
    ERROR_LOG("No mount point specified.\n");
    return -1;
  }

  *fuse_args = argv + i;
  *fuse_argc = argc - i;

  DEBUG_LOG("Arguments parsed successfully: %d disks, mount point: %s",
            *num_disks, *mount_point);
  return 0;
}

int load_superblock(void *disk_mmap, struct wfs_sb *sb) {
  if (!disk_mmap || !sb) {
    ERROR_LOG("Invalid arguments to load_superblock.\n");
    return -1;
  }

  memcpy(sb, disk_mmap, sizeof(struct wfs_sb));

  PRINT_SUPERBLOCK(*sb);
  return 0;
}

void print_arguments(int argc, char **argv) {
  DEBUG_LOG("Arguments passed to the program:\n");
  for (int i = 0; i < argc; i++) {
    DEBUG_LOG("  argv[%d]: %s\n", i, argv[i]);
  }
}

int main(int argc, char *argv[]) {
  if (argc < 3) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  char **disk_paths = NULL;
  int num_disks = 0;
  char *mount_point;
  char **fuse_args;
  int fuse_argc;

  if (parse_args(argc, argv, &disk_paths, &num_disks, &fuse_args, &fuse_argc,
                 &mount_point) != 0) {
    print_usage(argv[0]);
    free(disk_paths);
    return EXIT_FAILURE;
  }

  void **disk_mmaps = malloc(num_disks * sizeof(void *));
  size_t *disk_sizes = malloc(num_disks * sizeof(size_t));

  if (!disk_mmaps || !disk_sizes) {
    ERROR_LOG("Error allocating memory for disk mappings or sizes");
    free(disk_paths);
    free(disk_mmaps);
    free(disk_sizes);
    return EXIT_FAILURE;
  }

  int success = 1;
  for (int i = 0; i < num_disks; i++) {

    int fd = open(disk_paths[i], O_RDWR);
    if (fd < 0) {
      ERROR_LOG("Error opening disk file");
      success = 0;
      break;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
      ERROR_LOG("Error getting disk size");
      close(fd);
      success = 0;
      break;
    }

    struct wfs_sb *sb_temp = mmap(NULL, sizeof(struct wfs_sb),
                                  PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (sb_temp == MAP_FAILED) {
      ERROR_LOG("Error mapping temp file");
      close(fd);
      return EXIT_FAILURE;
    }

    int disk_index = sb_temp->disk_index;

    disk_sizes[disk_index] = st.st_size;
    disk_mmaps[disk_index] = mmap(NULL, disk_sizes[disk_index],
                                  PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (disk_mmaps[disk_index] == MAP_FAILED) {
      ERROR_LOG("Error mapping disk file");
      close(fd);
      success = 0;
      break;
    }

    close(fd);
  }

  if (!success) {
    for (int i = 0; i < num_disks; i++) {
      if (disk_mmaps[i]) {
        munmap(disk_mmaps[i], disk_sizes[i]);
      }
    }
    free(disk_mmaps);
    free(disk_sizes);
    free(disk_paths);
    return EXIT_FAILURE;
  }

  if (load_superblock(disk_mmaps[0], &sb) != 0) {
    ERROR_LOG(
        "Error reading superblock. Ensure disks are initialized using mkfs.\n");
    for (int i = 0; i < num_disks; i++) {
      munmap(disk_mmaps[i], disk_sizes[i]);
    }
    free(disk_mmaps);
    free(disk_sizes);
    free(disk_paths);
    return EXIT_FAILURE;
  }

  DEBUG_LOG(
      "Loaded superblock: RAID mode = %d, num_inodes = %ld, num_blocks = %ld\n",
      sb.raid_mode, sb.num_inodes, sb.num_data_blocks);

  initialize_raid(disk_mmaps, num_disks, sb.raid_mode, disk_sizes);
  DEBUG_LOG("Starting FUSE with mount point: %s\n", mount_point);

  print_arguments(fuse_argc, fuse_args);
  int ret = fuse_main(fuse_argc, fuse_args, &ops, NULL);
  return ret;
}
