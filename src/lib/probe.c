/*
 * This file is part of clr-boot-manager.
 *
 * Copyright © 2016-2018 Intel Corporation
 *
 * clr-boot-manager is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 */

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#include <btrfsutil.h>

#include "blkid_stub.h"
#include "files.h"
#include "log.h"
#include "probe.h"
#include "system_stub.h"
#include "util.h"

/**
 * Convert a sysfs dev file to the target device path
 */
static char *cbm_dev_file_to_devpath(const char *devfile)
{
        int fd = 0;
        unsigned int dev_major, dev_minor = 0;
        char read_buf[PATH_MAX] = { 0 };
        ssize_t size = -1;

        /* Read the dev file */
        fd = open(devfile, O_RDONLY | O_NOCTTY | O_CLOEXEC);

        if (fd < 0) {
                return NULL;
        }

        size = read(fd, read_buf, sizeof(read_buf));
        close(fd);
        if (size < 1) {
                return NULL;
        }

        if (sscanf(read_buf, "%u:%u", &dev_major, &dev_minor) != 2) {
                return NULL;
        }

        return cbm_system_devnode_to_devpath(makedev(dev_major, dev_minor));
}

static char *cbm_get_luks_uuid(const char *part)
{
        autofree(char) *npath = NULL;
        autofree(char) *dpath = NULL;
        glob_t glo = { 0 };
        blkid_probe blk_probe = NULL;
        char *ret = NULL;
        const char *value = NULL;
        const char *sys = cbm_system_get_sysfs_path();

        /* i.e. /sys/block/dm-1/slaves/dm-0/slaves/sdb1/dev
         * or /sys/block/dm-1/slaves/sdb1/dev
         */
        npath = string_printf("%s/block/%s/slaves/*{,/slaves/*}/dev", sys, part);

        glob(npath, GLOB_DOOFFS | GLOB_BRACE, NULL, &glo);

        if (glo.gl_pathc < 1) {
                globfree(&glo);
                return NULL;
        }

        dpath = cbm_dev_file_to_devpath(glo.gl_pathv[0]);
        globfree(&glo);
        if (!dpath) {
                return NULL;
        }

        blk_probe = cbm_blkid_new_probe_from_filename(dpath);
        if (!blk_probe) {
                LOG_ERROR("Unable to probe %s", dpath);
                return NULL;
        }

        cbm_blkid_probe_enable_superblocks(blk_probe, 1);
        cbm_blkid_probe_set_superblocks_flags(blk_probe, BLKID_SUBLKS_TYPE | BLKID_SUBLKS_UUID);
        cbm_blkid_probe_enable_partitions(blk_probe, 1);
        cbm_blkid_probe_set_partitions_flags(blk_probe, BLKID_PARTS_ENTRY_DETAILS);

        if (cbm_blkid_do_safeprobe(blk_probe) != 0) {
                LOG_ERROR("Error probing filesystem: %s", strerror(errno));
                goto clean;
        }

        /* Grab the type */
        if (cbm_blkid_probe_lookup_value(blk_probe, "TYPE", &value, NULL) != 0) {
                LOG_ERROR("Error determining type of device %s: %s\n", dpath, strerror(errno));
                goto clean;
        }

        /* Ensure that this parent disk really is LUKS */
        if (!streq(value, "crypto_LUKS")) {
                goto clean;
        }

        if (cbm_blkid_probe_lookup_value(blk_probe, "UUID", &value, NULL) == 0) {
                ret = strdup(value);
                if (!ret) {
                        DECLARE_OOM();
                        goto clean;
                }
        }

clean:
        cbm_blkid_free_probe(blk_probe);
        return ret;
}

/**
 * Determine whether the probe lives on a GPT disk or not,
 * which is the only instance in which we'll use PartUUID
 */
static bool cbm_probe_is_gpt(const char *path)
{
        autofree(char) *parent_disk = NULL;
        blkid_probe probe = NULL;
        blkid_partlist parts = NULL;
        blkid_parttable table = NULL;
        bool ret = false;
        const char *table_type = NULL;

        /* Could be a weird image type or --path into chroot */
        parent_disk = get_parent_disk((char *)path);
        if (!parent_disk) {
                return false;
        }

        probe = cbm_blkid_new_probe_from_filename(parent_disk);
        if (!probe) {
                LOG_ERROR("Unable to blkid probe %s", parent_disk);
                return NULL;
        }

        cbm_blkid_probe_enable_superblocks(probe, 1);
        cbm_blkid_probe_set_superblocks_flags(probe, BLKID_SUBLKS_TYPE);
        cbm_blkid_probe_enable_partitions(probe, 1);
        cbm_blkid_probe_set_partitions_flags(probe, BLKID_PARTS_ENTRY_DETAILS);

        if (cbm_blkid_do_safeprobe(probe) != 0) {
                LOG_ERROR("Error probing filesystem of %s: %s", parent_disk, strerror(errno));
                goto clean;
        }

        parts = cbm_blkid_probe_get_partitions(probe);
        if (cbm_blkid_partlist_numof_partitions(parts) <= 0) {
                /* No partitions */
                goto clean;
        }

        /* Grab the partition table */
        table = cbm_blkid_partlist_get_table(parts);
        if (!table) {
                LOG_ERROR("Unable to discover partitiojn table for %s: %s",
                          parent_disk,
                          strerror(errno));
                goto clean;
        }

        /* Determine the partition table type. We only care if its GPT. */
        table_type = cbm_blkid_parttable_get_type(table);
        if (table_type && streq(table_type, "gpt")) {
                ret = true;
        }

clean:
        cbm_blkid_free_probe(probe);
        errno = 0;
        return ret;
}

