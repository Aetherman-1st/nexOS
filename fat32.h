#ifndef FAT32_H
#define FAT32_H

#include "io.h"
#include "ata.h"

#define SECTORS_PER_CLUSTER 1
#define RESERVED_SECTORS 32
#define ROOT_ENTRY_COUNT 0
#define TOTAL_SECTORS 0
#define SECTORS_PER_FAT 0
#define FAT_START_SECTOR 32
#define ROOT_DIR_START 0xFFFFFFFF
#define DATA_SECTOR_START 0

#define ATTRIB_READ_ONLY   0x01
#define ATTRIB_HIDDEN      0x02
#define ATTRIB_SYSTEM      0x04
#define ATTRIB_VOLUME_ID   0x08
#define ATTRIB_DIRECTORY   0x10
#define ATTRIB_ARCHIVE     0x20

#define MAX_FILES 256
#define MAX_DIR_ENTRIES 256

typedef struct {
    u8 name[8];
    u8 ext[3];
    u8 attrib;
    u8 reserved[10];
    u16 create_time_ms;
    u16 create_time;
    u16 create_date;
    u16 last_access_date;
    u16 first_cluster_high;
    u16 write_time;
    u16 write_date;
    u16 first_cluster_low;
    u32 file_size;
} __attribute__((packed)) fat32_dir_entry_t;

typedef struct {
    u32 bpb_sectors_per_cluster;
    u32 bpb_reserved_sector_count;
    u32 bpb_number_of_fats;
    u32 bpb_root_entry_count;
    u32 bpb_total_sectors_16;
    u32 bpb_total_sectors_32;
    u32 bpb_sectors_per_fat;
    u32 bpb_fat_start_sector;
    u32 bpb_root_start_sector;
    u32 bpb_data_start_sector;
    u32 root_dir_start;
    u32 data_start;
    u32 total_clusters;
} fat32_boot_sector_t;

typedef struct {
    fat32_boot_sector_t bs;
    u8 *disk_buf;
    u32 root_dir_sector;
} fat32_fs_t;

void fat32_init(fat32_fs_t *fs, u8 *buf, u32 lba_start);
int fat32_read_cluster(fat32_fs_t *fs, u32 cluster, u8 *buf);
int fat32_list_dir(fat32_fs_t *fs, u32 cluster, int (*cb)(const char *, u32, int));
int fat32_read_file(fat32_fs_t *fs, const char *path, u8 *buf, u32 max_size);
u32 fat32_get_cluster(fat32_fs_t *fs, const char *path);

extern fat32_fs_t g_fs;
extern u8 g_disk_buf[];
extern int g_disk_init;

#endif
