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

#pragma once

#include "bootman.h"

#if UINTPTR_MAX == 0xffffffffffffffff
#define DEFAULT_EFI_BLOB "BOOTX64.EFI"
#else
#define DEFAULT_EFI_BLOB "BOOTIA32.EFI"
#endif

typedef bool (*boot_loader_init)(const BootManager *);
typedef bool (*boot_loader_install_kernel)(const BootManager *, const Kernel *);
typedef const char *(*boot_loader_get_kernel_destination)(const BootManager *);
typedef bool (*boot_loader_remove_kernel)(const BootManager *, const Kernel *);
typedef bool (*boot_loader_set_default_kernel)(const BootManager *, const Kernel *kernel);
typedef char *(*boot_loader_get_default_kernel)(const BootManager *);
typedef bool (*boot_loader_needs_update)(const BootManager *);
typedef bool (*boot_loader_needs_install)(const BootManager *);
typedef bool (*boot_loader_install)(const BootManager *);
typedef bool (*boot_loader_update)(const BootManager *);
typedef bool (*boot_loader_remove)(const BootManager *);
typedef void (*boot_loader_destroy)(const BootManager *);
typedef int (*boot_loader_caps)(const BootManager *);

typedef enum {
        BOOTLOADER_CAP_MIN = 1 << 0,
        BOOTLOADER_CAP_UEFI = 1 << 1,    /**<Bootloader supports UEFI */
        BOOTLOADER_CAP_GPT = 1 << 2,     /**<Bootloader supports GPT boot partition */
        BOOTLOADER_CAP_LEGACY = 1 << 3,  /**<Bootloader supports legacy boot */
        BOOTLOADER_CAP_EXTFS = 1 << 4,   /**<Bootloader supports ext2/3/4 */
        BOOTLOADER_CAP_FATFS = 1 << 5,   /**<Bootloader supports vfat */
        BOOTLOADER_CAP_PARTLESS = 1<< 6, /**<Bootloader supports partitionless boot */
        BOOTLOADER_CAP_MAX = 1 << 7
} BootLoaderCapability;

/**
 * Virtual BootLoader provider
 */
typedef struct BootLoader {
        const char *name;      /**<Name of the implementation */
        boot_loader_init init; /**<Init function */
        boot_loader_get_kernel_destination
            get_kernel_destination; /**<Get location where bootloader expects the kernels to reside */
        boot_loader_install_kernel install_kernel;         /**<Install a given kernel */
        boot_loader_remove_kernel remove_kernel;           /**<Remove a given kernel */
        boot_loader_set_default_kernel set_default_kernel; /**<Set the default kernel */
        boot_loader_get_default_kernel get_default_kernel; /**<Get the default kernel */
        boot_loader_needs_update needs_update;             /**<Check if an update is required */
        boot_loader_needs_install needs_install;           /**<Check if an install is required */
        boot_loader_install install;                       /**<Install this bootloader */
        boot_loader_update update;                         /**<Update this bootloader */
        boot_loader_remove remove;         /**<Remove this bootloader from the disk */
        boot_loader_destroy destroy;       /**<Perform necessary cleanups */
        boot_loader_caps get_capabilities; /**<Check capabilities */
} BootLoader;

#define __cbm_export__ __attribute__((visibility("default")))

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
