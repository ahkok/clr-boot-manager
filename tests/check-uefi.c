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
#include <check.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "bootloader.h"
#include "bootman.h"
#include "config.h"
#include "files.h"
#include "log.h"
#include "nica/array.h"
#include "nica/files.h"
#include "util.h"
#include "writer.h"

#include "blkid-harness.h"
#include "harness.h"
#include "system-harness.h"

#define PLAYGROUND_ROOT TOP_BUILD_DIR "/tests/update_playground"
#define BOOT_FULL PLAYGROUND_ROOT "/" BOOT_DIRECTORY

static PlaygroundKernel uefi_kernels[] = { { "4.2.1", "kvm", 121, false, false },
                                           { "4.2.3", "kvm", 124, true, false },
                                           { "4.2.1", "native", 137, false, false },
                                           { "4.2.3", "native", 138, true, false },
                                           { "4.2.1", "dash-test", 1, true, false } };

static PlaygroundConfig uefi_config = { "4.2.1-121.kvm",
                                        uefi_kernels,
                                        ARRAY_SIZE(uefi_kernels),
                                        .uefi = true };

static PlaygroundConfig uefi_config_dash_test = { "4.2.1-1.dash-test",
                                                  uefi_kernels,
                                                  ARRAY_SIZE(uefi_kernels),
                                                  .uefi = true };

static PlaygroundConfig uefi_config_no_modules = { "4.2.1-121.kvm",
                                                   uefi_kernels,
                                                   ARRAY_SIZE(uefi_kernels),
                                                   .uefi = true,
                                                   .disable_modules = true };

#define PLAYGROUND_ROOT TOP_BUILD_DIR "/tests/update_playground"

START_TEST(bootman_uefi_get_boot_device)
{
        autofree(char) *boot_device = NULL;
        autofree(BootManager) *m = NULL;
        autofree(char) *exp = NULL;

        /* Ensure cleanup */
        m = prepare_playground(&uefi_config);

        boot_device = get_legacy_boot_device(PLAYGROUND_ROOT);
        fail_if(boot_device != NULL, "Found incorrect legacy device for UEFI Boot");

        boot_device = get_boot_device();
        fail_if(!boot_device, "Failed to determine UEFI boot device");

        exp = string_printf("%s/dev/disk/by-partuuid/e90f44b5-bb8a-41af-b680-b0bf5b0f2a65",
                            PLAYGROUND_ROOT);
        fail_if(!streq(exp, boot_device), "Boot device does not match expected result");
}
END_TEST

static void bootman_uefi_image_shared(PlaygroundConfig *config)
{
        autofree(BootManager) *m = NULL;
        m = prepare_playground(config);
        fail_if(!m, "Failed to prepare update playground");

        /* Validate image install */
        fail_if(!boot_manager_update(m), "Failed to update image");
}

START_TEST(bootman_uefi_image_modules)
{
        bootman_uefi_image_shared(&uefi_config);
}
END_TEST

START_TEST(bootman_uefi_image_no_modules)
{
        bootman_uefi_image_shared(&uefi_config_no_modules);
}
END_TEST

static void bootman_uefi_native_shared(PlaygroundConfig *config)
{
        autofree(BootManager) *m = NULL;

        m = prepare_playground(config);
        fail_if(!m, "Failed to prepare update playground");

        fail_if(!set_kernel_booted(&uefi_kernels[1], true), "Failed to set kernel as booted");

        fail_if(!boot_manager_update(m), "Failed to update in native mode");

        /* Latest kernel for us */
        fail_if(!confirm_kernel_installed(m, &uefi_config, &(uefi_kernels[0])),
                "Newest kernel not installed");

        /* Running kernel */
        fail_if(!confirm_kernel_installed(m, &uefi_config, &(uefi_kernels[1])),
                "Newest kernel not installed");

        /* This guy isn't supposed to be kept around now */
        fail_if(!confirm_kernel_uninstalled(m, &(uefi_kernels[2])),
                "Uninteresting kernel shouldn't be kept around.");
}

