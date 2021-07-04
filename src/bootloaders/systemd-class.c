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
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "bootloader.h"
#include "bootman.h"
#include "config.h"
#include "files.h"
#include "log.h"
#include "nica/files.h"
#include "systemd-class.h"
#include "util.h"
#include "writer.h"

/**
 * Private to systemd-class implementation
 */
typedef struct SdClassConfig {
        char *efi_dir;
        char *vendor_dir;
        char *entries_dir;
        char *base_path;
        char *efi_blob_source;
        char *efi_blob_dest;
        char *default_path_efi_blob;
        char *loader_config;
        char *kernel_dir;
        char *kernel_dir_esp;
} SdClassConfig;

static SdClassConfig sd_class_config = { 0 };
static BootLoaderConfig *sd_config = NULL;

static const char *(*get_kernel_destination_impl)(const BootManager *);

#define FREE_IF_SET(x)                                                                             \
        {                                                                                          \
                if (x) {                                                                           \
                        free(x);                                                                   \
                        x = NULL;                                                                  \
                }                                                                                  \
        }

const char *sd_class_get_kernel_destination_default(__cbm_unused__ const BootManager *manager)
{
        return sd_class_config.kernel_dir_esp;
}

bool sd_class_init(const BootManager *manager, BootLoaderConfig *config)
{
        char *base_path = NULL;
        char *efi_dir = NULL;
        char *vendor_dir = NULL;
        char *entries_dir = NULL;
        char *efi_blob_source = NULL;
        char *efi_blob_dest = NULL;
        char *default_path_efi_blob = NULL;
        char *loader_config = NULL;
        const char *prefix = NULL;

        sd_config = config;

        get_kernel_destination_impl = sd_class_get_kernel_destination_default;

        /* Cache all of these to save useless allocs of the same paths later */
        base_path = boot_manager_get_boot_dir((BootManager *)manager);
        OOM_CHECK_RET(base_path, false);
        sd_class_config.base_path = base_path;

        efi_dir = nc_build_case_correct_path(base_path, "EFI", "Boot", NULL);
        OOM_CHECK_RET(efi_dir, false);
        sd_class_config.efi_dir = efi_dir;

        vendor_dir = nc_build_case_correct_path(base_path, "EFI", sd_config->vendor_dir, NULL);
        OOM_CHECK_RET(vendor_dir, false);
        sd_class_config.vendor_dir = vendor_dir;

        entries_dir = nc_build_case_correct_path(base_path, "loader", "entries", NULL);
        OOM_CHECK_RET(entries_dir, false);
        sd_class_config.entries_dir = entries_dir;

        prefix = boot_manager_get_prefix((BootManager *)manager);

        /* EFI paths */
        efi_blob_source =
            string_printf("%s/%s/%s", prefix, sd_config->efi_dir, sd_config->efi_blob);
        sd_class_config.efi_blob_source = efi_blob_source;

        efi_blob_dest = nc_build_case_correct_path(sd_class_config.base_path,
                                                   "EFI",
                                                   sd_config->vendor_dir,
                                                   sd_config->efi_blob,
                                                   NULL);
        OOM_CHECK_RET(efi_blob_dest, false);
        sd_class_config.efi_blob_dest = efi_blob_dest;

        /* default EFI loader path */
        default_path_efi_blob = nc_build_case_correct_path(sd_class_config.base_path,
                                                           "EFI",
                                                           "Boot",
                                                           DEFAULT_EFI_BLOB,
                                                           NULL);
        OOM_CHECK_RET(default_path_efi_blob, false);
        sd_class_config.default_path_efi_blob = default_path_efi_blob;

        /* Loader entry */
        loader_config =
            nc_build_case_correct_path(sd_class_config.base_path, "loader", "loader.conf", NULL);
        OOM_CHECK_RET(loader_config, false);
        sd_class_config.loader_config = loader_config;

        sd_class_config.kernel_dir = nc_build_case_correct_path(sd_class_config.base_path,
                                                                "EFI", KERNEL_NAMESPACE, NULL);
        sd_class_config.kernel_dir_esp = strdup(sd_class_config.kernel_dir + strlen(sd_class_config.base_path));

        return true;
}

