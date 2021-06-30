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
#include <getopt.h>
#include <string.h>
#include <unistd.h>

#include "bootman.h"
#include "cli.h"
#include "log.h"
#include "nica/files.h"
#include "update.h"

bool cbm_command_update(int argc, char **argv)
{
        autofree(char) *root = NULL;
        autofree(BootManager) *manager = NULL;
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
        
        return cbm_command_update_do(manager, root, forced_image);
}

bool cbm_command_update_do(BootManager *manager, char *root, bool forced_image)
{
        if (!boot_manager_detect_kernel_dir(root)) {
                fprintf(stderr, "No kernels detected on system to update\n");
                return true;
        }

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
        /* Grab the available freestanding initrd */
        if (!boot_manager_enumerate_initrds_freestanding(manager)) {
                return false;
        }

        /* Let CBM take care of the rest */
        return boot_manager_update(manager);
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