START_TEST(bootman_uefi_native_modules)
{
        bootman_uefi_native_shared(&uefi_config);
}
END_TEST

START_TEST(bootman_uefi_native_no_modules)
{
        bootman_uefi_native_shared(&uefi_config_no_modules);
}
END_TEST

/**
 * This test is designed to perform a system update to a new kernel, when the
 * current kernel cannot be detected. This ensures we can perform a transition
 * from a non cbm-managed distro to a cbm-managed one.
 *
 * Scenario:
 *
 *      - Unknown running kernel
 *      - One initial new kernel from the first update. Has never rebooted.
 *      - Update: New kernel should be installed
 *      - "Reboot", then verify running = only kernel on system
 */
START_TEST(bootman_uefi_update_from_unknown)
{
        autofree(BootManager) *m = NULL;
        PlaygroundKernel kernels[] = { { "4.2.1", "kvm", 121, true, false } };
        PlaygroundConfig config = { "4.2.1-121.kvm", kernels, 1, true };
        autofree(KernelArray) *pre_kernels = NULL;
        autofree(KernelArray) *post_kernels = NULL;
        Kernel *running_kernel = NULL;

        m = prepare_playground(&config);
        fail_if(!m, "Failed to prepare update playground");

        /* Hax the uname */
        boot_manager_set_uname(m, "unknown-uname");

        /* Make sure pre kernels are found */
        pre_kernels = boot_manager_get_kernels(m);
        fail_if(!pre_kernels, "Failed to find kernels");
        fail_if(pre_kernels->len != 1, "Available kernels != 1");

        /* Ensure it's not booting */
        fail_if(boot_manager_get_running_kernel(m, pre_kernels) != NULL,
                "Should not find a running kernel at this point");

        /* Attempt update in this configuration */
        fail_if(!boot_manager_update(m), "Failed to update single kernel system");

        /* Now "reboot" */
        fail_if(!boot_manager_set_uname(m, "4.2.1-121.kvm"), "Failed to simulate reboot");

        /* Grab new kernels */
        post_kernels = boot_manager_get_kernels(m);
        fail_if(!post_kernels, "Failed to find kernels after update");
        fail_if(post_kernels->len != 1, "Available post kernels != 1");

        /* Check running kernel */
        running_kernel = boot_manager_get_running_kernel(m, post_kernels);
        fail_if(!running_kernel, "Failed to find kernel post reboot");
        fail_if(!streq(running_kernel->meta.version, "4.2.1"), "Running kernel is invalid");
}
END_TEST

/**
 * Verify that we can update and install the bootloader correctly.
 *
 * Scenario:
 *
 *      - Install bootloader
 *      - Confirm initial bootloader installation
 *      - Bump bootloader data to not match
 *      - Verify bumped bootloader currently does NOT match
 *      - Request update of bootloader with *check* operation
 *      - Verify bootloader files are updated and match
 */
static void internal_loader_test(void)
{
        autofree(BootManager) *m = NULL;
        bool installs_default_bootloader = true;
#if defined(HAVE_SHIM_SYSTEMD_BOOT)
        installs_default_bootloader = false;
#endif
        /* whether the harness needs to check the default bootloader match. */
        bool check_default_bootloader = installs_default_bootloader;
        PlaygroundConfig start_conf = {.uefi = true };

        m = prepare_playground(&start_conf);
        fail_if(!m, "Fatal: Cannot initialise playground");

        fail_if(!boot_manager_modify_bootloader(m, BOOTLOADER_OPERATION_INSTALL),
                "Failed to install bootloader");

        confirm_bootloader();
        fail_if(!confirm_bootloader_match(check_default_bootloader), "Installed bootloader is incorrect");

        fail_if(!push_bootloader_update(1), "Failed to bump source bootloader");
        fail_if(confirm_bootloader_match(check_default_bootloader), "Source shouldn't match target bootloader yet");

        fail_if(!boot_manager_modify_bootloader(m,
                                                BOOTLOADER_OPERATION_UPDATE |
                                                    BOOTLOADER_OPERATION_NO_CHECK),
                "Failed to forcibly update bootloader");
        confirm_bootloader();
        fail_if(!confirm_bootloader_match(check_default_bootloader), "Bootloader didn't actually update");

        /* We're in sync */
        fail_if(boot_manager_needs_update(m), "Bootloader lied about needing an update");

        fail_if(!push_bootloader_update(2), "Failed to bump source bootloader");
        /* Pushed out of sync, should need update */
        fail_if(!boot_manager_needs_update(m), "Bootloader doesn't know it needs update");
        fail_if(!boot_manager_modify_bootloader(m, BOOTLOADER_OPERATION_UPDATE),
                "Failed to auto-update bootloader");
        fail_if(!confirm_bootloader_match(check_default_bootloader), "Auto-updated bootloader doesn't match source");
}

