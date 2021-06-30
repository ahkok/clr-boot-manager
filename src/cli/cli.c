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
#include <stdlib.h>
#include <string.h>

#include "cli.h"
#include "config.h"
#include "log.h"
#include "nica/files.h"
#include "util.h"

struct cli_option {
        struct option opt;
        char *desc;
};

#define OPTION(opt, req, flag, short_opt, desc)       \
        { {opt, req, flag, short_opt}, desc }         \

static struct cli_option cli_opts[] = {
        OPTION("path", required_argument, 0, 'p', "Set the base path for boot management operations."),
        OPTION("image", no_argument, 0, 'i', "Force clr-boot-manager to run in image mode(deprecated)."),
        OPTION("no-efi-update", no_argument, 0, 'n',
               "Don't update efi vars when using shim-systemd backend."),
        OPTION(0, 0, 0, 0, NULL),
};

void cli_print_default_args_help(void)
{
        int opt_len = (sizeof(cli_opts) / sizeof(struct cli_option)) - 1;
        int larger = -1;
        const char *flags_mask = "  -%c, --%s";

        fprintf(stdout, "\nOptions:\n");

        for (int i = 0; i < opt_len; i++) {
                struct cli_option curr = cli_opts[i];
                char tmp[60] = {0};
                int len;

                len = sprintf(tmp, flags_mask, curr.opt.val, curr.opt.name);
                if (len > larger) {
                        larger = len;
                }
        }

        for (int i = 0; i < opt_len; i++) {
                struct cli_option curr = cli_opts[i];
                char tmp[60] = {0};
                int len;

                len = sprintf(tmp, flags_mask, curr.opt.val, curr.opt.name);
                fprintf(stdout, "%s%*s%s\n", tmp, (larger - len) + 2, "", curr.desc);
        }
}

bool cli_default_args_init(int *argc, char ***argv, char **root, bool *update_efi_vars)
{
        int o_in = 0;
        int c;
        char *_root = NULL;
        int opt_len = sizeof(cli_opts) / sizeof(struct cli_option);
        struct option *default_opts;

        default_opts = alloca(sizeof(struct option) * (long unsigned int)opt_len);

        for (int i = 0; i < opt_len; i++) {
                default_opts[i] = cli_opts[i].opt;
        }

        /* We actually want to use getopt, so rewind one for getopt */;
        --(*argv);
        ++(*argc);

        if (!root) {
                return false;
        }

        /* Allow setting the root */
        while (true) {
                c = getopt_long(*argc, *argv, "nip:", default_opts, &o_in);
                if (c == -1) {
                        break;
                }
                switch (c) {
                case 0:
                case 'p':
                        if (optarg) {
                                if (_root) {
                                        free(_root);
                                        _root = NULL;
                                }
                                _root = strdup(optarg);
                        }
                        break;
                case 'i':
                        LOG_INFO("-i,--image flags are deprecated");
                        break;
                case 'n':
                        if (update_efi_vars) {
                                *update_efi_vars = false;
                        }
                        break;
                case '?':
                        goto bail;
                        break;
                default:
                        abort();
                }
        }
        *argc -= optind;

        if (_root) {
                *root = _root;
        }

        if (update_efi_vars && *update_efi_vars) {
                autofree(FILE) *f = NULL;
                autofree(char) *cfg_path = NULL;
                char *buf = NULL;
                size_t sn;
                ssize_t r = 0;

                cfg_path = string_printf("%s/%s/update_efi_vars", (*root != NULL) ? *root : "",
                                         KERNEL_CONF_DIRECTORY);
                CHECK_DBG_GOTO(!nc_file_exists(cfg_path), success, "No such file: %s", cfg_path);

                f = fopen(cfg_path, "r");
                CHECK_ERR_RET_VAL(!f, false, "Could not open file: %s", cfg_path);

                while ((r = getline(&buf, &sn, f)) > 0) {
                        if (!strncmp(buf, "no", 2) || !strncmp(buf, "false", 5)) {
                                *update_efi_vars = false;
                                break;
                        }
                }
        }

 success:
        return true;
bail:
        if (_root) {
                free(_root);
        }
        return false;
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
