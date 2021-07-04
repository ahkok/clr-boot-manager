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

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "bootloader.h"
#include "files.h"
#include "log.h"
#include "mbr.h"
#include "nica/files.h"
#include "syslinux-common.h"
#include "system_stub.h"
#include "util.h"
#include "writer.h"

static bool extlinux_command_writer(struct SyslinuxContext *ctx, const char *prefix, char *boot_device)
{
        ctx->syslinux_cmd = string_printf("%s/usr/bin/extlinux -i %s --device %s &> /dev/null",
                                          prefix, ctx->base_path, boot_device);
        return ctx->syslinux_cmd != NULL;
}

static bool extlinux_init(const BootManager *manager)
{
        return syslinux_common_init(manager, extlinux_command_writer);
}

/* No op due since conf file will only have queued kernels anyway */
static bool extlinux_remove_kernel(__cbm_unused__ const BootManager *manager,
                                   __cbm_unused__ const Kernel *kernel)
{
        return true;
}

static bool extlinux_needs_update(__cbm_unused__ const BootManager *manager)
{
        return true;
}

static bool extlinux_needs_install(__cbm_unused__ const BootManager *manager)
{
        return true;
}

static bool extlinux_update(const BootManager *manager)
{
        return syslinux_common_install(manager);
}

static bool extlinux_remove(__cbm_unused__ const BootManager *manager)
{
        /* Maybe should return false? Unsure */
        return true;
}

static int extlinux_get_capabilities(const BootManager *manager)
{
        const char *prefix = NULL;
        autofree(char) *command = NULL;

        prefix = boot_manager_get_prefix((BootManager *)manager);
        command = string_printf("%s/usr/bin/extlinux", prefix);

        if (access(command, X_OK) != 0) {
                LOG_DEBUG("extlinux not found at %s\n", command);
                return 0;
        }

        return BOOTLOADER_CAP_GPT | BOOTLOADER_CAP_LEGACY | BOOTLOADER_CAP_EXTFS |
               BOOTLOADER_CAP_PARTLESS;
}

__cbm_export__ const BootLoader extlinux_bootloader = {.name = "extlinux",
                                                       .init = extlinux_init,
                                                       .install_kernel =
                                                           syslinux_common_install_kernel,
                                                       .remove_kernel = extlinux_remove_kernel,
                                                       .set_default_kernel =
                                                           syslinux_common_set_default_kernel,
                                                       .get_default_kernel =
                                                           syslinux_common_get_default_kernel,
                                                       .needs_install = extlinux_needs_install,
                                                       .needs_update = extlinux_needs_update,
                                                       .install = syslinux_common_install,
                                                       .update = extlinux_update,
                                                       .remove = extlinux_remove,
                                                       .destroy = syslinux_common_destroy,
                                                       .get_capabilities =
                                                           extlinux_get_capabilities };

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