START_TEST(bootman_uefi_update)
{
        internal_loader_test();
}
END_TEST

/**
 * This test is currently specific only to the UEFI bootloader support,
 * as this is the only situation whereby it is possible to completely
 * and cleanly removed a bootloader.
 *
 * Scenario:
 *
 *      - Perform installation of the bootloader
 *      - Remove it again
 *      - Validate it is legitimately gone.
 */
START_TEST(bootman_uefi_remove_bootloader)
{
        autofree(BootManager) *m = NULL;

        fail_if(!nc_file_exists(TOP_BUILD_DIR "/tests/update_playground/" BOOT_DIRECTORY
                                              "/efi/BOOT"),
                "Main EFI directory missing, botched install");

        m = prepare_playground(&uefi_config);

        /* Install bootloader once */
        fail_if(!boot_manager_modify_bootloader(m,
                                                BOOTLOADER_OPERATION_INSTALL |
                                                    BOOTLOADER_OPERATION_NO_CHECK),
                "Failed to install bootloader");
        confirm_bootloader();

        /* Now remove it again */
        fail_if(!boot_manager_modify_bootloader(m, BOOTLOADER_OPERATION_REMOVE),
                "Failed to remove the bootloader");

        /* Ensure that it is indeed removed. */
        fail_if(nc_file_exists(TOP_BUILD_DIR "/tests/update_playground" BOOT_DIRECTORY
                                             "/efi/BOOT" DEFAULT_EFI_BLOB),
                "Main x64 bootloader present");
#if defined(HAVE_SYSTEMD_BOOT)
        fail_if(nc_file_exists(TOP_BUILD_DIR "/tests/update_playground/" BOOT_DIRECTORY
                                             "/efi/systemd"),
                "Systemd x64 bootloader present");
#endif

        fail_if(nc_file_exists(TOP_BUILD_DIR "/tests/update_playground/" BOOT_DIRECTORY
                                             "/loader/loader.conf"),
                "systemd-class loader.conf present");
        /* DEBUG:
        __attribute__ ((unused)) int r = system("tree " TOP_BUILD_DIR "/tests/update_playground");*/
}
END_TEST

