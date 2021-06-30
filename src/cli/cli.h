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

#include <stdbool.h>

typedef bool (*subcommand_callback)(int argc, char **argv);

typedef struct SubCommand {
        const char *name;
        const char *blurb;
        const char *usage;
        const char *help;
        subcommand_callback callback;
        bool requires_root;
} SubCommand;

bool cli_default_args_init(int *argc, char ***argv, char **root, bool *update_efi_vars);
void cli_print_default_args_help(void);

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
