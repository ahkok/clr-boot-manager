/*
 * This file is part of clr-boot-manager.
 *
 * Copyright © 2020 Intel Corporation
 *
 * clr-boot-manager is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 */

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "bootloader.h"
#include "bootman.h"
#include "files.h"
#include "log.h"
#include "mbr.h"
#include "syslinux-common.h"
#include "system_stub.h"
#include "writer.h"

#define CONFIG_FILE "syslinux.cfg"

char *syslinux_common_get_default_kernel(const BootManager *manager)
{
        autofree(char) *config_path = NULL;
        autofree(FILE) *f = NULL;
        char *result = NULL;
        char *buf = NULL;
        size_t len, lplen = 0;
        ssize_t read;
        char *lookup = "DEFAULT ";
        struct SyslinuxContext *ctx = NULL;

        ctx = boot_manager_get_data((BootManager *)manager);

        lplen = strlen(lookup);
        config_path = string_printf("%s/"CONFIG_FILE, ctx->base_path);

        f = fopen(config_path, "r");
        CHECK_ERR_RET_VAL(!f, NULL, "Could not open config file: %s", config_path);

        while ((read = getline(&buf, &len, f)) != -1) {
                char *tk;
                size_t reslen;

                tk = strstr(buf, lookup);
                if (tk != NULL) {
                        result = strndup(tk+lplen, strlen(tk)-lplen);
                        reslen = strlen(result);

                        if (result[reslen-1] == '\n') {
                                result[reslen-1] = '\0';
                        }

                        break;
                }
        }

        if (buf)  {
                free(buf);
        }

        return result;
}

bool syslinux_common_install_kernel(const BootManager *manager, const Kernel *kernel)
{
        struct SyslinuxContext *ctx = boot_manager_get_data((BootManager *)manager);

        /* We may end up adding the same kernel again, when in repair situations
         * for existing kernels (and current == tip cases)
         */
        for (uint16_t i = 0; i < ctx->kernel_queue->len; i++) {
                const Kernel *k = nc_array_get(ctx->kernel_queue, i);
                if (streq(k->source.path, kernel->source.path)) {
                        return true;
                }
        }

        if (!nc_array_add(ctx->kernel_queue, (void *)kernel)) {
                DECLARE_OOM();
                abort();
        }

        return true;
}

bool syslinux_common_set_default_kernel(const BootManager *manager, const Kernel *default_kernel)
{
        autofree(char) *config_path = NULL;
        const CbmDeviceProbe *root_dev = NULL;
        autofree(char) *old_conf = NULL;
        autofree(CbmWriter) *writer = CBM_WRITER_INIT;
        NcHashmapIter iter = { 0 };
        char *initrd_name = NULL;
        int timeout;
        struct SyslinuxContext *ctx = NULL;

        ctx = boot_manager_get_data((BootManager *)manager);

        root_dev = boot_manager_get_root_device((BootManager *)manager);
        if (!root_dev) {
                LOG_FATAL("Root device unknown, this should never happen!");
                return false;
        }

        config_path = string_printf("%s/"CONFIG_FILE, ctx->base_path);

        if (!cbm_writer_open(writer)) {
                DECLARE_OOM();
                abort();
        }

        timeout = boot_manager_get_timeout_value((BootManager *)manager);

        /* No default kernel for set timeout */
        if (!default_kernel) {
                cbm_writer_append(writer, "TIMEOUT 100\n");
        } else if (timeout > 0) {
                cbm_writer_append_printf(writer, "TIMEOUT %d\n", timeout);
        }

        for (uint16_t i = 0; i < ctx->kernel_queue->len; i++) {
                const Kernel *k = nc_array_get(ctx->kernel_queue, i);
                autofree(char) *initrd_paths = NULL;
                initrd_paths = malloc(1);
                initrd_paths[0] = '\0';

                /* Mark it default */
                if (default_kernel && streq(k->meta.ktype, default_kernel->meta.ktype) &&
                    streq(k->meta.version, default_kernel->meta.version) &&
                    k->meta.release == default_kernel->meta.release) {
                        cbm_writer_append_printf(writer, "DEFAULT %s\n", k->target.legacy_path);
                }

                cbm_writer_append_printf(writer, "LABEL %s\n", k->target.legacy_path);
                cbm_writer_append_printf(writer, "  KERNEL %s\n", k->target.legacy_path);

                /* Add the initrd if we found one */
                if (k->target.initrd_path) {
                        char *tmp = initrd_paths;
                        initrd_paths = string_printf("%s,%s", initrd_paths, k->target.initrd_path);
                        free(tmp);
                }
                boot_manager_initrd_iterator_init(manager, &iter);
                while (boot_manager_initrd_iterator_next(&iter, &initrd_name)) {
                        char *tmp = initrd_paths;
                        initrd_paths = string_printf("%s,%s", initrd_paths, initrd_name);
                        free(tmp);
                }

                if (strlen(initrd_paths)) {
                        cbm_writer_append_printf(writer, "  INITRD %s\n", initrd_paths + 1);
                }

                /* Begin options */
                cbm_writer_append(writer, "APPEND ");

                /* Write out root UUID */
                if (root_dev->part_uuid) {
                        cbm_writer_append_printf(writer, "root=PARTUUID=%s ", root_dev->part_uuid);
                } else {
                        cbm_writer_append_printf(writer, "root=UUID=%s ", root_dev->uuid);
                }
                /* Add LUKS information if relevant */
                if (root_dev->luks_uuid) {
                        cbm_writer_append_printf(writer, "rd.luks.uuid=%s ", root_dev->luks_uuid);
                }
                /* Add Btrfs information if relevant */
                if (root_dev->btrfs_sub) {
                        cbm_writer_append_printf(writer, "rootflags=subvol=%s ", root_dev->btrfs_sub);
                }

                /* Write out the cmdline */
                cbm_writer_append_printf(writer, "%s\n", k->meta.cmdline);
        }

        cbm_writer_close(writer);

        if (cbm_writer_error(writer) != 0) {
                DECLARE_OOM();
                abort();
        }

        /* If the file is the same, don't write it again or sync */
        if (cbm_file_has_content(config_path) && file_get_text(config_path, &old_conf)) {
                if (streq(old_conf, writer->buffer)) {
                        return true;
                }
        }

        if (!file_set_text(config_path, writer->buffer)) {
                LOG_FATAL("syslinux_set_default_kernel: Failed to write %s: %s",
                          config_path,
                          strerror(errno));
                return false;
        }

        cbm_sync();
        return true;
}

