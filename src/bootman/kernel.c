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
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>

#include "bootman.h"
#include "bootman_private.h"
#include "cmdline.h"
#include "files.h"
#include "log.h"
#include "nica/files.h"

#include "config.h"

/**
 * Determine the applicable kboot file
 */
__cbm_inline__ static inline char *boot_manager_get_kboot_file(BootManager *self, Kernel *k)
{
        char *p = NULL;
        /* /var/lib/kernel/k_booted_4.4.0-120.lts - new */
        p = string_printf("%s/var/lib/kernel/k_booted_%s-%d.%s",
                          self->sysconfig->prefix,
                          k->meta.version,
                          k->meta.release,
                          k->meta.ktype);
        return p;
}

bool boot_manager_detect_kernel_dir(char *path)
{
        autofree(char) *kernel_dir = NULL;

        kernel_dir = path ? string_printf("%s/%s", path, KERNEL_DIRECTORY) :
                string_printf("/%s", KERNEL_DIRECTORY);
        if (!nc_file_exists(kernel_dir)) {
                return false;
        }

        return true;
}

Kernel *boot_manager_inspect_kernel(BootManager *self, char *path)
{
        Kernel *kern = NULL;
        autofree(char) *cmp = NULL;
        char type[32] = { 0 };
        char version[16] = { 0 };
        int release = 0;
        autofree(char) *parent = NULL;
        autofree(char) *cmdline = NULL;
        autofree(char) *module_dir = NULL;
        autofree(char) *kconfig_file = NULL;
        autofree(char) *initrd_file = NULL;
        autofree(char) *user_initrd_file = NULL;
        autofree(char) *sysmap_file = NULL;
        autofree(char) *vmlinux_file = NULL;
        autofree(char) *headers_dir = NULL;
        ssize_t r = 0;
        char *bcp = NULL;

        if (!self || !path) {
                return NULL;
        }

        cmp = strdup(path);
        if (!cmp) {
                return NULL;
        }
        bcp = basename(cmp);

        /* org.clearlinux.kvm.4.2.1-121 */
        r = sscanf(bcp, KERNEL_NAMESPACE ".%31[^.].%15[^-]-%d", type, version, &release);
        if (r != 3) {
                return NULL;
        }

        parent = cbm_get_file_parent(path);
        cmdline = string_printf("%s/cmdline-%s-%d.%s", parent, version, release, type);
        kconfig_file = string_printf("%s/config-%s-%d.%s", parent, version, release, type);
        sysmap_file = string_printf("%s/System.map-%s-%d.%s", parent, version, release, type);
        vmlinux_file = string_printf("%s/vmlinux-%s-%d.%s", parent, version, release, type);

        /* i.e. /usr/lib/kernel/initrd-org.clearlinux.lts.4.9.1-1  */
        initrd_file = string_printf("%s/initrd-%s.%s.%s-%d",
                                    parent,
                                    KERNEL_NAMESPACE,
                                    type,
                                    version,
                                    release);

        /* i.e. /etc/kernel/initrd-org.clearlinux.lts.4.9.1-1  */
        user_initrd_file = string_printf("%s/initrd-%s.%s.%s-%d",
                                         KERNEL_CONF_DIRECTORY,
                                         KERNEL_NAMESPACE,
                                         type,
                                         version,
                                         release);

        /* TODO: We may actually be uninstalling a partially flopped kernel,
         * so validity of existing kernels may be questionable
         * Thus, flag it, and return kernel */
        CHECK_ERR_RET_VAL(!nc_file_exists(cmdline), NULL,
                          "Valid kernel found with no cmdline: %s (expected %s)",
                          path, cmdline);

        /* Check local modules */
        module_dir = string_printf("%s/%s/%s-%d.%s",
                                   self->sysconfig->prefix,
                                   KERNEL_MODULES_DIRECTORY,
                                   version,
                                   release,
                                   type);

        /* Fallback to an older namespace */
        if (!nc_file_exists(module_dir)) {
                free(module_dir);
                module_dir = string_printf("%s/%s/%s-%d",
                                           self->sysconfig->prefix,
                                           KERNEL_MODULES_DIRECTORY,
                                           version,
                                           release);

                if (!nc_file_exists(module_dir)) {
                        LOG_WARNING("Found kernel with no modules: %s %s", path, module_dir);
                        free(module_dir);
                        module_dir = NULL;
                }
        }

        /* Check headers directory, standardised path on all distros */
        headers_dir = string_printf("%s/usr/src/linux-headers-%s-%d.%s",
                                    self->sysconfig->prefix,
                                    version,
                                    release,
                                    type);

        /* Got this far, we have a valid clear kernel */
        kern = calloc(1, sizeof(struct Kernel));
        if (!kern) {
                abort();
        }

        kern->source.path = strdup(path);
        kern->meta.bpath = strdup(bcp);
        kern->meta.version = strdup(version);
        if (module_dir) {
                kern->source.module_dir = strdup(module_dir);
        }
        kern->meta.ktype = strdup(type);
        /* Legacy path should be used by non-UEFI bootloaders */
        kern->target.legacy_path = kern->meta.bpath;

        /* New path is virtually identical to the old one with the exception of
         * a kernel- prefix */
        kern->target.path = string_printf("kernel-%s", kern->target.legacy_path);

        if (nc_file_exists(kconfig_file)) {
                kern->source.kconfig_file = strdup(kconfig_file);
                if (!kern->source.kconfig_file) {
                        DECLARE_OOM();
                        abort();
                }
        }

        if (nc_file_exists(sysmap_file)) {
                kern->source.sysmap_file = strdup(sysmap_file);
                if (!kern->source.sysmap_file) {
                        DECLARE_OOM();
                        abort();
                }
        }

        if (nc_file_exists(vmlinux_file)) {
                kern->source.vmlinux_file = strdup(vmlinux_file);
                if (!kern->source.vmlinux_file) {
                        DECLARE_OOM();
                        abort();
                }
        }

        if (nc_file_exists(headers_dir)) {
                kern->source.headers_dir = strdup(headers_dir);
                if (!kern->source.headers_dir) {
                        DECLARE_OOM();
                        abort();
                }
        }

        if (nc_file_exists(initrd_file)) {
                kern->source.initrd_file = strdup(initrd_file);
                if (!kern->source.initrd_file) {
                        DECLARE_OOM();
                        abort();
                }
        }

        if (nc_file_exists(user_initrd_file)) {
                kern->source.user_initrd_file = strdup(user_initrd_file);
                if (!kern->source.user_initrd_file) {
                        DECLARE_OOM();
                        abort();
                }
        }

        /* Target initrd is just basename'd initrd file, simpler to just
         * reprintf it than copy & basename it */
        if (kern->source.initrd_file || kern->source.user_initrd_file) {
                kern->target.initrd_path =
                    string_printf("initrd-%s.%s.%s-%d", KERNEL_NAMESPACE, type, version, release);
        }

        kern->meta.release = (int16_t)release;

        /* cmdline */
        kern->meta.cmdline = cbm_parse_cmdline_file(cmdline);
        if (!kern->meta.cmdline) {
                LOG_ERROR("Unable to load cmdline %s: %s", cmdline, strerror(errno));
                free_kernel(kern);
                return NULL;
        }

        /* Merge global cmdline if we have one */
        if (self->cmdline) {
                char *cm = string_printf("%s %s", kern->meta.cmdline, self->cmdline);
                free(kern->meta.cmdline);
                kern->meta.cmdline = cm;
        }

        cbm_parse_cmdline_removal_files_directory(self->sysconfig->prefix, kern->meta.cmdline);

        kern->source.cmdline_file = strdup(cmdline);

        /** Determine if the kernel boots */
        kern->source.kboot_file = boot_manager_get_kboot_file(self, kern);
        if (kern->source.kboot_file && nc_file_exists(kern->source.kboot_file)) {
                kern->meta.boots = true;
        }
        return kern;
}

