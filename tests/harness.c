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
#include <check.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>


#include "bootman.h"
#define _BOOTMAN_INTERNAL_
#include "bootman_private.h"
#undef _BOOTMAN_INTERNAL_
#include "files.h"
#include "nica/files.h"

#include "config.h"
#include "harness.h"
#include "system_stub.h"

#define PLAYGROUND_ROOT TOP_BUILD_DIR "/tests/update_playground"

/**
 * 64-bit vs 32-bit test
 */
#if UINTPTR_MAX == 0xffffffffffffffff
#define EFI_STUB_SUFFIX "X64.EFI"
#define EFI_STUB_SUFFIX_L "x64.efi"
#else
#define EFI_STUB_SUFFIX "IA32.EFI"
#define EFI_STUB_SUFFIX_L "ia32.efi"
#endif

/**
 * i.e. $root/boot
 */
#define BOOT_FULL PLAYGROUND_ROOT "/" BOOT_DIRECTORY

#define EFI_START BOOT_FULL "/efi"
/**
 * i.e. $dir/EFI/Boot/BOOTX64.EFI
 */
#define EFI_STUB_MAIN BOOT_FULL "/efi/BOOT/BOOT" EFI_STUB_SUFFIX

/**
 * Places that need to exist..
 */

/**
 * Systemd support, including shim-systemd two-stage
 */
#if defined(HAVE_SYSTEMD_BOOT)
#if defined(HAVE_SHIM_SYSTEMD_BOOT)
#define ESP_BOOT_DIR BOOT_FULL "/efi/" KERNEL_NAMESPACE
#define ESP_BOOT_STUB ESP_BOOT_DIR "/bootloader" EFI_STUB_SUFFIX_L
#define SHIM_BOOT_COPY_DIR PLAYGROUND_ROOT "/usr/lib/shim"
#else
#define ESP_BOOT_DIR EFI_START "/systemd"
#define ESP_BOOT_STUB ESP_BOOT_DIR "/systemd-boot" EFI_STUB_SUFFIX_L
#endif /* HAVE_SHIM_SYSTEMD_BOOT */

#define BOOT_COPY_TARGET PLAYGROUND_ROOT "/usr/lib/systemd/boot/efi/systemd-boot" EFI_STUB_SUFFIX_L
#define BOOT_COPY_DIR PLAYGROUND_ROOT "/usr/lib/systemd/boot/efi"

#else
#error No known ESP loader
#endif

/* $moduledir/$i */
static const char *module_dirs[] = { "build", "source", "extra",   "kernel", "updates",
                                     "arch",  "crypto", "drivers", "fs",     "lib",
                                     "mm",    "net",    "sound" };
/* $moduledir/kernel/$i */
static const char *module_modules[] = { "arch/dummy.ko", "crypto/dummy.ko", "drivers/dummy.ko",
                                        "fs/dummy.ko",   "lib/dummy.ko",    "mm/dummy.ko",
                                        "net/dummy.ko",  "sound/dummy.ko" };

/**
 * Wrap nc_file_exists and spam to stderr
 */
__cbm_inline__ static inline bool noisy_file_exists(const char *p)
{
        bool b = nc_file_exists(p);
        if (b) {
                return b;
        }
        fprintf(stderr, "missing-file: %s does not exist\n", p);
        return b;
}

void confirm_bootloader(void)
{
#if !defined(HAVE_SHIM_SYSTEMD_BOOT)
        fail_if(!noisy_file_exists(EFI_STUB_MAIN), "Main EFI stub missing");
#endif
        fail_if(!noisy_file_exists(ESP_BOOT_DIR), "ESP target directory missing");
        fail_if(!noisy_file_exists(ESP_BOOT_STUB), "ESP target stub missing");
}