START_TEST(bootman_uefi_namespace_migration)
{
        autofree(BootManager) *m = NULL;
        PlaygroundKernel uefi_old_kernels[] = { { "4.2.1", "kvm", 121, false, true },
                                                { "4.2.3", "kvm", 124, true, true },
                                                { "4.2.1", "native", 137, false, true },
                                                { "4.2.3", "native", 138, true, true } };
        PlaygroundConfig uefi_old = { "4.2.1-121.kvm",
                                      uefi_old_kernels,
                                      ARRAY_SIZE(uefi_old_kernels),
                                      .uefi = true };
        const char *vendor = NULL;

        m = prepare_playground(&uefi_old);
        vendor = boot_manager_get_vendor_prefix(m);

        fail_if(!nc_mkdir_p(BOOT_FULL "/loader/entries", 00755), "Failed to create loader dirs");

        /* Manually install a bunch of kernels using their legacy paths */
        for (size_t i = 0; i < uefi_old.n_kernels; i++) {
                PlaygroundKernel *k = &(uefi_old_kernels[i]);
                autofree(char) *tgt_path_kernel = NULL;
                autofree(char) *tgt_path_initrd = NULL;
                autofree(char) *tgt_path_config = NULL;

                tgt_path_kernel = string_printf("%s/%s.%s.%s-%d",
                                                BOOT_FULL,
                                                KERNEL_NAMESPACE,
                                                k->ktype,
                                                k->version,
                                                k->release);

                tgt_path_initrd = string_printf("%s/initrd-%s.%s.%s-%d",
                                                BOOT_FULL,
                                                KERNEL_NAMESPACE,
                                                k->ktype,
                                                k->version,
                                                k->release);

                tgt_path_config = string_printf("%s/loader/entries/%s-%s-%s-%d.conf",
                                                BOOT_FULL,
                                                vendor,
                                                k->ktype,
                                                k->version,
                                                k->release);

                file_set_text(tgt_path_kernel, "Placeholder kernel");
                file_set_text(tgt_path_initrd, "Placeholder initrd");
                file_set_text(tgt_path_config, "Placeholder config");

                fail_if(!confirm_kernel_installed(m, &uefi_old, k),
                        "Failed to manually install old kernel");
        }
        /* Upgrade will put new kernels in place, and wipe old ones */
        fail_if(!boot_manager_update(m), "Failed to migrate namespace on update");

        /* Iterate again, make sure new ones are valid, and old ones are gone
         * Not all kernels are retained during upgrade */
        for (size_t i = 0; i < uefi_old.n_kernels; i++) {
                PlaygroundKernel *k = &(uefi_old_kernels[i]);
                /* Only one file should be installed for the old kernel, the loader config */
                fail_if(kernel_installed_files_count(m, k) > 1, "Old kernel not removed correctly");
                /* Flip to non legacy for next check */
                k->legacy_name = false;
        }
        fail_if(!confirm_kernel_installed(m, &uefi_old, &(uefi_old_kernels[0])),
                "Kernel 1 not fully installed");
        fail_if(!confirm_kernel_installed(m, &uefi_old, &(uefi_old_kernels[1])),
                "Kernel 2 not fully installed");
        fail_if(!confirm_kernel_installed(m, &uefi_old, &(uefi_old_kernels[3])),
                "Kernel 4 not fully installed");
}
END_TEST

START_TEST(bootman_uefi_initrd_freestandings)
{
        autofree(BootManager) *m = NULL;
        autofree(char) *path_initrd = NULL;
        char *initrd_name = "00-initrd";

        m = prepare_playground(&uefi_config);
        fail_if(!m, "Failed to prepare update playground");

        path_initrd = string_printf("%s%s/%s",
                                        PLAYGROUND_ROOT,
                                        INITRD_DIRECTORY,
                                        initrd_name);

        file_set_text(path_initrd, "Placeholder initrd");
        /* Validate image install */
        fail_if(!boot_manager_enumerate_initrds_freestanding(m), "Failed to find freestanding initrd");

        fail_if(!check_freestanding_initrds_available(m, initrd_name), "Failed reading from initrd path");
        fail_if(!boot_manager_update(m), "Failed to update image");
        fail_if(!check_initrd_file_exist(m, initrd_name), "Failed copying initrd file");
}
END_TEST

START_TEST(bootman_uefi_missing_initrd_freestandings)
{
        autofree(BootManager) *m = NULL;
        autofree(char) *path_initrd = NULL;
        char *initrd_name = "00-initrd";

        m = prepare_playground(&uefi_config);
        fail_if(!m, "Failed to prepare update playground");

        path_initrd = string_printf("%s%s/%s",
                                    PLAYGROUND_ROOT,
                                    INITRD_DIRECTORY,
                                    initrd_name);

        file_set_text(path_initrd, "Placeholder initrd");
        /* Validate image install */
        fail_if(!boot_manager_enumerate_initrds_freestanding(m), "Failed to find freestanding initrd");
        unlink(path_initrd);
        fail_if(!check_freestanding_initrds_available(m, initrd_name), "Failed reading from initrd path");
        fail_if(boot_manager_update(m), "Incorrectly updated kernel when missing initrd");
        fail_if(confirm_kernel_installed(m, &uefi_config, &(uefi_kernels[0])),
                "Installed kernel with invalid config");
}
END_TEST

