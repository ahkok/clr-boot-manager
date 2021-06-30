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

#include <getopt.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "bootman.h"
#include "cli.h"
#include "config.h"
#include "log.h"

bool cbm_command_list_kernels(int argc, char **argv)
{
        autofree(char) *root = NULL;
        autofree(BootManager) *manager = NULL;
        char **kernels = NULL;
        bool update_efi_vars = true;

        if (!cli_default_args_init(&argc, &argv, &root, &update_efi_vars)) {
                return false;
        }

        manager = boot_manager_new();
        if (!manager) {
                DECLARE_OOM();
                return false;
        }

        boot_manager_set_update_efi_vars(manager, update_efi_vars);

        if (root) {
                if (!boot_manager_set_prefix(manager, root)) {
                        return false;
                }
        } else {
                /* Default to "/", bail if it doesn't work. */
                if (!boot_manager_set_prefix(manager, "/")) {
                        return false;
                }
        }

        /* Let CBM take care of the rest */
        kernels = boot_manager_list_kernels(manager);
        if (!kernels) {
                return false;
        }
        for (char **k = kernels; *k; k++) {
                printf("%s\n", *k);
                free(*k);
        }
        free(kernels);
        return true;
}

bool cbm_command_set_kernel(int argc, char **argv)
{
        autofree(char) *root = NULL;
        autofree(BootManager) *manager = NULL;
        char type[32] = { 0 };
        char version[16] = { 0 };
        int release = 0;
        Kernel kern = { 0 };
        bool update_efi_vars = true;

        if (!cli_default_args_init(&argc, &argv, &root, &update_efi_vars)) {
                return false;
        }

        manager = boot_manager_new();
        if (!manager) {
                DECLARE_OOM();
                return false;
        }

        boot_manager_set_update_efi_vars(manager, update_efi_vars);

        if (root) {
                if (!boot_manager_set_prefix(manager, root)) {
                        return false;
                }
        } else {
                /* Default to "/", bail if it doesn't work. */
                if (!boot_manager_set_prefix(manager, "/")) {
                        return false;
                }
        }

        if (argc != 1) {
                fprintf(stderr,
                        "set-kernel takes a kernel ID of the form %s.TYPE.VERSION-RELEASE\n",
                        KERNEL_NAMESPACE);
                return false;
        }

        if (sscanf(argv[optind], KERNEL_NAMESPACE ".%31[^.].%15[^-]-%d", type, version, &release) != 3) {
                fprintf(stderr,
                        "set-kernel takes a kernel ID of the form %s.TYPE.VERSION-RELEASE\n",
                        KERNEL_NAMESPACE);
                return false;
        }

        kern.meta.ktype = type;
        kern.meta.version = version;
        kern.meta.release = release;

        /* Let CBM take care of the rest */
        if (!boot_manager_set_default_kernel(manager, &kern)) {
                return false;
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
