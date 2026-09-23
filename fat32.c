#include "fat32.h"

fat32_fs_t g_fs;
u8 g_disk_buf[1024 * 1024];
int g_disk_init = 0;

void fat32_init(fat32_fs_t *fs, u8 *buf, u32 lba_start) {
    fs->disk_buf = buf;
    fat32_boot_sector_t *bs = (fat32_boot_sector_t *)buf;

    ata_read_sector_data(lba_start, buf);

    fs->bs.bpb_sectors_per_cluster  = bs->bpb_sectors_per_cluster;
    fs->bs.bpb_reserved_sector_count = bs->bpb_reserved_sector_count;
    fs->bs.bpb_number_of_fats       = bs->bpb_number_of_fats;
    fs->bs.bpb_total_sectors_32     = bs->bpb_total_sectors_32;
    fs->bs.bpb_sectors_per_fat      = bs->bpb_sectors_per_fat;
    fs->bs.bpb_fat_start_sector     = bs->bpb_reserved_sector_count;
    fs->bs.root_dir_start           = bs->bpb_reserved_sector_count + bs->bpb_number_of_fats * bs->bpb_sectors_per_fat;
    fs->bs.data_start               = fs->bs.root_dir_start + 32 * 512 / (bs->bpb_sectors_per_cluster * 512);
    fs->bs.total_clusters           = (bs->bpb_total_sectors_32 - fs->bs.data_start) / bs->bpb_sectors_per_cluster;
}

static u32 fat32_cluster_to_sector(fat32_fs_t *fs, u32 cluster) {
    return fs->bs.data_start + (cluster - 2) * fs->bs.bpb_sectors_per_cluster;
}

static u32 fat32_get_fat_entry(fat32_fs_t *fs, u32 cluster) {
    u32 fat_sector = fs->bs.bpb_fat_start_sector + (cluster * 4) / 512;
    u32 offset = (cluster * 4) % 512;
    u8 buf[512];
    ata_read_sector_data(fat_sector, buf);
    return *(u32 *)(buf + offset);
}

int fat32_read_cluster(fat32_fs_t *fs, u32 cluster, u8 *buf) {
    u32 sector = fat32_cluster_to_sector(fs, cluster);
    u32 count = fs->bs.bpb_sectors_per_cluster;
    for (u32 i = 0; i < count; i++) {
        ata_read_sector_data(sector + i, buf + i * 512);
    }
    return 0;
}

int fat32_list_dir(fat32_fs_t *fs, u32 cluster, int (*cb)(const char *, u32, int)) {
    u8 buf[512 * 32];
    fat32_read_cluster(fs, cluster, buf);

    fat32_dir_entry_t *entries = (fat32_dir_entry_t *)buf;
    for (u32 i = 0; i < 256; i++) {
        if (entries[i].name[0] == 0x00) break;
        if (entries[i].name[0] == 0xE5) continue;

        char name[13] = {0};
        for (int j = 0; j < 8 && entries[i].name[j] != ' '; j++)
            name[j] = entries[i].name[j];
        if (entries[i].ext[0] != ' ' && entries[i].ext[0] != 0) {
            strcat(name, ".");
            for (int j = 0; j < 3 && entries[i].ext[j] != ' '; j++)
                strcat(name, &entries[i].ext[j]);
        }

        int is_dir = entries[i].attrib & ATTRIB_DIRECTORY;
        if (cb(name, entries[i].file_size, is_dir) != 0) break;
    }
    return 0;
}

int fat32_read_file(fat32_fs_t *fs, const char *path, u8 *buf, u32 max_size) {
    u32 cluster = fat32_get_cluster(fs, path);
    if (cluster == 0) return -1;

    u32 total = 0;
    u32 cur = cluster;
    while (cur >= 2 && cur < 0x0FFFFFF8 && total < max_size) {
        u8 cluster_buf[512 * 32];
        fat32_read_cluster(fs, cur, cluster_buf);
        u32 to_copy = (max_size - total < 512 * 32) ? (max_size - total) : (512 * 32);
        memcpy(buf + total, cluster_buf, to_copy);
        total += to_copy;
        cur = fat32_get_fat_entry(fs, cur);
    }
    return total;
}

u32 fat32_get_cluster(fat32_fs_t *fs, const char *path) {
    u8 buf[512 * 32];
    fat32_read_cluster(fs, 2, buf);
    fat32_dir_entry_t *entries = (fat32_dir_entry_t *)buf;
    for (u32 i = 0; i < 256; i++) {
        if (entries[i].name[0] == 0x00) break;
        if (entries[i].name[0] == 0xE5) continue;

        char name[13] = {0};
        for (int j = 0; j < 8 && entries[i].name[j] != ' '; j++)
            name[j] = entries[i].name[j];
        if (entries[i].ext[0] != ' ') {
            strcat(name, ".");
            for (int j = 0; j < 3 && entries[i].ext[j] != ' '; j++)
                strcat(name, &entries[i].ext[j]);
        }

        if (strcmp(name, path) == 0) {
            return (u32)((entries[i].first_cluster_high << 16) | entries[i].first_cluster_low);
        }
    }
    return 0;
}

u32 fat32_get_root_cluster(fat32_fs_t *fs) {
    return fat32_get_cluster(fs, "");
}
