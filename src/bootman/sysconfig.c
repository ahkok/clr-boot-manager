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
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <blkid/blkid.h>

#include "bootman.h"
#include "bootman_private.h"
#include "files.h"
#include "log.h"
#include "nica/files.h"
#include "system_stub.h"

#define CBM_BOOTVAR_TEST_MODE_VAR "CBM_BOOTVAR_TEST_MODE"

struct FilesystemMap {
        char *name;
        int id;
};

static const struct FilesystemMap _fsmap[] = {
    {
        .id = FSTYPE_VFAT,
        .name = "vfat",
    },
    {
        .id = FSTYPE_EXT2,
        .name = "ext2",
    },
    {
        .id = FSTYPE_EXT3,
        .name = "ext3",
    },
    {
        .id = FSTYPE_EXT4,
        .name = "ext4",
    },
};

void cbm_free_sysconfig(SystemConfig *config)
{
        if (!config) {
                return;
        }
        free(config->prefix);
        free(config->boot_device);
        cbm_probe_free(config->root_device);
        free(config);
}

static const struct FilesystemMap *cbm_find_fstype(const char *fsname)
{
        int i;
        int length = sizeof(_fsmap) / sizeof(struct FilesystemMap);

        for (i = 0; i < length; i++) {
                const struct FilesystemMap *curr = &_fsmap[i];
                if (!strcmp(curr->name, fsname)) {
                        return curr;
                }
        }

        return NULL;
}

static const struct FilesystemMap *cbm_get_fstype(const char *boot_device)
{
        autofree(char) *fsname = NULL;
        const struct FilesystemMap *fs;

        /* Test suite will set the CBM_TEST_FSTYPE env var and inform the wanted fstype */
        fsname = getenv("CBM_TEST_FSTYPE");
        if (fsname) {
                fsname = strdup(fsname);
        } else {
                int rc;
                blkid_probe pr;
                const char *tmp;

                pr = blkid_new_probe_from_filename(boot_device);
                if (!pr) {
                        LOG_ERROR("%s: failed to create a new libblkid probe",
                                  boot_device);
                        exit(EXIT_FAILURE);
                }

                blkid_probe_set_superblocks_flags(pr, BLKID_SUBLKS_TYPE);
                rc = blkid_do_safeprobe(pr);
                if (rc != 0) {
                        LOG_ERROR("%s: blkid_do_safeprobe() failed", boot_device);
                        exit(EXIT_FAILURE);
                }

                rc = blkid_probe_lookup_value(pr, "TYPE", &tmp, NULL);
                if (rc != 0 || tmp == NULL || strlen(tmp) == 0) {
                        LOG_ERROR("%s: blkid_probe_lookup_value() failed", boot_device);
                        exit(EXIT_FAILURE);
                }

                fsname = strdup(tmp);
                if (fsname == NULL) {
                        DECLARE_OOM();
                        exit(EXIT_FAILURE);
                }

                blkid_free_probe(pr);
        }

        fs = cbm_find_fstype(fsname);
        CHECK_DBG_RET_VAL(!fs, NULL, "Failed to find fstype for: %s(%s)", boot_device, fsname);

        return fs;
}

const char *cbm_get_fstype_name(const char *boot_device)
{
        const struct FilesystemMap *fs = cbm_get_fstype(boot_device);
        CHECK_DBG_RET_VAL(!fs, NULL, "Unknown Filesystem of: %s", boot_device);
        return fs->name;
}

int cbm_get_filesystem_cap(const char *boot_device)
{
        const struct FilesystemMap *fs = NULL;

        fs = cbm_get_fstype(boot_device);
        CHECK_DBG_RET_VAL(!fs, 0, "Could not find filesystem map for: %s", boot_device);

        switch (fs->id) {
        case FSTYPE_VFAT:
                return BOOTLOADER_CAP_FATFS;
        case FSTYPE_EXT2:
        case FSTYPE_EXT3:
        case FSTYPE_EXT4:
                return BOOTLOADER_CAP_EXTFS;
        default:
                return 0;
        }
}

SystemConfig *cbm_inspect_root(const char *path)
{
        SystemConfig *c = NULL;
        char *realp = NULL;
        char *rel = NULL;
        char *legacy_boot = NULL;
        char *uefi_boot = NULL;
        bool force_legacy = false;
        bool native_uefi = false;
        char *fw_path = NULL;

        CHECK_ERR_RET_VAL(!path, NULL, "invalid \"path\" value: null");

        realp = realpath(path, NULL);
        CHECK_ERR_RET_VAL(!realp, NULL, "Path specified does not exist: %s", path);

        c = calloc(1, sizeof(struct SystemConfig));
        CHECK_ERR_GOTO(!c, error, "Could not allocate SystemConfig");

        c->prefix = realp;
        c->wanted_boot_mask = 0;

        if (getenv("CBM_FORCE_LEGACY")) {
                force_legacy = true;
        }

        legacy_boot = get_legacy_boot_device(realp);
        uefi_boot = get_boot_device();

        /* typically /sys, but we forcibly fail this with our tests */
        fw_path = string_printf("%s/firmware/efi", cbm_system_get_sysfs_path());
        native_uefi = nc_file_exists(fw_path);

        /*
         * uefi has precedence over legacy, if we detected both uefi wins
         * but if force_legacy is set then we honor "users choice"
         */
        if (!force_legacy && ((legacy_boot && uefi_boot) || uefi_boot)) {
                c->wanted_boot_mask = BOOTLOADER_CAP_UEFI;
        } else if (!native_uefi && (legacy_boot || force_legacy)) {
                c->wanted_boot_mask = BOOTLOADER_CAP_LEGACY;
        } else if (native_uefi && !force_legacy) {
                c->wanted_boot_mask = BOOTLOADER_CAP_UEFI;
        } else {
                c->wanted_boot_mask = BOOTLOADER_CAP_LEGACY;
        }

        if (c->wanted_boot_mask != 0) {
                if ((c->wanted_boot_mask & BOOTLOADER_CAP_LEGACY) == BOOTLOADER_CAP_LEGACY) {
                        c->boot_device = legacy_boot;
                        free(uefi_boot);
                } else if ((c->wanted_boot_mask & BOOTLOADER_CAP_UEFI) == BOOTLOADER_CAP_UEFI) {
                        c->boot_device = uefi_boot;
                        free(legacy_boot);
                }
        }

        /* Our probe methods are GPT only. If we found one, it's definitely GPT */
        if (c->boot_device) {
                rel = realpath(c->boot_device, NULL);
                if (!rel) {
                        LOG_FATAL("Cannot determine boot device: %s %s",
                                  c->boot_device,
                                  strerror(errno));
                } else {
                        free(c->boot_device);
                        c->boot_device = rel;
                        LOG_INFO("Fully resolved boot device: %s", rel);
                }
                c->wanted_boot_mask |= BOOTLOADER_CAP_GPT;

                /* determine fstype of the boot_device */
                c->wanted_boot_mask |= cbm_get_filesystem_cap(c->boot_device);
        }

        c->root_device = cbm_probe_path(realp);

        free(fw_path);
        return c;

 error:
        DECLARE_OOM();
        free(fw_path);
        free(realp);
        return NULL;
}

bool cbm_is_sysconfig_sane(SystemConfig *config)
{
        CHECK_FATAL_RET_VAL(!config, false, "sysconfig insane: Missing config");
        CHECK_FATAL_RET_VAL(!config->root_device, false,
                            "sysconfig insane: Missing root device");
        return true;
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
