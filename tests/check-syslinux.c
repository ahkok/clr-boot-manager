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
#include <sys/sysmacros.h>

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

/**
 * Coerce legacy lookup
 */
static inline int legacy_devno_to_wholedisk(__cbm_unused__ dev_t dev, __cbm_unused__ char *diskname,
                                            __cbm_unused__ size_t len,
                                            __cbm_unused__ dev_t *diskdevno)
{
        *diskdevno = makedev(8, 8);
        return 0;
}

/**
 * Coerce legacy lookup
 */
static inline dev_t legacy_probe_get_wholedisk_devno(__cbm_unused__ blkid_probe pr)
{
        return makedev(8, 8);
}

/**
 * Forces detection of GPT legacy boot partition
 */
static inline unsigned long long legacy_partition_get_flags(__cbm_unused__ blkid_partition par)
{
        return (1ULL << 2);
}

/**
 * Force the detection of a boot partition GPT UUID
 */
static inline const char *legacy_partition_get_uuid(__cbm_unused__ blkid_partition par)
{
        return DEFAULT_PART_UUID;
}

static PlaygroundKernel legacy_kernels[] = { { "4.2.1", "kvm", 121, false, true },
                                             { "4.2.3", "kvm", 124, true, true },
                                             { "4.2.1", "native", 137, false, true },
                                             { "4.2.3", "native", 138, true, true } };

static PlaygroundConfig legacy_config = { "4.2.1-121.kvm",
                                          legacy_kernels,
                                          ARRAY_SIZE(legacy_kernels),
                                          .uefi = false };

#define PLAYGROUND_ROOT TOP_BUILD_DIR "/tests/update_playground"

START_TEST(bootman_legacy_get_boot_device)
{
        autofree(char) *boot_device = NULL;
        autofree(BootManager) *m = NULL;
        autofree(char) *exp = NULL;

        /* Ensure cleanup */
        m = prepare_playground(&legacy_config);

        boot_device = get_boot_device();
        fail_if(boot_device != NULL, "Found incorrect UEFI device for Legacy Boot");

        boot_device = get_legacy_boot_device(PLAYGROUND_ROOT);
        fail_if(!boot_device, "Failed to determine legacy boot device");

        exp = string_printf("%s/dev/disk/by-partuuid/Test-PartUUID", PLAYGROUND_ROOT);
        fail_if(!streq(exp, boot_device), "Boot device does not match expected result");
}
END_TEST

START_TEST(bootman_legacy_image)
{
        autofree(BootManager) *m = NULL;
        m = prepare_playground(&legacy_config);
        fail_if(!m, "Failed to prepare update playground");

        /* Validate image install */
        fail_if(!boot_manager_update(m), "Failed to update image");
}
END_TEST

START_TEST(bootman_legacy_native)
{
        autofree(BootManager) *m = NULL;

        m = prepare_playground(&legacy_config);
        fail_if(!m, "Failed to prepare update playground");

        fail_if(!set_kernel_booted(&legacy_kernels[1], true), "Failed to set kernel as booted");

        fail_if(!boot_manager_update(m), "Failed to update in native mode");

        /* Latest kernel for us */
        fail_if(!confirm_kernel_installed(m, &legacy_config, &(legacy_kernels[0])),
                "Newest kernel not installed");

        /* Running kernel */
        fail_if(!confirm_kernel_installed(m, &legacy_config, &(legacy_kernels[1])),
                "Newest kernel not installed");

        /* This guy isn't supposed to be kept around now */
        fail_if(!confirm_kernel_uninstalled(m, &(legacy_kernels[2])),
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
START_TEST(bootman_legacy_update_from_unknown)
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
        PlaygroundConfig start_conf = { 0 };
        /* Testing bbbb data */
        const char *syslinux_v2 = TOP_DIR "/tests/data/gptmbr.bin.v2";
        /* Original aaaa data */
        const char *syslinux_v3 = TOP_DIR "/tests/data/gptmbr.bin";
        const char *syslinux_orig = PLAYGROUND_ROOT "/dev/leRootDevice-orig";
        const char *syslinux_disk = PLAYGROUND_ROOT "/dev/leRootDevice";

        m = prepare_playground(&start_conf);
        fail_if(!m, "Fatal: Cannot initialise playground");

        fail_if(!boot_manager_modify_bootloader(m, BOOTLOADER_OPERATION_INSTALL),
                "Failed to install bootloader");

        /* 
         * syslinux creates the mbr file not based on a "template" file, but actually
         * writes the disk file based on an the "on source statically defined template"
         * so for future comparation make a copy of it
         */
        fail_if(!copy_file(syslinux_disk, syslinux_orig, 00644),
                "Failed to bump source bootloader");
        fail_if(!cbm_files_match(syslinux_disk, syslinux_orig),
                "Source should match target bootloader");

        /* Simulate an update with a new gptmbr file */
        fail_if(!copy_file(syslinux_v2, syslinux_disk, 00644),
                "Failed to bump source bootloader");
        fail_if(!cbm_files_match(syslinux_v2, syslinux_disk),
                "Source should match target bootloader");

        fail_if(!boot_manager_modify_bootloader(m,
                                                BOOTLOADER_OPERATION_UPDATE |
                                                    BOOTLOADER_OPERATION_NO_CHECK),
                "Failed to forcibly update bootloader");

        fail_if(!cbm_files_match(syslinux_disk, syslinux_orig),
                "Bootloader didn't actually update");

        /* Push a "v3" */
        fail_if(!copy_file(syslinux_v3, syslinux_disk, 00644),
                "Failed to bump source bootloader");

        /* Pushed out of sync, should need update */
        fail_if(!boot_manager_modify_bootloader(m, BOOTLOADER_OPERATION_UPDATE),
                "Failed to auto-update bootloader");

        /* Make sure last update now works */
        fail_if(!cbm_files_match(syslinux_disk, syslinux_orig),
                "Auto-updated bootloader doesn't match source");
}

START_TEST(bootman_legacy_update)
{
        internal_loader_test();
}
END_TEST

static Suite *core_suite(void)
{
        Suite *s = NULL;
        TCase *tc = NULL;

        s = suite_create("bootman_legacy");
        tc = tcase_create("bootman_legacy_functions");
        tcase_add_test(tc, bootman_legacy_get_boot_device);
        tcase_add_test(tc, bootman_legacy_image);
        tcase_add_test(tc, bootman_legacy_native);
        tcase_add_test(tc, bootman_legacy_update_from_unknown);
        tcase_add_test(tc, bootman_legacy_update);
        suite_add_tcase(s, tc);

        return s;
}

int main(void)
{
        Suite *s;
        SRunner *sr;
        int fail;
        /* override test ops for legacy testing */
        CbmBlkidOps blkid_ops = BlkidTestOps;
        blkid_ops.devno_to_wholedisk = legacy_devno_to_wholedisk;
        blkid_ops.probe_get_wholedisk_devno = legacy_probe_get_wholedisk_devno;
        blkid_ops.partition_get_flags = legacy_partition_get_flags;
        blkid_ops.partition_get_uuid = legacy_partition_get_uuid;

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
