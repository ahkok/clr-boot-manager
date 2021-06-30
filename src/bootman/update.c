/*
 * This file is part of clr-boot-manager.
 *
 * Copyright © 2016-2017 Intel Corporation
 *
 * clr-boot-manager is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 */

#define _GNU_SOURCE

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <sys/mount.h>
#include <unistd.h>

#include "bootman.h"
#include "bootman_private.h"
#include "files.h"
#include "log.h"
#include "nica/files.h"
#include "system_stub.h"

static bool boot_manager_update_target(BootManager *self);
static bool boot_manager_update_bootloader(BootManager *self);

bool boot_manager_update(BootManager *self)
{
        assert(self != NULL);
        bool ret = false;
        autofree(char) *boot_dir = NULL;
        int did_mount = -1;

        did_mount = detect_and_mount_boot(self, &boot_dir);
        ret = boot_manager_update_target(self);
        if (did_mount > 0) {
                umount_boot(boot_dir);
        }

        /* Done */
        return ret;
}

/**
 * Update the target with logical view of a native installation
 */
static bool boot_manager_update_target(BootManager *self)
{
        assert(self != NULL);
        autofree(KernelArray) *kernels = NULL;
        autofree(NcHashmap) *mapped_kernels = NULL;
        Kernel *running = NULL;
        NcHashmapIter map_iter = { 0 };
        const char *kernel_type = NULL;
        KernelArray *typed_kernels = NULL;
        NcArray *removals = NULL;
        Kernel *new_default = NULL;
        const SystemKernel *system_kernel = NULL;
        bool ret = false;
        bool bootloader_updated = false;

        LOG_DEBUG("Now beginning update");

        /* Grab the available kernels */
        kernels = boot_manager_get_kernels(self);
        if (!kernels || kernels->len == 0) {
                LOG_ERROR("No kernels discovered in %s, bailing", self->kernel_dir);
                return false;
        }

        LOG_DEBUG("update_native: %d available kernels", kernels->len);

        /* Get them sorted */
        nc_array_qsort(kernels, kernel_compare_reverse);

        running = boot_manager_get_running_kernel(self, kernels);
        /* Try fallback comparison */
        if (!running) {
                running = boot_manager_get_running_kernel_fallback(self, kernels);
        }

        system_kernel = boot_manager_get_system_kernel(self);

        if (!running) {
                /* We don't know the currently running kernel, don't try to
                 * remove anything */
                LOG_ERROR("Cannot determine the currently running kernel");
        } else {
                LOG_DEBUG("update_native: Running kernel is (%s) %s",
                          running->meta.ktype,
                          running->source.path);
        }

        /** Map kernels to type */
        mapped_kernels = boot_manager_map_kernels(self, kernels);
        if (!mapped_kernels || nc_hashmap_size(mapped_kernels) == 0) {
                LOG_FATAL("Failed to map kernels by type, bailing");
                return false;
        }

        /* Get the bootloader sorted out */
        if (boot_manager_update_bootloader(self)) {
                LOG_SUCCESS("update_native: Bootloader updated");
                bootloader_updated = true;
        }

        if (!boot_manager_copy_initrd_freestanding(self)) {
                LOG_ERROR("Failed to copying freestanding initrd");
                return false;
        }

        /* This is mostly to allow a repair-situation */
        if (running) {
                /* Not necessarily fatal. */
                if (!boot_manager_install_kernel(self, running)) {
                        LOG_ERROR("Failed to repair running kernel");
                } else {
                        LOG_SUCCESS("update_native: Repaired running kernel %s",
                                    running->source.path);
                }
        }

        nc_hashmap_iter_init(mapped_kernels, &map_iter);
        while (nc_hashmap_iter_next(&map_iter, (void **)&kernel_type, (void **)&typed_kernels)) {
                Kernel *tip = NULL;
                Kernel *last_good = NULL;

                LOG_DEBUG("update_native: Checking kernels for type %s", kernel_type);

                /* Sort this kernel set highest to lowest */
                nc_array_qsort(typed_kernels, kernel_compare_reverse);

                /* Get the default kernel selection */
                tip = boot_manager_get_default_for_type(self, typed_kernels, kernel_type);
                if (!tip) {
                        LOG_ERROR("Could not find default kernel for type %s, using highest relno",
                                  kernel_type);
                        /* Fallback to highest release number */
                        tip = nc_array_get(typed_kernels, 0);
                } else {
                        LOG_INFO("update_native: Default kernel for type %s is %s",
                                 kernel_type,
                                 tip->source.path);
                }

                /* Ensure this tip kernel is installed */
                if (!boot_manager_install_kernel(self, tip)) {
                        LOG_FATAL("Failed to install default-%s kernel: %s",
                                  tip->meta.ktype,
                                  tip->source.path);
                        goto cleanup;
                }
                LOG_SUCCESS("update_native: Installed tip for %s: %s",
                            kernel_type,
                            tip->source.path);

                /* Last known booting kernel, might be null. */
                last_good = boot_manager_get_last_booted(self, typed_kernels);

                /* Ensure this guy is still installed/repaired */
                if (last_good) {
                        if (!boot_manager_install_kernel(self, last_good)) {
                                LOG_FATAL("Failed to install last-good kernel: %s",
                                          last_good->source.path);
                                goto cleanup;
                        }
                        LOG_SUCCESS("update_native: Installed last_good kernel (%s) (%s)",
                                    kernel_type,
                                    last_good->source.path);
                } else {
                        LOG_DEBUG("update_native: No last_good kernel for type %s", kernel_type);
                }

                /* Only allow garbage collection when we know the running kernel */
                if (running) {
                        for (uint16_t i = 0; i < typed_kernels->len; i++) {
                                Kernel *tk = nc_array_get(typed_kernels, i);
                                LOG_DEBUG("update_native: Analyzing for type %s: %s",
                                          kernel_type,
                                          tk->source.path);
                                /* Preserve running kernel */
                                if (tk == running) {
                                        LOG_DEBUG("update_native: Skipping running kernel");
                                        continue;
                                }
                                LOG_INFO("update_native: not-running: %s", tk->source.path);
                                /* Preserve tip */
                                if (tip && tk == tip) {
                                        LOG_DEBUG("update_native: Skipping default-%s: %s",
                                                  kernel_type,
                                                  tk->source.path);
                                        continue;
                                }
                                LOG_INFO("update_native: not-default-%s: %s",
                                         kernel_type,
                                         tk->source.path);
                                /* Preserve last running */
                                if (last_good && tk == last_good) {
                                        LOG_DEBUG("update_native: Skipping last_good kernel");
                                        continue;
                                }
                                LOG_INFO("update_native: not-last-booted: %s", tk->source.path);
                                if (!removals) {
                                        removals = nc_array_new();
                                }
                                /* Schedule removal of kernel - regardless of install status */
                                if (!nc_array_add(removals, tk)) {
                                        DECLARE_OOM();
                                        goto cleanup;
                                }
                                LOG_INFO("update_native: Proposed for deletion from %s: %s",
                                         kernel_type,
                                         tk->source.path);
                        }
                }
        }

        /* Might return NULL */
        if (!running) {
                /* Attempt to get it based on the current uname anyway */
                if (system_kernel && system_kernel->ktype[0] != '\0') {
                        new_default =
                            boot_manager_get_default_for_type(self, kernels, system_kernel->ktype);
                }
        } else {
                new_default = boot_manager_get_default_for_type(self, kernels, running->meta.ktype);
        }

        if (new_default) {
                if (!boot_manager_set_default_kernel(self, new_default)) {
                        LOG_ERROR("Failed to set the default kernel to: %s",
                                  new_default ? new_default->source.path : "<timeout mode>");
                        goto cleanup;
                }

                LOG_SUCCESS("update_native: Default kernel for %s is %s",
                            new_default->meta.ktype,
                            new_default->source.path);
        } else if (running) {
                LOG_INFO("update_native: No possible default kernel for %s", running->meta.ktype);
        } else {
                LOG_INFO("No kernel available for any type");
        }

        if (bootloader_updated) {
                ret = true;
        }

        if (!removals) {
                /* We're done. */
                LOG_DEBUG("No kernel removals found");
                goto cleanup;
        }

        /* Now remove the older kernels */
        for (uint16_t i = 0; i < removals->len; i++) {
                Kernel *k = nc_array_get(removals, i);
                LOG_INFO("update_native: Garbage collecting %s: %s", k->meta.ktype, k->source.path);
                if (!boot_manager_remove_kernel(self, k)) {
                        LOG_ERROR("Failed to remove kernel: %s", k->source.path);
                        ret = false;
                        goto cleanup;
                }
        }

cleanup:
        if (!boot_manager_remove_initrd_freestanding(self)) {
                ret = false;
                LOG_ERROR("Failed to remove old freestanding initrd");
        }
        if (removals) {
                nc_array_free(&removals, NULL);
        }
        return ret;
}

/**
 * Handle the update logic for the bootloader as both methods require the
 * same calls.
 */
static bool boot_manager_update_bootloader(BootManager *self)
{
        if (boot_manager_needs_install(self)) {
                /* Attempt install of the bootloader */
                int flags = BOOTLOADER_OPERATION_INSTALL | BOOTLOADER_OPERATION_NO_CHECK;
                if (!boot_manager_modify_bootloader(self, flags)) {
                        LOG_FATAL("Failed to install bootloader");
                        return false;
                }
        } else if (boot_manager_needs_update(self)) {
                /* Attempt update of the bootloader */
                int flags = BOOTLOADER_OPERATION_UPDATE | BOOTLOADER_OPERATION_NO_CHECK;
                if (!boot_manager_modify_bootloader(self, flags)) {
                        LOG_FATAL("Failed to update bootloader");
                        return false;
                }
        }
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
