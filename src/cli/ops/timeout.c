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

#include <ctype.h>
#include <getopt.h>
#include <string.h>

#include "bootman.h"
#include "cli.h"
#include "update.h"

static inline bool is_numeric(const char *str)
{
        for (char *c = (char *)str; *c; c++) {
                if (!isdigit(*c)) {
                        if (c == str && *c == '-') {
                                continue;
                        }
                        return false;
                }
        }
        return true;
}

bool cbm_command_set_timeout(int argc, char **argv)
{
        int n_val = -1;
        autofree(char) *root = NULL;
        autofree(BootManager) *manager = NULL;
        bool update_efi_vars = false;

        if (!cli_default_args_init(&argc, &argv, &root, &update_efi_vars)) {
                return false;
        }

        manager = boot_manager_new();
        if (!manager) {
                DECLARE_OOM();
                return false;
        }

        boot_manager_set_update_efi_vars(manager, update_efi_vars);

        /* Use specified root if required */
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
                fprintf(stderr, "set-timeout takes one integer parameter\n");
                return false;
        }

        if (sscanf(argv[optind], "%d", &n_val) < 0) {
                fprintf(stderr, "Erroneous input. Please provide an integer value.\n");
                return false;
        }

        if (!is_numeric(argv[optind])) {
                fprintf(stderr, "Please provide a valid numeric value.\n");
                return false;
        }

        if (n_val < -1) {
                fprintf(stderr,
                        "Value of '%d' is incorrect. Use 0 if you mean to disable boot timeout.\n",
                        n_val);
                return false;
        }

        if (!boot_manager_set_timeout_value(manager, n_val)) {
                fprintf(stderr, "Failed to update timeout\n");
                return false;
        }
        if (n_val <= 0) {
                fprintf(stdout, "Timeout has been removed\n");
        } else {
                fprintf(stdout, "New timeout value is: %d\n", n_val);
        }

        return cbm_command_update_do(manager, root, false);
}

bool cbm_command_get_timeout(int argc, char **argv)
{
        autofree(char) *root = NULL;
        autofree(BootManager) *manager = NULL;
        bool update_efi_vars = false;

        cli_default_args_init(&argc, &argv, &root, &update_efi_vars);

        manager = boot_manager_new();
        if (!manager) {
                DECLARE_OOM();
                return false;
        }

        boot_manager_set_update_efi_vars(manager, update_efi_vars);

        /* Use specified root if required */
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

        if (argc != 0) {
                fprintf(stderr, "get-timeout does not take any parameters\n");
                return false;
        }

        int tval = boot_manager_get_timeout_value(manager);
        if (tval <= 0) {
                fprintf(stdout, "No timeout is currently configured\n");
        } else {
                fprintf(stdout, "Timeout value: %d seconds\n", tval);
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