START_TEST(bootman_uefi_initrd_freestandings_image)
{
        autofree(BootManager) *m = NULL;
        autofree(char) *path_initrd = NULL;
        char *initrd_name = "00-initrd";

        m = prepare_playground(&uefi_config);
        fail_if(!m, "Failed to prepare update playground");

        path_initrd = string_printf("%s%s/%s",
                                        PLAYGROUND_ROOT,
                                        INITRD_DIRECTORY,
                                        initrd_name);

        file_set_text(path_initrd, "Placeholder initrd");
        /* Validate image install */
        fail_if(!boot_manager_enumerate_initrds_freestanding(m), "Failed to find freestanding initrd");

        fail_if(!check_freestanding_initrds_available(m, initrd_name), "Failed reading from initrd path");
        fail_if(!boot_manager_update(m), "Failed to update image");
        fail_if(!check_initrd_file_exist(m, initrd_name), "Failed copying initrd file");
}
END_TEST


/**
 * Ensure all blobs are removed for garbage collected kernels
 */
START_TEST(bootman_uefi_ensure_removed)
{
        autofree(BootManager) *m = NULL;

        m = prepare_playground(&uefi_config);
        fail_if(!m, "Failed to prepare update playground");

        /* Start on the 4.2.1-121.kvm */
        fail_if(!boot_manager_set_uname(m, "4.2.1-121.kvm"), "Failed to set initial kernel");

        /* Set the default kernel to the next kernel */
        fail_if(!set_kernel_default(&uefi_kernels[1]), "Failed to set kernel as default");

        /* Put 2 of the KVM kernels in */
        fail_if(!boot_manager_update(m), "Failed to apply initial updates");

        /* Reboot to new kernel */
        fail_if(!boot_manager_set_uname(m, "4.2.3-124.kvm"), "Failed to simulate reboot");
        /* Fully bootred */
        fail_if(!set_kernel_booted(&uefi_kernels[1], true), "Failed to set kernel booted");

        /* Apply the update so that the old kernel is now gone */
        fail_if(!boot_manager_update(m), "Failed to apply post-reboot updates");

        fail_if(!confirm_kernel_installed(m, &uefi_config, &uefi_kernels[1]),
                "New kernel is not installed");
        fail_if(!confirm_kernel_uninstalled(m, &uefi_kernels[0]), "Old kernel not fully removed");
}
END_TEST

START_TEST(bootman_uefi_list_kernels)
{
        autofree(BootManager) *m = NULL;
        char *kernels[] = {
                "  " KERNEL_NAMESPACE ".native.4.2.3-138",
                "  " KERNEL_NAMESPACE ".kvm.4.2.3-124",
                "* " KERNEL_NAMESPACE ".dash-test.4.2.1-1",
                NULL
        };
        int klen = sizeof(kernels) / sizeof(char *);
        char **results = NULL;
        m = prepare_playground(&uefi_config_dash_test);
        fail_if(!m, "Failed to prepare update playground");

        /* Validate image install */
        fail_if(!boot_manager_update(m), "Failed to update image");
        results = boot_manager_list_kernels(m);
        fail_if(!results, "Failed to get kernels");
        for (int i = 0; i < klen; i++) {
                fail_if(!streq(results[i], kernels[i]),
                        "Failed to get correct kernel list %s %s", results[i], kernels[i]);
                free(results[i]);
        }
        fail_if(results[klen - 1] != NULL,
                "Failed to get correct kernel list");
        free(results);
}
END_TEST