void sd_class_set_get_kernel_destination_impl(const char *(*impl)(const BootManager *))
{
        get_kernel_destination_impl = impl;
}

const char *sd_class_get_kernel_destination(const BootManager *manager)
{
        return get_kernel_destination_impl(manager);
}

void sd_class_destroy(__cbm_unused__ const BootManager *manager)
{
        FREE_IF_SET(sd_class_config.efi_dir);
        FREE_IF_SET(sd_class_config.vendor_dir);
        FREE_IF_SET(sd_class_config.entries_dir);
        FREE_IF_SET(sd_class_config.base_path);
        FREE_IF_SET(sd_class_config.efi_blob_source);
        FREE_IF_SET(sd_class_config.efi_blob_dest);
        FREE_IF_SET(sd_class_config.default_path_efi_blob);
        FREE_IF_SET(sd_class_config.loader_config);
        FREE_IF_SET(sd_class_config.kernel_dir);
        FREE_IF_SET(sd_class_config.kernel_dir_esp);
}

/* i.e. $prefix/$boot/loader/entries/Clear-linux-native-4.1.6-113.conf */
static char *get_entry_path_for_kernel(BootManager *manager, const Kernel *kernel)
{
        if (!manager || !kernel) {
                return NULL;
        }
        autofree(char) *item_name = NULL;
        const char *prefix = NULL;

        prefix = boot_manager_get_vendor_prefix(manager);

        item_name = string_printf("%s-%s-%s-%d.conf",
                                  prefix,
                                  kernel->meta.ktype,
                                  kernel->meta.version,
                                  kernel->meta.release);

        return nc_build_case_correct_path(sd_class_config.base_path,
                                          "loader",
                                          "entries",
                                          item_name,
                                          NULL);
}

static bool sd_class_ensure_dirs(void)
{
        if (!nc_mkdir_p(sd_class_config.efi_dir, 00755)) {
                LOG_FATAL("Failed to create %s: %s", sd_class_config.efi_dir, strerror(errno));
                return false;
        }
        cbm_sync();

        if (!nc_mkdir_p(sd_class_config.vendor_dir, 00755)) {
                LOG_FATAL("Failed to create %s: %s", sd_class_config.vendor_dir, strerror(errno));
                return false;
        }
        cbm_sync();

        if (!nc_mkdir_p(sd_class_config.kernel_dir, 00755)) {
                LOG_FATAL("Failed to create %s: %s", sd_class_config.kernel_dir, strerror(errno));
                return false;
        }
        cbm_sync();

        if (!nc_mkdir_p(sd_class_config.entries_dir, 00755)) {
                LOG_FATAL("Failed to create %s: %s", sd_class_config.entries_dir, strerror(errno));
                return false;
        }
        cbm_sync();

        return true;
}