bool confirm_bootloader_match(bool check_default)
{
        if (check_default) {
                if (!cbm_files_match(BOOT_COPY_TARGET, EFI_STUB_MAIN)) {
                        fprintf(stderr, "EFI_STUB_MAIN doesn't match the source\n");
                        return false;
                }
        }
        if (!cbm_files_match(BOOT_COPY_TARGET, ESP_BOOT_STUB)) {
                fprintf(stderr, "ESP_BOOT_STUB(vendor) doesn't match the source\n");
                return false;
        }
        return true;
}

bool set_kernel_default(PlaygroundKernel *kernel)
{
        if (!kernel) {
                return false;
        }
        autofree(char) *link_source = NULL;
        autofree(char) *link_target = NULL;

        link_source = string_printf("%s.%s.%s-%d",
                                    KERNEL_NAMESPACE,
                                    kernel->ktype,
                                    kernel->version,
                                    kernel->release);

        /* i.e. default-kvm */
        link_target =
            string_printf("%s/%s/default-%s", PLAYGROUND_ROOT, KERNEL_DIRECTORY, kernel->ktype);

        /* Purge old link */
        if (nc_file_exists(link_target) && unlink(link_target) < 0) {
                fprintf(stderr, "Cannot remove default symlink: %s\n", strerror(errno));
                return false;
        }

        /* Set new link */
        if (symlink(link_source, link_target) < 0) {
                fprintf(stderr, "Failed to create default-%s symlink\n", kernel->ktype);
                return false;
        }
        return true;
}

bool set_kernel_booted(PlaygroundKernel *kernel, bool did_boot)
{
        /*  /var/lib/kernel/k_booted_4.4.0-120.lts  */
        if (!kernel) {
                return false;
        }
        autofree(char) *p = NULL;

        p = string_printf("%s/var/lib/kernel/k_booted_%s-%d.%s",
                          PLAYGROUND_ROOT,
                          kernel->version,
                          kernel->release,
                          kernel->ktype);

        if (!did_boot) {
                if (nc_file_exists(p) && unlink(p) < 0) {
                        fprintf(stderr, "Cannot unlink %s: %s\n", p, strerror(errno));
                        return false;
                }
                return true;
        }
        if (!file_set_text((const char *)p, "clr-boot-manager file\n")) {
                fprintf(stderr, "Cannot write k_boot file %s: %s\n", p, strerror(errno));
                return false;
        }
        return true;
}

