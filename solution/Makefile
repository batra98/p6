BINS = wfs mkfs
CC = gcc
CFLAGS = -Wall -Werror -pedantic -std=gnu18 -g -D_FILE_OFFSET_BITS=64
FUSE_CFLAGS = `pkg-config fuse --cflags --libs`

MKFS_SRCS = mkfs.c fs_utils.c globals.c  
MKFS_OBJS = $(MKFS_SRCS:.c=.o)

WFS_SRCS = wfs.c raid.c globals.c inode.c fuse_ops.c fuse_file_ops.c fuse_dir_ops.c fuse_meta_ops.c fuse_common.c fs_utils.c data_block.c
WFS_OBJS = $(WFS_SRCS:.c=.o)

.PHONY: all clean

all: $(BINS)

wfs: $(WFS_OBJS)
	$(CC) $(CFLAGS) $(WFS_OBJS) $(FUSE_CFLAGS) -o wfs
mkfs: $(MKFS_OBJS)
	$(CC) $(CFLAGS) $(MKFS_OBJS) -o mkfs

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -rf $(BINS) *.o