bool sd_class_install_kernel(const BootManager *manager, const Kernel *kernel)
{
        if (!manager || !kernel) {
                return false;
        }
        autofree(char) *conf_path = NULL;
        const CbmDeviceProbe *root_dev = NULL;
        const char *os_name = NULL;
        autofree(char) *old_conf = NULL;
        autofree(CbmWriter) *writer = CBM_WRITER_INIT;
        NcHashmapIter iter = { 0 };
        char *initrd_name = NULL;

        conf_path = get_entry_path_for_kernel((BootManager *)manager, kernel);

        if (!cbm_writer_open(writer)) {
                DECLARE_OOM();
                abort();
        }

        /* Build the options for the entry */
        root_dev = boot_manager_get_root_device((BootManager *)manager);
        if (!root_dev) {
                LOG_FATAL("Root device unknown, this should never happen! %s", kernel->source.path);
                return false;
        }

        os_name = boot_manager_get_os_name((BootManager *)manager);

        /* Standard title + linux lines */
        cbm_writer_append_printf(writer, "title %s\n", os_name);
        cbm_writer_append_printf(writer,
                                 "linux %s/%s\n",
                                 get_kernel_destination_impl(manager),
                                 kernel->target.path);

        /* Optional initrd */
        if (kernel->target.initrd_path) {
                cbm_writer_append_printf(writer,
                                         "initrd %s/%s\n",
                                         get_kernel_destination_impl(manager),
                                         kernel->target.initrd_path);
        }

        boot_manager_initrd_iterator_init(manager, &iter);
        while (boot_manager_initrd_iterator_next(&iter, &initrd_name)) {
                cbm_writer_append_printf(writer,
                                         "initrd %s/%s\n",
                                         get_kernel_destination_impl(manager),
                                         initrd_name);
        }

        /* Add the root= section */
        if (root_dev->part_uuid) {
                cbm_writer_append_printf(writer, "options root=PARTUUID=%s ", root_dev->part_uuid);
        } else {
                cbm_writer_append_printf(writer, "options root=UUID=%s ", root_dev->uuid);
        }
        /* Add LUKS information if relevant */
        if (root_dev->luks_uuid) {
                cbm_writer_append_printf(writer, "rd.luks.uuid=%s ", root_dev->luks_uuid);
        }
        /* Add Btrfs information if relevant */
        if (root_dev->btrfs_sub) {
                cbm_writer_append_printf(writer, "rootflags=subvol=%s ", root_dev->btrfs_sub);
        }

        /* Finish it off with the command line options */
        cbm_writer_append_printf(writer, "%s\n", kernel->meta.cmdline);
        cbm_writer_close(writer);

        if (cbm_writer_error(writer) != 0) {
                DECLARE_OOM();
                abort();
        }

        /* If our new config matches the old config, just return. */
        if (file_get_text(conf_path, &old_conf)) {
                if (streq(old_conf, writer->buffer)) {
                        return true;
                }
        }

        if (!file_set_text(conf_path, writer->buffer)) {
                LOG_FATAL("Failed to create loader entry for: %s [%s]",
                          kernel->source.path,
                          strerror(errno));
                return false;
        }

        cbm_sync();

        return true;
}

bool sd_class_remove_kernel(const BootManager *manager, const Kernel *kernel)
{
        if (!manager || !kernel) {
                return false;
        }

        autofree(char) *conf_path = NULL;

        conf_path = get_entry_path_for_kernel((BootManager *)manager, kernel);
        OOM_CHECK_RET(conf_path, false);

        /* We must take a non-fatal approach in a remove operation */
        if (nc_file_exists(conf_path)) {
                if (unlink(conf_path) < 0) {
                        LOG_ERROR("sd_class_remove_kernel: Failed to remove %s: %s",
                                  conf_path,
                                  strerror(errno));
                } else {
                        cbm_sync();
                }
        }

        return true;
}

bool sd_class_set_default_kernel(const BootManager *manager, const Kernel *kernel)
{
        if (!manager) {
                return false;
        }

        autofree(char) *item_name = NULL;
        int timeout = 0;
        const char *prefix = NULL;
        autofree(char) *old_conf = NULL;

        prefix = boot_manager_get_vendor_prefix((BootManager *)manager);

        /* No default possible, set high time out */
        if (!kernel) {
                item_name = strdup("timeout 10\n");
                if (!item_name) {
                        DECLARE_OOM();
                        return false;
                }
                /* Check if the config changed and write the new one */
                goto write_config;
        }

        timeout = boot_manager_get_timeout_value((BootManager *)manager);

        if (timeout > 0) {
                /* Set the timeout as configured by the user */
                item_name = string_printf("timeout %d\ndefault %s-%s-%s-%d.conf\n",
                                          timeout,
                                          prefix,
                                          kernel->meta.ktype,
                                          kernel->meta.version,
                                          kernel->meta.release);
        } else {
                item_name = string_printf("default %s-%s-%s-%d.conf\n",
                                          prefix,
                                          kernel->meta.ktype,
                                          kernel->meta.version,
                                          kernel->meta.release);
        }

write_config:
        if (file_get_text(sd_class_config.loader_config, &old_conf)) {
                if (streq(old_conf, item_name)) {
                        return true;
                }
        }

        if (!file_set_text(sd_class_config.loader_config, item_name)) {
                LOG_FATAL("sd_class_set_default_kernel: Failed to write %s: %s",
                          sd_class_config.loader_config,
                          strerror(errno));
                return false;
        }

        cbm_sync();

        return true;
}