KernelArray *boot_manager_get_kernels(BootManager *self)
{
        KernelArray *ret = NULL;
        DIR *dir = NULL;
        struct dirent *ent = NULL;
        struct stat st = { 0 };
        if (!self || !self->kernel_dir) {
                return NULL;
        }

        ret = nc_array_new();
        OOM_CHECK_RET(ret, NULL);

        dir = opendir(self->kernel_dir);
        if (!dir) {
                LOG_ERROR("Error opening %s: %s", self->kernel_dir, strerror(errno));
                nc_array_free(&ret, NULL);
                return NULL;
        }

        while ((ent = readdir(dir)) != NULL) {
                autofree(char) *path = NULL;
                Kernel *kern = NULL;

                path = string_printf("%s/%s", self->kernel_dir, ent->d_name);

                /* Some kind of broken link */
                if (lstat(path, &st) != 0) {
                        continue;
                }

                /* Regular only */
                if (!S_ISREG(st.st_mode)) {
                        continue;
                }

                /* empty files are skipped too */
                if (st.st_size == 0) {
                        continue;
                }

                /* Now see if its a kernel */
                kern = boot_manager_inspect_kernel(self, path);
                if (!kern) {
                        continue;
                }
                if (!nc_array_add(ret, kern)) {
                        DECLARE_OOM();
                        abort();
                }
        }
        closedir(dir);
        return ret;
}

