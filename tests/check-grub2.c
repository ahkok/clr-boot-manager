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

/**
 * We only permit UUID in our tests.
 */
static int grub2_blkid_probe_lookup_value(__cbm_unused__ blkid_probe pr, const char *name,
                                          const char **data, size_t *len)
{
        if (!name || !data) {
                return -1;
        }
        if (streq(name, "UUID")) {
                *data = DEFAULT_UUID;
        } else {
                return -1;
        }
        if (!*data) {
                abort();
        }
        if (len) {
                *len = strlen(*data);
        }
        return 0;
}

static PlaygroundKernel grub2_kernels[] = { { "4.2.1", "kvm", 121, false, true },
                                            { "4.2.3", "kvm", 124, true, true },
                                            { "4.2.1", "native", 137, false, true },
                                            { "4.2.3", "native", 138, true, true } };

static PlaygroundConfig grub2_config = { "4.2.1-121.kvm",
                                         grub2_kernels,
                                         ARRAY_SIZE(grub2_kernels),
                                         .uefi = false };

#define PLAYGROUND_ROOT TOP_BUILD_DIR "/tests/update_playground"

START_TEST(bootman_grub2_get_boot_device)
{
        autofree(char) *boot_device = NULL;
        autofree(BootManager) *m = NULL;
        autofree(char) *exp = NULL;

        /* Ensure cleanup */
        m = prepare_playground(&grub2_config);

        boot_device = get_boot_device();
        fail_if(boot_device != NULL, "Found incorrect device for Legacy (GRUB2) Boot");
}
END_TEST

START_TEST(bootman_grub2_image)
{
        autofree(BootManager) *m = NULL;
        m = prepare_playground(&grub2_config);
        fail_if(!m, "Failed to prepare update playground");

        /* Validate image install */
        fail_if(!boot_manager_update(m), "Failed to update image");
}
END_TEST

START_TEST(bootman_grub2_native)
{
        autofree(BootManager) *m = NULL;

        m = prepare_playground(&grub2_config);
        fail_if(!m, "Failed to prepare update playground");

        fail_if(!set_kernel_booted(&grub2_kernels[1], true), "Failed to set kernel as booted");

        fail_if(!boot_manager_update(m), "Failed to update in native mode");

        /* Latest kernel for us */
        fail_if(!confirm_kernel_installed(m, &grub2_config, &(grub2_kernels[0])),
                "Newest kernel not installed");

        /* Running kernel */
        fail_if(!confirm_kernel_installed(m, &grub2_config, &(grub2_kernels[1])),
                "Newest kernel not installed");

        /* This guy isn't supposed to be kept around now */
        fail_if(!confirm_kernel_uninstalled(m, &(grub2_kernels[2])),
                "Uninteresting kernel shouldn't be kept around.");
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
START_TEST(bootman_grub2_update_from_unknown)
{
        autofree(BootManager) *m = NULL;
        PlaygroundKernel kernels[] = { { "4.2.1", "kvm", 121, true, true } };
        PlaygroundConfig config = { "4.2.1-121.kvm", kernels, 1, false };
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

START_TEST(bootman_grub2_namespace_migration)
{
        autofree(BootManager) *m = NULL;
        PlaygroundKernel kernels[] = { { "4.2.1", "kvm", 121, false, false },
                                       { "4.2.3", "kvm", 124, true, false },
                                       { "4.2.1", "native", 137, false, false },
                                       { "4.2.3", "native", 138, true, false } };
        PlaygroundConfig config = { "4.2.1-121.kvm", kernels, ARRAY_SIZE(kernels), .uefi = false };

        m = prepare_playground(&config);

        fail_if(!nc_mkdir_p(PLAYGROUND_ROOT "/etc/grub.d", 00755), "Failed to create GRUB dir");
        fail_if(!nc_mkdir_p(BOOT_FULL, 00755), "Failed to create boot dir");

        /* Manually install a bunch of kernels using their legacy paths */
        for (size_t i = 0; i < config.n_kernels; i++) {
                PlaygroundKernel *k = &(kernels[i]);
                autofree(char) *tgt_path_config = NULL;

                tgt_path_config = string_printf("%s/etc/grub.d/10_%s_%s-%d.%s",
                                                PLAYGROUND_ROOT,
                                                boot_manager_get_os_id(m),
                                                k->version,
                                                k->release,
                                                k->ktype);

                fail_if(!file_set_text(tgt_path_config, "Placeholder config"),
                        "Failed to write legacy GRUB2 config file");
        }
        /* Upgrade will put new kernels in place, and wipe old ones */
        fail_if(!boot_manager_update(m), "Failed to migrate namespace on update");

        /* Iterate again, make sure new ones are valid, and old ones are gone
         * Not all kernels are retained during upgrade */
        for (size_t i = 0; i < config.n_kernels; i++) {
                PlaygroundKernel *k = &(kernels[i]);
                autofree(char) *tgt_path_config = NULL;

                tgt_path_config = string_printf("%s/etc/grub.d/10_%s_%s-%d.%s",
                                                PLAYGROUND_ROOT,
                                                boot_manager_get_os_id(m),
                                                k->version,
                                                k->release,
                                                k->ktype);

                fail_if(nc_file_exists(tgt_path_config),
                        "Old GRUB2 path not removed: %s",
                        tgt_path_config);
        }
        fail_if(!nc_file_exists(PLAYGROUND_ROOT "/etc/grub.d/10_" KERNEL_NAMESPACE),
                "GRUB2 configuration not written");
}
END_TEST

static Suite *core_suite(void)
{
        Suite *s = NULL;
        TCase *tc = NULL;

        s = suite_create("bootman_grub2");
        tc = tcase_create("bootman_grub2_functions");
        tcase_add_test(tc, bootman_grub2_get_boot_device);
        tcase_add_test(tc, bootman_grub2_image);
        tcase_add_test(tc, bootman_grub2_native);
        tcase_add_test(tc, bootman_grub2_update_from_unknown);
        tcase_add_test(tc, bootman_grub2_namespace_migration);
        suite_add_tcase(s, tc);

        return s;
}

int main(void)
{
        Suite *s;
        SRunner *sr;
        int fail;
        /* override test ops for legacy grub2 testing */
        CbmBlkidOps blkid_ops = BlkidTestOps;
        blkid_ops.probe_lookup_value = grub2_blkid_probe_lookup_value;

        /* syncing can be problematic during test suite runs */
        cbm_set_sync_filesystems(false);

        /* Ensure that logging is set up properly. */
        setenv("CBM_DEBUG", "1", 1);
        cbm_log_init(stderr);

        /* Turn off the EFI variable manipulation. */
        setenv("CBM_BOOTVAR_TEST_MODE", "yes", 1);

        /* Force detection of `ext` filesystem. */
        setenv("CBM_TEST_FSTYPE", "ext4", 1);

        cbm_blkid_set_vtable(&blkid_ops);
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