void syslinux_common_destroy(const BootManager *manager)
{
        struct SyslinuxContext *ctx = boot_manager_get_data((BootManager *)manager);

        if (ctx->kernel_queue) {
                /* kernels pointers inside are not owned by the array */
                nc_array_free(&ctx->kernel_queue, NULL);
        }

        if (ctx->syslinux_cmd) {
                free(ctx->syslinux_cmd);
        }

        if (ctx->sgdisk_cmd) {
                free(ctx->sgdisk_cmd);
        }

        if (ctx->base_path) {
                free(ctx->base_path);
        }

        free(ctx);
        boot_manager_set_data((BootManager *)manager, NULL);
}

bool syslinux_common_init(const BootManager *manager, command_writer writer)
{
        autofree(char) *parent_disk = NULL;
        autofree(char) *boot_device = NULL;
        const char *prefix = NULL;
        int partition_index;
        struct SyslinuxContext *ctx = NULL;

        ctx = boot_manager_get_data((BootManager *)manager);
        if (ctx == NULL) {
                ctx = calloc(1, sizeof(struct SyslinuxContext));

                if (!ctx) {
                        DECLARE_OOM();
                        abort();
                }

                boot_manager_set_data((BootManager *)manager, ctx);
        }

        if (ctx->kernel_queue) {
                kernel_array_free(ctx->kernel_queue);
        }

        if (getenv("CBM_BOOTVAR_TEST_MODE")) {
                ctx->kernel_queue = nc_array_new();
        } else {
                ctx->kernel_queue = boot_manager_get_kernels((BootManager *)manager);
        }

        if (!ctx->kernel_queue) {
                DECLARE_OOM();
                abort();
        }

        if (ctx->base_path) {
                free(ctx->base_path);
                ctx->base_path = NULL;
        }

        ctx->base_path = boot_manager_get_boot_dir((BootManager *)manager);
        OOM_CHECK_RET(ctx->base_path, false);

        if (ctx->syslinux_cmd) {
                free(ctx->syslinux_cmd);
                ctx->syslinux_cmd = NULL;
        }

        if (ctx->sgdisk_cmd) {
                free(ctx->sgdisk_cmd);
                ctx->sgdisk_cmd = NULL;
        }

        prefix = boot_manager_get_prefix((BootManager *)manager);
        boot_device = get_legacy_boot_device((char *)prefix);

        if (!boot_device) {
                boot_device = get_boot_device();
        }

        CHECK_ERR_RET_VAL(!boot_device, false, "No boot partition found, you need to "
                          "mark the boot partition with \"legacy_boot\" flag.");

        CHECK_ERR_GOTO(!writer(ctx, prefix, boot_device), cleanup,
                       "Could not initialize bootloader command");

        partition_index = get_partition_index(prefix, boot_device);
        if (partition_index == -1) {
                LOG_ERROR("Failed to get partition index");
                goto cleanup;
        }

        parent_disk = get_parent_disk((char *)prefix);
        if (!parent_disk) {
                LOG_ERROR("Failed to get parent disk");
                goto cleanup;
        }

        ctx->sgdisk_cmd = string_printf("%s/usr/bin/sgdisk %s --attributes=%d:set:2",
                                        prefix, parent_disk, partition_index + 1);
        return true;

 cleanup:
        syslinux_common_destroy(manager);
        return false;
}

bool syslinux_common_install(const BootManager *manager)
{
        autofree(char) *boot_device = NULL;
        const char *prefix = NULL;
        int mbr = -1;
        ssize_t count = 0;
        bool is_gpt = false;
        struct SyslinuxContext *ctx;

        ctx = boot_manager_get_data((BootManager *)manager);

        prefix = boot_manager_get_prefix((BootManager *)manager);
        boot_device = get_parent_disk((char *)prefix);

        mbr = open(boot_device, O_WRONLY);
        CHECK_ERR_RET_VAL(mbr < 0, false, "Could not open boot device: %s", boot_device);

        is_gpt = boot_manager_get_wanted_boot_mask((BootManager *)manager)
                & BOOTLOADER_CAP_GPT;
        count = write(mbr, is_gpt ? syslinux_gptmbr_bin : syslinux_mbr_bin,
                      MBR_BIN_LEN);
        LOG_DEBUG("wrote \"%s.bin\" to %s", is_gpt ? "gptmbr" : "mbr",
                  boot_device);

        CHECK_ERR_GOTO(count != MBR_BIN_LEN, mbr_error,
                       "Written mbr size doesn't match the expected");

        close(mbr);

        CHECK_ERR_RET_VAL(cbm_system_system(ctx->syslinux_cmd) != 0, false,
                          "cbm_system_system() returned value != 0");

        CHECK_ERR_RET_VAL(cbm_system_system(ctx->sgdisk_cmd) != 0, false,
                          "Failed to run sgdisk command: %s", ctx->sgdisk_cmd);

        cbm_sync();
        return true;

 mbr_error:
        close(mbr);
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