static char *parse_kernel_from_loader(char *conf, const BootManager *manager)
{
        char ktype[32] = { 0 };
        char version[16] = { 0 };
        long int release = -1;
        const char *prefix = NULL;
        char * const hdr = "default ";
        char *start = NULL;
        char *end = NULL;
        int field = 0;
        size_t ln;

        if (!conf) {
                return NULL;
        }

        /*
         * Looking for the 'default $kernel' line, it may contain extra
         * lines around it, but just grab the start and end of the
         * $kernel content.
         */
        start = strstr(conf, hdr);
        if (!start) {
                return NULL;
        }
        start = start + strlen(hdr);
        *(start - 1) = '-';
        end = strstr(start, "\n");

        /* Make end a nul character */
        if (end) {
                *end = 0;
        } else {
                end = start + strlen(start);
        }

        /*
         * Horrible manually parsing a string of the form:
         * 'Clear-linux-pk414-standard-4.14.74-115' below.
         *
         * Idea is to parse backwards using the dashes '-' as field
         * boundries. First comes release #, then kernel version and
         * finally the kernel identifier with the vendor prefix.
         *
         * So given the example we want:
         * release = 115;
         * version = "4.14.74";
         * ktype = "pk414-standard";
         */
        for (char *c = end; c > start; c--) {
                if (field == 2) {
                        /* The rest includes ktype */
                        prefix = boot_manager_get_vendor_prefix((BootManager *)manager);
                        ln = strlen(start);
                        /*
                         * Add one to prefix length for the dash between
                         * prefix and ktype.
                         */
                        size_t pln = strlen(prefix) + 1;
                        if (((pln) >= ln) || (ln - pln > 31)) {
                                break;
                        }
                        memcpy(ktype, start + pln, ln - pln);
                        break;
                }

                if (*c == '-') {
                        if (*(c + 1) == 0) {
                                /* Invalid field */
                                break;
                        }
                        switch (field) {
                        case 0:
                                errno = 0;
                                release = strtol(c + 1, NULL, 10);
                                if (errno != 0) {
                                        release = -1;
                                }
                                break;
                        case 1:
                                ln = strlen(c + 1);
                                if (ln > 15) {
                                        break;
                                }
                                memcpy(version, c + 1, ln);
                                break;
                        default:
                                break;
                        }
                        *c = 0;
                        field++;
                }
        }

        if (!ktype[0] || !version[0] || release < 0) {
                return NULL;
        }

        return string_printf("%s.%s.%s-%ld",
                             KERNEL_NAMESPACE,
                             ktype,
                             version,
                             release);

}

char *sd_class_get_default_kernel(const BootManager *manager)
{
        if (!manager) {
                return NULL;
        }

        autofree(char) *conf = NULL;
        char *kernel = NULL;

        if (file_get_text(sd_class_config.loader_config, &conf)) {
                kernel = parse_kernel_from_loader(conf, manager);
        }

        if (!kernel) {
                LOG_FATAL("sd_class_get_default_kernel Unable to parse loader config");
        }
        return kernel;
}

bool sd_class_needs_install(const BootManager *manager)
{
        if (!manager) {
                return false;
        }

        const char *paths[] = { sd_class_config.efi_blob_dest,
                                sd_class_config.default_path_efi_blob };
        const char *source_path = sd_class_config.efi_blob_source;

        /* Catch this in the install */
        if (!nc_file_exists(source_path)) {
                return true;
        }

        /* Try to see if targets are missing */
        for (size_t i = 0; i < ARRAY_SIZE(paths); i++) {
                const char *check_p = paths[i];

                if (!nc_file_exists(check_p)) {
                        return true;
                }
        }

        return false;
}

