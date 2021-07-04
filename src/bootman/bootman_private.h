/*
 * This file is part of clr-boot-manager.
 *
 * This file masks private implementation details to share throughout the
 * libcbm implementation for the purposes of organisational sanity.
 *
 * Copyright © 2016-2018 Intel Corporation
 *
 * clr-boot-manager is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 */

#pragma once

#ifndef _BOOTMAN_INTERNAL_
#error This file can only be included within libcbm!
#endif

#include <stdbool.h>

#include "bootloader.h"
#include "bootman.h"
#include "os-release.h"

struct BootManager {
        char *kernel_dir;              /**<Kernel directory */
        const BootLoader *bootloader;  /**<Selected bootloader */
        CbmOsRelease *os_release;      /**<Parsed os-release file */
        char *abs_bootdir;             /**<Real boot dir */
        SystemKernel sys_kernel;       /**<Native kernel info, if any */
        bool have_sys_kernel;          /**<Whether sys_kernel is set */
        bool image_mode;               /**<Are we in image mode? */
        bool update_efi_vars;          /**<Should we update efi variables? */
        SystemConfig *sysconfig;       /**<System configuration */
        char *cmdline;                 /**<Additional cmdline to append */
        char *initrd_freestanding_dir; /**<Initrd without kernel deps directory */
        char *user_initrd_freestanding_dir; /**<User's initrd without kernel deps directory */
        NcHashmap *initrd_freestanding;/**<Array of initrds without kernel deps */
        void *data; /**<Bootloaders private data */
};

/**
 * Internal function to install the kernel blob itself
 */
bool boot_manager_install_kernel_internal(const BootManager *manager, const Kernel *kernel);

/**
 * Internal function to remove the kernel blob itself
 */
bool boot_manager_remove_kernel_internal(const BootManager *manager, const Kernel *kernel);

/**
 * Internal function to unmount boot directory
 */
void umount_boot(char *boot_dir);

/**
 * Internal function to mount the boot directory
 *
 * Returns tri-state of -1 for error, 0 for already mounted and 1 for mount
 * completed. *boot_directory should be free'd by caller.
 */
int mount_boot(BootManager *self, char **boot_directory);

/**
 * Detect if legacy, if so make sure there's a boot partition, in that case mount. For
 * other cases always try to mount.
 *
 * @see mount_boot() for return and error conditions.
 */
int detect_and_mount_boot(BootManager *self, char **boot_dir);

/**
 * Internal function to sort by Kernel structs by release number (highest first)
 */
int kernel_compare_reverse(const void *a, const void *b);

/**
 * Given a boot_device returns the filesystem name, if unknown filesystem returns NULL.
 */
const char *cbm_get_fstype_name(const char *boot_device);

/**
 * Check if the system supports a "partitionless" (the system has no /boot partition) boot.
 * Conditions are:
 * - The bootloader supports this.
 * - The system is not UEFI.
 * - The /boot folder is not empty.
 */
bool check_partitionless_boot(const BootManager *self, const char *boot_dir);

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