void free_kernel(Kernel *t)
{
        if (!t) {
                return;
        }
        free(t->meta.bpath);
        free(t->meta.version);
        free(t->meta.cmdline);
        free(t->meta.ktype);
        free(t->source.path);
        free(t->source.module_dir);
        free(t->source.cmdline_file);
        free(t->source.headers_dir);
        free(t->source.kconfig_file);
        free(t->source.sysmap_file);
        free(t->source.vmlinux_file);
        free(t->source.kboot_file);
        free(t->source.initrd_file);
        free(t->source.user_initrd_file);
        free(t->target.initrd_path);
        free(t->target.path);
        free(t);
}

Kernel *boot_manager_get_default_for_type(BootManager *self, KernelArray *kernels, const char *type)
{
        autofree(char) *default_file = NULL;
        char linkbuf[PATH_MAX] = { 0 };

        if (!self || !kernels || !type) {
                return NULL;
        }

        default_file = string_printf("%s/default-%s", self->kernel_dir, type);

        CHECK_DBG_RET_VAL(readlink(default_file, linkbuf, sizeof(linkbuf)) < 0,
                          NULL, "Could not resolve symlink");

        for (uint16_t i = 0; i < kernels->len; i++) {
                Kernel *k = nc_array_get(kernels, i);
                if (streq(k->meta.bpath, linkbuf)) {
                        return k;
                }
        }

        return NULL;
}

static inline void kern_dup_free(void *v)
{
        NcArray *array = v;
        nc_array_free(&array, NULL);
}

NcHashmap *boot_manager_map_kernels(BootManager *self, KernelArray *kernels)
{
        if (!self || !kernels) {
                return NULL;
        }

        NcHashmap *map = NULL;

        map = nc_hashmap_new_full(nc_string_hash, nc_string_compare, free, kern_dup_free);

        for (uint16_t i = 0; i < kernels->len; i++) {
                KernelArray *r = NULL;
                Kernel *cur = nc_array_get(kernels, i);

                r = nc_hashmap_get(map, cur->meta.ktype);
                if (!r) {
                        r = nc_array_new();
                        if (!r) {
                                goto oom;
                        }
                        if (!nc_hashmap_put(map, strdup(cur->meta.ktype), r)) {
                                kern_dup_free(r);
                                goto oom;
                        }
                }
                if (!nc_array_add(r, cur)) {
                        goto oom;
                }
        }

        return map;
oom:
        DECLARE_OOM();
        nc_hashmap_free(map);
        return NULL;
}

bool cbm_parse_system_kernel(const char *inp, SystemKernel *kernel)
{
        if (!kernel || !inp) {
                return false;
        }

        /* Re-entrant, we might've mangled the kernel obj */
        memset(kernel, 0, sizeof(struct SystemKernel));

        char krelease[CBM_KELEM_LEN + 1] = { 0 };
        long release = 0;
        ssize_t len;
        char *c, *c2, *junk = NULL;

        c = strchr(inp, '-');
        if (!c) {
                return false;
        }
        if (c - inp >= CBM_KELEM_LEN) {
                return false;
        }
        if (*(c + 1) == '\0') {
                return false;
        }
        c2 = strchr(c + 1, '.');
        if (!c2) {
                return false;
        }
        /* Check length */
        if (c2 - c >= CBM_KELEM_LEN) {
                return false;
        }

        /* Copy version */
        len = c - inp;
        if (len < 1) {
                return false;
        }
        strncpy(kernel->version, inp, (size_t)len);
        kernel->version[len + 1] = '\0';

        /* Copy release */
        len = c2 - c - 1;
        if (len < 1) {
                return false;
        }
        strncpy(krelease, c + 1, (size_t)len);
        krelease[len + 1] = '\0';

        /* Sane release? */
        release = strtol(krelease, &junk, 10);
        if (junk == krelease) {
                return false;
        }
        kernel->release = (int16_t)release;

        /* Wind the type size **/
        len = 0;
        for (char *j = c2 + 1; *j; ++j) {
                ++len;
        }

        if (len < 1 || len + 1 > CBM_KELEM_LEN) {
                return false;
        }

        /* Kernel type */
        strncpy(kernel->ktype, c2 + 1, (size_t)len);
        kernel->ktype[len + 1] = '\0';

        return true;
}