START_TEST(bootman_uefi_set_kernel)
{
        autofree(BootManager) *m = NULL;
        char *kernels[] = {
                "  " KERNEL_NAMESPACE ".native.4.2.3-138",
                "  " KERNEL_NAMESPACE ".kvm.4.2.3-124",
                "* " KERNEL_NAMESPACE ".dash-test.4.2.1-1",
                NULL
        };
        int klen = sizeof(kernels) / sizeof(char *);
        char **results = NULL;
        Kernel kern = { 0 };
        kern.meta.version = strdup(uefi_kernels[4].version);
        kern.meta.ktype = strdup(uefi_kernels[4].ktype);
        kern.meta.release = uefi_kernels[4].release;

        m = prepare_playground(&uefi_config_dash_test);
        fail_if(!m, "Failed to prepare update playground");

        /* Validate image install */
        fail_if(!boot_manager_update(m), "Failed to update image");
        fail_if(!boot_manager_set_default_kernel(m, &kern), "Failed to set default kernel");
        results = boot_manager_list_kernels(m);
        fail_if(!results, "Failed to get kernels");
        for (int i = 0; i < klen; i++) {
                fail_if(!streq(results[i], kernels[i]),
                        "Failed to get correct kernel list %s %s", results[i], kernels[i]);
                free(results[i]);
        }
        fail_if(results[klen - 1] != NULL,
                "Failed to get correct kernel list");
        free(results);
        free(kern.meta.version);
        free(kern.meta.ktype);
}
END_TEST

START_TEST(bootman_uefi_set_kernel_missing)
{
        autofree(BootManager) *m = NULL;
        Kernel kern = { 0 };
        kern.meta.version = "not";
        kern.meta.ktype = "exist";
        kern.meta.release = 0;

        m = prepare_playground(&uefi_config);
        fail_if(!m, "Failed to prepare update playground");

        /* Validate image install */
        fail_if(!boot_manager_update(m), "Failed to update image");
        fail_if(boot_manager_set_default_kernel(m, &kern),
                "Set default kernel that doesn't exist");
}
END_TEST

static Suite *core_suite(void)
{
        Suite *s = NULL;
        TCase *tc = NULL;

        s = suite_create("bootman_uefi");
        tc = tcase_create("bootman_uefi_modules");
        tcase_add_test(tc, bootman_uefi_get_boot_device);
        tcase_add_test(tc, bootman_uefi_image_modules);
        tcase_add_test(tc, bootman_uefi_native_modules);
        tcase_add_test(tc, bootman_uefi_update_from_unknown);
        tcase_add_test(tc, bootman_uefi_update);
        tcase_add_test(tc, bootman_uefi_remove_bootloader);
        tcase_add_test(tc, bootman_uefi_namespace_migration);
        tcase_add_test(tc, bootman_uefi_ensure_removed);
        tcase_add_test(tc, bootman_uefi_initrd_freestandings);
        tcase_add_test(tc, bootman_uefi_missing_initrd_freestandings);
        tcase_add_test(tc, bootman_uefi_initrd_freestandings_image);
        tcase_add_test(tc, bootman_uefi_list_kernels);
        tcase_add_test(tc, bootman_uefi_set_kernel);
        tcase_add_test(tc, bootman_uefi_set_kernel_missing);
        suite_add_tcase(s, tc);

        /* Tests without kernel modules */
        tc = tcase_create("bootman_uefi_no_modules");
        tcase_add_test(tc, bootman_uefi_native_no_modules);
        tcase_add_test(tc, bootman_uefi_image_no_modules);
        suite_add_tcase(s, tc);

        return s;
}

int main(void)
{
        Suite *s;
        SRunner *sr;
        int fail;

        /* syncing can be problematic during test suite runs */
        cbm_set_sync_filesystems(false);

        /* Ensure that logging is set up properly. */
        setenv("CBM_DEBUG", "1", 1);
        cbm_log_init(stderr);

        /* Turn off the EFI variable manipulation. */
        setenv("CBM_BOOTVAR_TEST_MODE", "yes", 1);

        /* Force detection of `fat` filesystem. */
        setenv("CBM_TEST_FSTYPE", "vfat", 1);

        cbm_blkid_set_vtable(&BlkidTestOps);
        cbm_system_set_vtable(&SystemTestOps);

        s = core_suite();
        sr = srunner_create(s);
        srunner_run_all(sr, CK_VERBOSE);
        fail = srunner_ntests_failed(sr);
        srunner_free(sr);

        if (fail > 0) {
                return EXIT_FAILURE;
        }

        return EXIT_SUCCESS;
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