bool push_kernel_update(PlaygroundConfig *config, PlaygroundKernel *kernel)
{
        autofree(char) *kfile = NULL;
        autofree(char) *cmdfile = NULL;
        autofree(char) *conffile = NULL;
        autofree(char) *link_source = NULL;
        autofree(char) *link_target = NULL;
        autofree(char) *initrd_file = NULL;

        /* $root/$kerneldir/$prefix.native.4.2.1-137 */
        kfile = string_printf("%s/%s/%s.%s.%s-%d",
                              PLAYGROUND_ROOT,
                              KERNEL_DIRECTORY,
                              KERNEL_NAMESPACE,
                              kernel->ktype,
                              kernel->version,
                              kernel->release);

        /* $root/$kerneldir/initrd-$prefix.native.4.2.1-137 */
        initrd_file = string_printf("%s/%s/initrd-%s.%s.%s-%d",
                                    PLAYGROUND_ROOT,
                                    KERNEL_DIRECTORY,
                                    KERNEL_NAMESPACE,
                                    kernel->ktype,
                                    kernel->version,
                                    kernel->release);

        /* $root/$kerneldir/cmdline-$version-$release.$type */
        cmdfile = string_printf("%s/%s/cmdline-%s-%d.%s",
                                PLAYGROUND_ROOT,
                                KERNEL_DIRECTORY,
                                kernel->version,
                                kernel->release,
                                kernel->ktype);

        /* $root/$kerneldir/config-$version-$release.$type */
        conffile = string_printf("%s/%s/config-%s-%d.%s",
                                 PLAYGROUND_ROOT,
                                 KERNEL_DIRECTORY,
                                 kernel->version,
                                 kernel->release,
                                 kernel->ktype);

        /* Write the "kernel blob" */
        if (!file_set_text((const char *)kfile, (char *)kernel->version)) {
                return false;
        }
        /* Write the "cmdline file" */
        if (!file_set_text((const char *)cmdfile, "cmdline-for-kernel")) {
                return false;
        }
        /* Write the "config file" */
        if (!file_set_text((const char *)conffile, (char *)kernel->version)) {
                return false;
        }
        /* Write the initrd file */
        if (!file_set_text((const char *)initrd_file, (char *)kernel->version)) {
                return false;
        }

        /* Nothing more to do */
        if (config->disable_modules) {
                return true;
        }

        /* Create all the dirs .. */
        for (size_t i = 0; i < ARRAY_SIZE(module_dirs); i++) {
                const char *p = module_dirs[i];
                autofree(char) *t = NULL;

                /* $root/$moduledir/$version-$rel/$p */
                t = string_printf("%s/%s/%s-%d/%s",
                                  PLAYGROUND_ROOT,
                                  KERNEL_MODULES_DIRECTORY,
                                  kernel->version,
                                  kernel->release,
                                  p);
                if (!nc_mkdir_p(t, 00755)) {
                        fprintf(stderr, "Failed to mkdir: %s %s\n", p, strerror(errno));
                        return false;
                }
        }
        /* Create all the .ko's .. */
        for (size_t i = 0; i < ARRAY_SIZE(module_modules); i++) {
                const char *p = module_modules[i];
                autofree(char) *t = NULL;

                /* $root/$moduledir/$version-$rel/$p */
                t = string_printf("%s/%s/%s-%d/%s",
                                  PLAYGROUND_ROOT,
                                  KERNEL_MODULES_DIRECTORY,
                                  kernel->version,
                                  kernel->release,
                                  p);
                if (!file_set_text((const char *)t, (char *)kernel->version)) {
                        fprintf(stderr, "Failed to touch: %s %s\n", t, strerror(errno));
                        return false;
                }
        }
        return true;
}

bool push_bootloader_update(int revision)
{
        autofree(char) *text = NULL;

        text = string_printf("faux-bootloader-revision: %d\n", revision);

        if (!nc_file_exists(BOOT_COPY_DIR)) {
                if (!nc_mkdir_p(BOOT_COPY_DIR, 00755)) {
                        fprintf(stderr, "Failed to mkdir %s: %s\n", BOOT_COPY_DIR, strerror(errno));
                        return false;
                }
        }
        if (!file_set_text(BOOT_COPY_TARGET, text)) {
                fprintf(stderr, "Failed to update bootloader: %s\n", strerror(errno));
                return false;
        }
#if defined(HAVE_SHIM_SYSTEMD_BOOT)
        {
                int i;
                char path[PATH_MAX];
                char *files[3] = { "fb", "mm", "shim" };
                if (!nc_file_exists(SHIM_BOOT_COPY_DIR) && !nc_mkdir_p(SHIM_BOOT_COPY_DIR, 0755)) {
                        return false;
                }
                for (i = 0; i < 3; i++) {
                        snprintf(path,
                                 PATH_MAX,
                                 "%s/%s" EFI_STUB_SUFFIX_L,
                                 SHIM_BOOT_COPY_DIR,
                                 files[i]);
                        if (!file_set_text(path, text)) {
                                return false;
                        }
                }
        }
#endif /* HAVE_SHIM_SYSTEMD_BOOT */
        return true;
}