const SystemKernel *boot_manager_get_system_kernel(BootManager *self)
{
        if (!self || !self->have_sys_kernel) {
                return NULL;
        }
        return (const SystemKernel *)&(self->sys_kernel);
}

Kernel *boot_manager_get_running_kernel(BootManager *self, KernelArray *kernels)
{
        if (!self || !kernels) {
                return NULL;
        }
        const SystemKernel *k = boot_manager_get_system_kernel(self);
        if (!k) {
                return NULL;
        }

        for (uint16_t i = 0; i < kernels->len; i++) {
                Kernel *cur = nc_array_get(kernels, i);
                if (streq(cur->meta.ktype, k->ktype) && streq(cur->meta.version, k->version) &&
                    cur->meta.release == k->release) {
                        return cur;
                }
        }
        return NULL;
}

Kernel *boot_manager_get_running_kernel_fallback(BootManager *self, KernelArray *kernels)
{
        if (!self || !kernels) {
                return NULL;
        }
        const SystemKernel *k = boot_manager_get_system_kernel(self);
        if (!k) {
                return NULL;
        }

        for (uint16_t i = 0; i < kernels->len; i++) {
                Kernel *cur = nc_array_get(kernels, i);
                if (streq(cur->meta.ktype, k->ktype) && cur->meta.release == k->release) {
                        return cur;
                }
        }
        return NULL;
}

Kernel *boot_manager_get_last_booted(BootManager *self, KernelArray *kernels)
{
        if (!self || !kernels) {
                return NULL;
        }
        int high_rel = -1;
        Kernel *candidate = NULL;

        for (uint16_t i = 0; i < kernels->len; i++) {
                Kernel *k = nc_array_get(kernels, i);
                if (k->meta.release < high_rel) {
                        continue;
                }
                if (!k->meta.boots) {
                        continue;
                }
                candidate = k;
                high_rel = k->meta.release;
        }
        return candidate;
}

/**
 * Older versions of clr-boot-manager would install kernels directly into the
 * root of the ESP, i.e. /$NAMESPACE*.
 * In order to provide full compliance and to enable secure boot, we since moved
 * the paths (kernel->target.path) to /EFI/$NAMESPACE/.*.
 *
 * However, during updates the old paths (kernel->target.legacy_path) may still
 * exist on disk. Thus, when we remove a kernel, or successfully install a new
 * kernel, we also check for the legacy paths that *may* exist. If so, we'll
 * remove them to complete the migration.
 *
 * It is *not fatal* for this to fail, just highly undesirable.
 */
static bool boot_manager_remove_legacy_uefi_kernel(const BootManager *manager, const Kernel *kernel)
{
        autofree(char) *base_path = NULL;
        autofree(char) *initrd_target = NULL;
        autofree(char) *kfile_target = NULL;
        bool ret = true;
        bool migrated = false;

        assert(manager != NULL);
        assert(kernel != NULL);

        /* Boot path */
        base_path = boot_manager_get_boot_dir((BootManager *)manager);

        kfile_target = string_printf("%s/%s", base_path, kernel->target.legacy_path);
        initrd_target = string_printf("%s/%s", base_path, kernel->target.initrd_path);

        /* Remove old kernel */
        if (nc_file_exists(kfile_target)) {
                if (unlink(kfile_target) < 0) {
                        LOG_ERROR("Failed to remove legacy-path UEFI kernel %s: %s",
                                  kfile_target,
                                  strerror(errno));
                        ret = false;
                } else {
                        migrated = true;
                }
        }

        /* Remove old initrd */
        if (nc_file_exists(initrd_target)) {
                if (unlink(initrd_target) < 0) {
                        LOG_ERROR("Failed to remove legacy-path UEFI initrd %s: %s",
                                  initrd_target,
                                  strerror(errno));
                        ret = false;
                } else {
                        migrated = true;
                }
        }

        if (migrated) {
                LOG_SUCCESS("Migrated '%s' to new namespace '%s'",
                            kernel->target.legacy_path,
                            kernel->target.path);
        }

        return ret;
}