CbmDeviceProbe *cbm_probe_path(const char *path)
{
        CbmDeviceProbe probe = { 0 };
        CbmDeviceProbe *ret = NULL;
        autofree(char) *devnode = NULL;
        struct stat st = { 0 };
        blkid_probe blk_probe = NULL;
        const char *value = NULL;
        char *basenom = NULL;

        if (stat(path, &st) != 0) {
                LOG_ERROR("Path does not exist: %s", path);
                return NULL;
        }

        devnode = cbm_system_get_device_for_mountpoint(path);
        if (!devnode) {
                LOG_ERROR("No device for path: %s", path);
                DECLARE_OOM();
                return NULL;
        }

        blk_probe = cbm_blkid_new_probe_from_filename(devnode);
        if (!blk_probe) {
                fprintf(stderr, "Unable to probe device %s", devnode);
                return NULL;
        }

        cbm_blkid_probe_enable_superblocks(blk_probe, 1);
        cbm_blkid_probe_set_superblocks_flags(blk_probe, BLKID_SUBLKS_TYPE | BLKID_SUBLKS_UUID);
        cbm_blkid_probe_enable_partitions(blk_probe, 1);
        cbm_blkid_probe_set_partitions_flags(blk_probe, BLKID_PARTS_ENTRY_DETAILS);

        if (cbm_blkid_do_safeprobe(blk_probe) != 0) {
                LOG_ERROR("Error probing filesystem: %s", strerror(errno));
                goto clean;
        }

        if (cbm_blkid_probe_lookup_value(blk_probe, "PART_ENTRY_UUID", &value, NULL) == 0) {
                probe.part_uuid = strdup(value);
                if (!probe.part_uuid) {
                        DECLARE_OOM();
                        goto clean;
                }
        }

        if (cbm_blkid_probe_lookup_value(blk_probe, "UUID", &value, NULL) == 0) {
                probe.uuid = strdup(value);
                if (!probe.uuid) {
                        DECLARE_OOM();
                        goto clean;
                }
        }

        /* If the device isn't GPT, clear out the the PartUUID */
        probe.gpt = cbm_probe_is_gpt(path);
        if (!probe.gpt && probe.part_uuid) {
                free(probe.part_uuid);
                probe.part_uuid = NULL;
        }

        /* Now check we have at least one UUID value */
        if (!probe.part_uuid && !probe.uuid) {
                LOG_ERROR("Unable to find UUID for %s: %s", devnode, strerror(errno));
        }

        /* Check if its a Btrfs device */
        if (btrfs_util_is_subvolume(path) == BTRFS_UTIL_OK) {
                LOG_DEBUG("Root device is a Btrfs subvolume");
                enum btrfs_util_error err = btrfs_util_subvolume_path(path, 0, &probe.btrfs_sub);
                if (err != BTRFS_UTIL_OK) {
                        LOG_ERROR("Failed to get subvolume of Btrfs filesystem %s: %s",
                                  path, btrfs_util_strerror(err));
                }
        }

        /* Check if its a software raid device */
        basenom = basename(devnode);
        if (strncmp(basenom, "md", 2) == 0) {
                LOG_DEBUG("Root device exists on Linux software RAID configuration");
                /* Need to use the UUID only */
                if (probe.part_uuid) {
                        free(probe.part_uuid);
                        probe.part_uuid = NULL;
                }
        }

        /* Lastly check if its a device-mapper device */
        if (strncmp(basenom, "dm-", 3) == 0) {
                LOG_DEBUG("Root device exists on device-mapper configuration");
                probe.luks_uuid = cbm_get_luks_uuid(basenom);
        }

        ret = calloc(1, sizeof(CbmDeviceProbe));
        if (!ret) {
                DECLARE_OOM();
                return NULL;
        }
        *ret = probe;

clean:
        cbm_blkid_free_probe(blk_probe);
        return ret;
}

void cbm_probe_free(CbmDeviceProbe *probe)
{
        if (!probe) {
                return;
        }

        free(probe->uuid);
        free(probe->part_uuid);
        free(probe->luks_uuid);
        free(probe);
        return;
}

/*
 * Editor modelines  -  https://www.wireshark.org/tools/modelines.html
 *
 * Local variables:
 * c-basic-offset: 8
 * tab-width: 8
 * indent-tabs-mode: nil
 * End:
 *
 * vi: set shiftwidth=8 tabstop=8 expandtab:
 * :indentSize=8:tabSize=8:noTabs=true:
 */