BootManager *prepare_playground(PlaygroundConfig *config)
{
        assert(config != NULL);

        BootManager *m = NULL;

        m = boot_manager_new();
        if (!m) {
                return NULL;
        }

        /* Purge last runs */
        if (nc_file_exists(PLAYGROUND_ROOT)) {
                if (!nc_rm_rf(PLAYGROUND_ROOT)) {
                        fprintf(stderr, "Failed to rm_rf: %s\n", strerror(errno));
                        goto fail;
                }
        }
        /* Now create fresh tree */
        if (!nc_mkdir_p(PLAYGROUND_ROOT, 00755)) {
                fprintf(stderr, "Cannot create playground root: %s\n", strerror(errno));
                goto fail;
        }

        if (!nc_mkdir_p(PLAYGROUND_ROOT "/var/lib/kernel", 00755)) {
                fprintf(stderr, "Cannot set up the booted dir: %s\n", strerror(errno));
                goto fail;
        }

        /* Construct /etc directory for os-release */
        if (!nc_mkdir_p(PLAYGROUND_ROOT "/" SYSCONFDIR, 00755)) {
                goto fail;
        }

        if (!file_set_text(PLAYGROUND_ROOT "/" SYSCONFDIR "/os-release",
                           "PRETTY_NAME=\"clr-boot-manager testing\"\n")) {
                goto fail;
        }

        /* Initialise the root devfs/sysfs */
        if (config->uefi) {
                set_test_system_uefi();
        } else {
                set_test_system_legacy();
        }

        /* Construct kernel config directory */
        if (!nc_mkdir_p(PLAYGROUND_ROOT "/" KERNEL_CONF_DIRECTORY, 00755)) {
                goto fail;
        }

        /* plant extlinux/grub before boot_manager_set_prefix */
        if (!nc_mkdir_p(PLAYGROUND_ROOT "/usr/bin", 00755)) {
                goto fail;
        }
        if (!creat(PLAYGROUND_ROOT "/usr/bin/extlinux", 00755)) {
                goto fail;
        }
        if (!creat(PLAYGROUND_ROOT "/usr/bin/syslinux", 00755)) {
                goto fail;
        }
        if (!nc_mkdir_p(PLAYGROUND_ROOT "/usr/sbin", 00755)) {
                goto fail;
        }
        if (!creat(PLAYGROUND_ROOT "/usr/sbin/grub-mkconfig", 00755)) {
                goto fail;
        }

        if (!boot_manager_set_prefix(m, PLAYGROUND_ROOT)) {
                goto fail;
        }

        /* Construct the root kernels directory */
        if (!nc_mkdir_p(PLAYGROUND_ROOT "/" KERNEL_DIRECTORY, 00755)) {
                goto fail;
        }

        /* Construct the root initrd no deps directory */
        if (!nc_mkdir_p(PLAYGROUND_ROOT "/" INITRD_DIRECTORY, 00755)) {
                goto fail;
        }

        if (!config->disable_modules) {
                /* Construct the root kernel modules directory */
                if (!nc_mkdir_p(PLAYGROUND_ROOT "/" KERNEL_MODULES_DIRECTORY, 00755)) {
                        goto fail;
                }
        }

        if (!nc_mkdir_p(PLAYGROUND_ROOT "/" BOOT_DIRECTORY, 00755)) {
                goto fail;
        }

        if (!boot_manager_set_boot_dir(m, PLAYGROUND_ROOT "/" BOOT_DIRECTORY)) {
                goto fail;
        }

        /* Copy the bootloader bits into the tree */
        if (config->uefi) {
                if (!push_bootloader_update(0)) {
                        goto fail;
                }
                /* Create dir *after* init to simulate ESP mount behaviour with
                 * a different-case boot tree on the ESP */
                fail_if(!nc_mkdir_p(EFI_START "/BOOT", 00755), "Failed to create boot structure");
        }

        /* Insert all intitial kernels into the root */
        for (size_t i = 0; i < config->n_kernels; i++) {
                PlaygroundKernel *k = &(config->initial_kernels[i]);
                if (!push_kernel_update(config, k)) {
                        goto fail;
                }
                /* Not default so skip */
                if (!k->default_for_type) {
                        continue;
                }
                if (!set_kernel_default(k)) {
                        goto fail;
                }
        }

        if (config->uts_name && !boot_manager_set_uname(m, config->uts_name)) {
                fprintf(stderr, "Cannot set given uname of %s\n", config->uts_name);
                goto fail;
        }

        return m;
fail:
        boot_manager_free(m);
        return NULL;
}