/**
 * Internal function to install the kernel blob itself
 */
bool boot_manager_install_kernel_internal(const BootManager *manager, const Kernel *kernel)
{
        autofree(char) *kfile_target = NULL;
        autofree(char) *base_path = NULL;
        autofree(char) *initrd_target = NULL;
        const char *initrd_source = NULL;
        bool is_uefi = ((manager->bootloader->get_capabilities(manager) & BOOTLOADER_CAP_UEFI) ==
                        BOOTLOADER_CAP_UEFI);
        const char *efi_boot_dir =
            is_uefi ? manager->bootloader->get_kernel_destination(manager) : NULL;

        assert(manager != NULL);
        assert(kernel != NULL);

        if (is_uefi && !efi_boot_dir) {
                return false;
        }

        /* Boot path */
        base_path = boot_manager_get_boot_dir((BootManager *)manager);
        OOM_CHECK_RET(base_path, false);

        /* for UEFI, the kernel location is prefixed with efi_boot_dir which is
         * guaranteed to start with '/' since it's its absolute path on ESP. */
        kfile_target = string_printf("%s%s/%s",
                                     base_path,
                                     (is_uefi ? efi_boot_dir : ""),
                                     (is_uefi ? kernel->target.path : kernel->target.legacy_path));

        /* Now copy the kernel file to it's new location */
        if (!cbm_files_match(kernel->source.path, kfile_target)) {
                if (!copy_file_atomic(kernel->source.path, kfile_target, 00644)) {
                        LOG_FATAL("Failed to install kernel %s: %s", kfile_target, strerror(errno));
                        return false;
                }
        }

        /* Install user initrd if it exists, otherwise system initrd */
        if (kernel->source.user_initrd_file) {
                initrd_source = kernel->source.user_initrd_file;
        } else if (kernel->source.initrd_file) {
                initrd_source = kernel->source.initrd_file;
        } else {
                /* No initrd file for this kernel */
                return true;
        }

        initrd_target = string_printf("%s%s/%s",
                                      base_path,
                                      (is_uefi ? efi_boot_dir : ""),
                                      kernel->target.initrd_path);

        if (!cbm_files_match(initrd_source, initrd_target)) {
                if (!copy_file_atomic(initrd_source, initrd_target, 00644)) {
                        LOG_FATAL("Failed to install initrd %s: %s",
                                  initrd_target,
                                  strerror(errno));
                        return false;
                }
        }

        /* Our portion is complete, remove any legacy uefi bits we might have
         * from previous runs, and then continue and let the bootloader configure
         * as appropriate.
         */
        if (is_uefi && !boot_manager_remove_legacy_uefi_kernel(manager, kernel)) {
                LOG_WARNING("Failed to remove legacy kernel on ESP: %s",
                            kernel->target.legacy_path);
        }

        return true;
}

/**
 * Internal function to remove the kernel blob itself
 */