bool sd_class_needs_update(const BootManager *manager)
{
        if (!manager) {
                return false;
        }

        const char *paths[] = { sd_class_config.efi_blob_dest,
                                sd_class_config.default_path_efi_blob };
        const char *source_path = sd_class_config.efi_blob_source;

        for (size_t i = 0; i < ARRAY_SIZE(paths); i++) {
                const char *check_p = paths[i];

                if (nc_file_exists(check_p) && !cbm_files_match(source_path, check_p)) {
                        return true;
                }
        }

        return false;
}

bool sd_class_install(const BootManager *manager)
{
        if (!manager) {
                return false;
        }

        if (!sd_class_ensure_dirs()) {
                LOG_FATAL("Failed to create required directories for %s", sd_config->name);
                return false;
        }

        /* Install vendor EFI blob */
        if (!copy_file_atomic(sd_class_config.efi_blob_source,
                              sd_class_config.efi_blob_dest,
                              00644)) {
                LOG_FATAL("Failed to install %s: %s",
                          sd_class_config.efi_blob_dest,
                          strerror(errno));
                return false;
        }
        cbm_sync();

        /* Install default EFI blob */
        if (!copy_file_atomic(sd_class_config.efi_blob_source,
                              sd_class_config.default_path_efi_blob,
                              00644)) {
                LOG_FATAL("Failed to install %s: %s",
                          sd_class_config.default_path_efi_blob,
                          strerror(errno));
                return false;
        }
        cbm_sync();

        return true;
}

bool sd_class_update(const BootManager *manager)
{
        if (!manager) {
                return false;
        }
        if (!sd_class_ensure_dirs()) {
                LOG_FATAL("Failed to create required directories for %s", sd_config->name);
                return false;
        }

        if (!cbm_files_match(sd_class_config.efi_blob_source, sd_class_config.efi_blob_dest)) {
                if (!copy_file_atomic(sd_class_config.efi_blob_source,
                                      sd_class_config.efi_blob_dest,
                                      00644)) {
                        LOG_FATAL("Failed to update %s: %s",
                                  sd_class_config.efi_blob_dest,
                                  strerror(errno));
                        return false;
                }
        }
        cbm_sync();

        if (!cbm_files_match(sd_class_config.efi_blob_source,
                             sd_class_config.default_path_efi_blob)) {
                if (!copy_file_atomic(sd_class_config.efi_blob_source,
                                      sd_class_config.default_path_efi_blob,
                                      00644)) {
                        LOG_FATAL("Failed to update %s: %s",
                                  sd_class_config.default_path_efi_blob,
                                  strerror(errno));
                        return false;
                }
        }
        cbm_sync();

        return true;
}

bool sd_class_remove(const BootManager *manager)
{
        if (!manager) {
                return false;
        }

        /* We call multiple syncs in case something goes wrong in removal, where we could be seeing
         * an ESP umount after */
        if (nc_file_exists(sd_class_config.vendor_dir) && !nc_rm_rf(sd_class_config.vendor_dir)) {
                LOG_FATAL("Failed to remove vendor dir: %s", strerror(errno));
                return false;
        }
        cbm_sync();

        if (nc_file_exists(sd_class_config.default_path_efi_blob) &&
            unlink(sd_class_config.default_path_efi_blob) < 0) {
                LOG_FATAL("Failed to remove %s: %s",
                          sd_class_config.default_path_efi_blob,
                          strerror(errno));
                return false;
        }
        cbm_sync();

        if (nc_file_exists(sd_class_config.loader_config) &&
            unlink(sd_class_config.loader_config) < 0) {
                LOG_FATAL("Failed to remove %s: %s",
                          sd_class_config.loader_config,
                          strerror(errno));
                return false;
        }
        cbm_sync();

        return true;
}

int sd_class_get_capabilities(__cbm_unused__ const BootManager *manager)
{
        /* Very trivial bootloader, we support UEFI/GPT only */
        return BOOTLOADER_CAP_GPT | BOOTLOADER_CAP_UEFI | BOOTLOADER_CAP_FATFS;
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
