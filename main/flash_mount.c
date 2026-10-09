#include "flash_files.h"
#include <stdio.h>
#include "esp_partition.h"
#include "esp_vfs_fat.h"

static wl_handle_t handle=WL_INVALID_HANDLE;
static bool mounted;
bool flash_files_ready(void) { return mounted; }

bool flash_files_mount(void)
{
    if (mounted) return true;
    const esp_partition_t *p=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_DATA_FAT,"storage");
    if (!p) { printf("Filesystem: storage partition missing\n"); return false; }
    /* Check before wl_mount, which can write its own metadata. Never auto-format
     * a nonblank partition just because FAT mounting fails. */
    bool blank=true;
    uint8_t data[1024];
    for (size_t offset=0; offset<p->size && blank; offset+=sizeof(data)) {
        size_t n=p->size-offset < sizeof(data) ? p->size-offset : sizeof(data);
        if (esp_partition_read(p,offset,data,n)!=ESP_OK) {
            printf("Filesystem: partition read failed\n"); return false;
        }
        for (size_t i=0; i<n; ++i) if (data[i]!=0xff) { blank=false; break; }
    }
    const esp_vfs_fat_mount_config_t config={.format_if_mount_failed=blank,
        .max_files=6, .allocation_unit_size=CONFIG_WL_SECTOR_SIZE, .use_one_fat=false};
    esp_err_t err=esp_vfs_fat_spiflash_mount_rw_wl(FLASH_FILES_ROOT,"storage",&config,&handle);
    if (err!=ESP_OK) {
        printf("Filesystem: mount failed (%s); existing data not formatted\n",esp_err_to_name(err));
        return false;
    }
    mounted=true;
    printf("Filesystem: /flash ready%s\n",blank?" (new partition formatted)":"");
    return true;
}