bool boot_manager_remove_kernel_internal(const BootManager *manager, const Kernel *kernel)
{
        autofree(char) *kfile_target = NULL;
        autofree(char) *base_path = NULL;
        autofree(char) *initrd_target = NULL;
        bool is_uefi = ((manager->bootloader->get_capabilities(manager) & BOOTLOADER_CAP_UEFI) ==
                        BOOTLOADER_CAP_UEFI);
        const char *efi_boot_dir =
            is_uefi ? manager->bootloader->get_kernel_destination(manager) : NULL;

        assert(manager != NULL);
        assert(kernel != NULL);

        /* if it's UEFI, then bootloader->get_kernel_dst() must return a value. */
        if (is_uefi && !efi_boot_dir) {
                return false;
        }

        /* Boot path */
        base_path = boot_manager_get_boot_dir((BootManager *)manager);
        OOM_CHECK_RET(base_path, false);

        /* Remove old blobs */
        kfile_target = string_printf("%s%s/%s",
                                     base_path,
                                     (is_uefi ? efi_boot_dir : ""),
                                     (is_uefi ? kernel->target.path : kernel->target.legacy_path));

        if (kernel->source.initrd_file) {
                initrd_target = string_printf("%s%s/%s",
                                              base_path,
                                              (is_uefi ? efi_boot_dir : ""),
                                              kernel->target.initrd_path);
        }

        /* Remove the kernel from the ESP */
        if (nc_file_exists(kfile_target) && unlink(kfile_target) < 0) {
                LOG_ERROR("Failed to remove kernel %s: %s", kfile_target, strerror(errno));
        } else {
                cbm_sync();
        }

        /* Purge the kernel modules from disk */
        if (kernel->source.module_dir && nc_file_exists(kernel->source.module_dir)) {
                if (!nc_rm_rf(kernel->source.module_dir)) {
                        LOG_ERROR("Failed to remove module dir (-rf) %s: %s",
                                  kernel->source.module_dir,
                                  strerror(errno));
                } else {
                        cbm_sync();
                }
        }

        /* Purge the kernel headers from disk */
        if (kernel->source.headers_dir && nc_file_exists(kernel->source.headers_dir)) {
                if (!nc_rm_rf(kernel->source.headers_dir)) {
                        LOG_ERROR("Failed to remove headers dir (-rf) %s: %s",
                                  kernel->source.module_dir,
                                  strerror(errno));
                } else {
                        cbm_sync();
                }
        }

        if (kernel->source.cmdline_file && nc_file_exists(kernel->source.cmdline_file)) {
                if (unlink(kernel->source.cmdline_file) < 0) {
                        LOG_ERROR("Failed to remove cmdline file %s: %s",
                                  kernel->source.cmdline_file,
                                  strerror(errno));
                }
        }
        if (kernel->source.kconfig_file && nc_file_exists(kernel->source.kconfig_file)) {
                if (unlink(kernel->source.kconfig_file) < 0) {
                        LOG_ERROR("Failed to remove kconfig file %s: %s",
                                  kernel->source.kconfig_file,
                                  strerror(errno));
                }
        }
        if (kernel->source.sysmap_file && nc_file_exists(kernel->source.sysmap_file)) {
                if (unlink(kernel->source.sysmap_file) < 0) {
                        LOG_ERROR("Failed to remove System.map file %s: %s",
                                  kernel->source.sysmap_file,
                                  strerror(errno));
                }
        }
        if (kernel->source.vmlinux_file && nc_file_exists(kernel->source.vmlinux_file)) {
                if (unlink(kernel->source.vmlinux_file) < 0) {
                        LOG_ERROR("Failed to remove vmlinux file %s: %s",
                                  kernel->source.vmlinux_file,
                                  strerror(errno));
                }
        }
        if (kernel->source.kboot_file && nc_file_exists(kernel->source.kboot_file)) {
                if (unlink(kernel->source.kboot_file) < 0) {
                        LOG_ERROR("Failed to remove kboot file %s: %s",
                                  kernel->source.kboot_file,
                                  strerror(errno));
                }
        }

        if (kernel->source.initrd_file) {
                if (nc_file_exists(kernel->source.initrd_file) &&
                    unlink(kernel->source.initrd_file) < 0) {
                        LOG_ERROR("Failed to remove initrd file %s: %s",
                                  kernel->source.initrd_file,
                                  strerror(errno));
                }
                if (nc_file_exists(initrd_target) && unlink(initrd_target) < 0) {
                        LOG_ERROR("Failed to remove initrd blob %s: %s",
                                  initrd_target,
                                  strerror(errno));
                }
        }

        /* Lastly, remove the source */
        if (unlink(kernel->source.path) < 0) {
                LOG_ERROR("Failed to remove kernel blob %s: %s",
                          kernel->source.path,
                          strerror(errno));
                return false;
        }

        /* Our portion is complete, remove any legacy uefi bits we might have
         * from previous runs.
         */
        if (is_uefi && !boot_manager_remove_legacy_uefi_kernel(manager, kernel)) {
                LOG_WARNING("Failed to remove legacy kernel on ESP: %s",
                            kernel->target.legacy_path);
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