int kernel_installed_files_count(BootManager *manager, PlaygroundKernel *kernel)
{
        assert(manager);
        assert(kernel);

        autofree(char) *conf_file = NULL;
        autofree(char) *kernel_blob = NULL;
        autofree(char) *kernel_blob_legacy = NULL;
        autofree(char) *initrd_file = NULL;
        autofree(char) *initrd_file_legacy = NULL;
        /* where the kernel files are expected to be found on the ESP */
        const char *esp_path = manager->bootloader->get_kernel_destination
                                   ? manager->bootloader->get_kernel_destination(manager)
                                   : "efi/" KERNEL_NAMESPACE;
        const char *vendor = NULL;
        int file_count = 0;

        vendor = boot_manager_get_vendor_prefix(manager);

        kernel_blob = string_printf("%s/%s/kernel-%s.%s.%s-%d",
                                    BOOT_FULL,
                                    esp_path,
                                    KERNEL_NAMESPACE,
                                    kernel->ktype,
                                    kernel->version,
                                    kernel->release);

        /* Old name, pre namespace change */
        kernel_blob_legacy = string_printf("%s/%s.%s.%s-%d",
                                           BOOT_FULL,
                                           KERNEL_NAMESPACE,
                                           kernel->ktype,
                                           kernel->version,
                                           kernel->release);

        initrd_file = string_printf("%s/%s/initrd-%s.%s.%s-%d",
                                    BOOT_FULL,
                                    esp_path,
                                    KERNEL_NAMESPACE,
                                    kernel->ktype,
                                    kernel->version,
                                    kernel->release);

        /* Old name, pre namespace change */
        initrd_file_legacy = string_printf("%s/initrd-%s.%s.%s-%d",
                                           BOOT_FULL,
                                           KERNEL_NAMESPACE,
                                           kernel->ktype,
                                           kernel->version,
                                           kernel->release);

        conf_file = string_printf("%s/loader/entries/%s-%s-%s-%d.conf",
                                  BOOT_FULL,
                                  vendor,
                                  kernel->ktype,
                                  kernel->version,
                                  kernel->release);

        if (nc_file_exists(conf_file)) {
                ++file_count;
        }

        if (kernel->legacy_name) {
                if (nc_file_exists(kernel_blob_legacy)) {
                        ++file_count;
                }
                if (nc_file_exists(initrd_file_legacy)) {
                        ++file_count;
                }
        } else {
                if (nc_file_exists(kernel_blob)) {
                        ++file_count;
                }
                if (nc_file_exists(initrd_file)) {
                        ++file_count;
                }
        }
        return file_count;
}

bool confirm_kernel_installed(BootManager *manager, PlaygroundConfig *config,
                              PlaygroundKernel *kernel)
{
        return kernel_installed_files_count(manager, kernel) == (config->uefi ? 3 : 2);
}

bool confirm_kernel_uninstalled(BootManager *manager, PlaygroundKernel *kernel)
{
        return kernel_installed_files_count(manager, kernel) == 0;
}

bool create_timeout_conf(void)
{
        autofree(char) *timeout_conf = NULL;

        timeout_conf = string_printf("%s/%s/timeout", PLAYGROUND_ROOT, KERNEL_CONF_DIRECTORY);
        if (!file_set_text((const char *)timeout_conf, (char *)"5")) {
                fprintf(stderr, "Failed to touch: %s %s\n", timeout_conf, strerror(errno));
                return false;
        }
        return true;
}

void set_test_system_uefi(void)
{
        autofree(char) *root = NULL;
        autofree(char) *lfile = NULL;
        autofree(char) *dfile = NULL;
        autofree(char) *ddir = NULL;

        /* Create fake UEFI variables */
        root = string_printf("%s/firmware/efi/efivars", cbm_system_get_sysfs_path());
        nc_mkdir_p(root, 00755);

        /* Create fake LoaderDevicePartUUID */
        lfile = string_printf("%s/LoaderDevicePartUUID-dummyRoot", root);
        if (!file_set_text(lfile, "E90F44B5-BB8A-41AF-B680-B0BF5B0F2A65")) {
                DECLARE_OOM();
                abort();
        }

        /* Create /dev/disk/by-partuuid portions */
        ddir = string_printf("%s/disk/by-partuuid", cbm_system_get_devfs_path());

        dfile = string_printf("%s/e90f44b5-bb8a-41af-b680-b0bf5b0f2a65", ddir);

        /* commit them to disk */
        nc_mkdir_p(ddir, 00755);
        if (!file_set_text(dfile, "clr-boot-manager UEFI testing")) {
                DECLARE_OOM();
                abort();
        }
}

void set_test_system_legacy(void)
{
        autofree(char) *ddir = NULL;
        autofree(char) *diskdir = NULL;
        autofree(char) *diskfile = NULL;
        autofree(char) *diskdir_uuid = NULL;
        autofree(char) *diskfile_uuid = NULL;
        autofree(char) *dfile = NULL;
        autofree(char) *dlink = NULL;
        const char *devfs_path = cbm_system_get_devfs_path();

        /* dev tree */
        ddir = string_printf("%s/block", devfs_path);

        /* dev/block link */
        dlink = string_printf("%s/block/%u:%u", devfs_path, 8, 8);

        /* "real" dev file for realpath()ing */
        dfile = string_printf("%s/leRootDevice", devfs_path);

        nc_mkdir_p(ddir, 00755);
        if (!file_set_text(dfile, "le-root-device")) {
                DECLARE_OOM();
                abort();
        }

        if (symlink("../leRootDevice", dlink) != 0) {
                fprintf(stderr, "Cannot create symlink: %s", strerror(errno));
                abort();
        }

        /* Create /dev/disk/by-partuuid portions */
        diskdir = string_printf("%s/disk/by-partuuid", cbm_system_get_devfs_path());
        diskfile = string_printf("%s/%s", diskdir, "Test-PartUUID");

        /* Create /dev/disk/by-uuid portions */
        diskdir_uuid = string_printf("%s/disk/by-uuid", cbm_system_get_devfs_path());
        diskfile_uuid = string_printf("%s/%s", diskdir, "Test-UUID");

        nc_mkdir_p(diskdir, 00755);
        if (!file_set_text(diskfile, "clr-boot-manager Legacy testing")) {
                DECLARE_OOM();
                abort();
        }

        nc_mkdir_p(diskdir_uuid, 00755);
        if (!file_set_text(diskfile_uuid, "clr-boot-manager Legacy testing")) {
                DECLARE_OOM();
                abort();
        }
}

bool check_freestanding_initrds_available(BootManager *manager, const char *file_name)
{
        autofree(char) *name = string_printf("freestanding-%s", file_name);
        return nc_hashmap_get(manager->initrd_freestanding, name);
}

bool check_initrd_file_exist(BootManager *manager, const char *file_name)
{
        autofree(char) *initrd_file = NULL;
        struct stat st = { 0 };
        /* where the kernel files are expected to be found on the ESP */
        const char *esp_path = manager->bootloader->get_kernel_destination
                                   ? manager->bootloader->get_kernel_destination(manager)
                                   : "efi/" KERNEL_NAMESPACE;

        initrd_file = string_printf("%s/%s/freestanding-%s",
                                    BOOT_FULL,
                                    esp_path,
                                    file_name);

        return stat(initrd_file, &st) == 0;
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
