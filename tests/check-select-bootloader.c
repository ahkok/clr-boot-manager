/*
 * This file is part of clr-boot-manager.
 *
 * This test suite is designed to resolve issue #54:
 * https://github.com/ikeydoherty/clr-boot-manager/issues/54
 *
 * The basic logic is, create a suitable test environment, and then have
 * CBM initialise a BootManager for the root, which will fall through:
 *
 *  - cbm_inspect_root
 *  - boot_manager_select_bootloader
 *
 * We know the update logic works based on the other tests. Here, we are
 * trying to ascertain that the correct bootloader is selected in every case
 * for the given system configuration.
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

#include "bootloader.h"
#include "bootman.h"
#define _BOOTMAN_INTERNAL_
#include "bootman_private.h"
#undef _BOOTMAN_INTERNAL_
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

/**
 * Define a single name for the UEFI bootloader to reduce complexity further down
 */
#if defined(HAVE_SHIM_SYSTEMD_BOOT)
#define UEFI_BOOTLOADER_NAME "shim-systemd"
#elif defined(HAVE_SYSTEMD_BOOT)
#define UEFI_BOOTLOADER_NAME "systemd"
#endif

#define PLAYGROUND_ROOT TOP_BUILD_DIR "/tests/update_playground"

/**
 * Restore the default testing vtables
 */
static void bootman_select_set_default_vtables(void)
{
        cbm_blkid_set_vtable(&BlkidTestOps);
        cbm_system_set_vtable(&SystemTestOps);
}

/**
 * Simple assertion that the bootloader is the one we've asked for
 */
static void ensure_bootloader_is(BootManager *manager, const char *expected)
{
        fail_if(manager == NULL, "No BootManager");
        fail_if(!manager->bootloader, "No bootloader is selected. Expected %s", expected);
        const char *name = manager->bootloader->name;
        fail_if(!streq(name, expected), "Expected bootloader '%s', got '%s'", expected, name);
}

/**
 * Harness helps us out by creating the required "bits" within the prepared root.
 * This method will forcibly break that logic by making the root device go away
 *
 * The blkid tests themselves will still work fine as blkid has been encapsulated
 * already.
 */
static void nuke_boot_device(struct PlaygroundConfig *config)
{
        if (config->uefi) {
                fail_if(!nc_rm_rf(PLAYGROUND_ROOT "/sys/firmware/efi/efivars"),
                        "Failed to remove efivars");
        }
        fail_if(!nc_rm_rf(PLAYGROUND_ROOT "/dev"), "Failed to remove /dev");
}

/**
 * ############ BEGIN UEFI TESTS #################
 */

/**
 * We're operating over UEFI, and a boot device
 */
START_TEST(bootman_select_uefi_with_boot)
{
        static PlaygroundConfig config = { "4.2.1-121.kvm", NULL, 0, .uefi = true };
        setenv("CBM_TEST_FSTYPE", "vfat", 1);
        autofree(BootManager) *m = NULL;
        bootman_select_set_default_vtables();

        m = prepare_playground(&config);
        boot_manager_set_prefix(m, PLAYGROUND_ROOT);

        ensure_bootloader_is(m, UEFI_BOOTLOADER_NAME);
}
END_TEST

/**
 * We're operating over UEFI, and NO boot device
 */
START_TEST(bootman_select_uefi_without_boot)
{
        static PlaygroundConfig config = { "4.2.1-121.kvm", NULL, 0, .uefi = true };
        setenv("CBM_TEST_FSTYPE", "vfat", 1);
        autofree(BootManager) *m = NULL;
        bootman_select_set_default_vtables();

        /* Reinit the boot manager with no supporting boot device */
        m = prepare_playground(&config);
        nuke_boot_device(&config);
        boot_manager_set_prefix(m, PLAYGROUND_ROOT);

        ensure_bootloader_is(m, UEFI_BOOTLOADER_NAME);
}
END_TEST

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

/**
 * This will force the tests to use the legacy detection codepaths
 */
static CbmBlkidOps legacy_blkid_ops = {
        .probe_new_from_filename = test_blkid_new_probe_from_filename,
        .probe_enable_superblocks = test_blkid_probe_enable_superblocks,
        .probe_set_superblocks_flags = test_blkid_probe_set_superblocks_flags,
        .probe_enable_partitions = test_blkid_probe_enable_partitions,
        .probe_set_partitions_flags = test_blkid_probe_set_partitions_flags,
        .probe_get_wholedisk_devno = legacy_probe_get_wholedisk_devno,
        .probe_lookup_value = test_blkid_probe_lookup_value,
        .do_safeprobe = test_blkid_do_safeprobe,
        .free_probe = test_blkid_free_probe,
        .probe_get_partitions = test_blkid_probe_get_partitions,
        .partlist_numof_partitions = test_blkid_partlist_numof_partitions,
        .partlist_get_partition = test_blkid_partlist_get_partition,
        .partition_get_flags = legacy_partition_get_flags,
        .partition_get_uuid = legacy_partition_get_uuid,
        .partlist_get_table = test_blkid_partlist_get_table,
        .parttable_get_type = test_blkid_parttable_get_type,
        .devno_to_wholedisk = legacy_devno_to_wholedisk,
        .devno_to_devname = test_blkid_devno_to_devname,
};

static void bootman_select_set_legacy_vtables(void)
{
        /* override test ops for legacy testing */
        cbm_blkid_set_vtable(&legacy_blkid_ops);
        cbm_system_set_vtable(&SystemTestOps);
}

/**
 * ############ BEGIN EXTLINUX TESTS #################
 */

/**
 * We're operating with a legacy device available
 */
START_TEST(bootman_select_extlinux_with_boot)
{
        PlaygroundConfig config = { "4.2.1-121.kvm", NULL, 0, .uefi = false };
        setenv("CBM_TEST_FSTYPE", "ext4", 1);
        autofree(BootManager) *m = NULL;
        bootman_select_set_legacy_vtables();

        m = prepare_playground(&config);
        boot_manager_set_prefix(m, PLAYGROUND_ROOT);

        ensure_bootloader_is(m, "extlinux");
}
END_TEST

/**
 * ############ BEGIN GRUB2 TESTS #################
 */

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

/**
 * Force blkid ops to work for GRUB2 (UUID/MBR system, no PartUUID)
 */
static CbmBlkidOps grub2_blkid_ops = {
        .probe_new_from_filename = test_blkid_new_probe_from_filename,
        .probe_enable_superblocks = test_blkid_probe_enable_superblocks,
        .probe_set_superblocks_flags = test_blkid_probe_set_superblocks_flags,
        .probe_enable_partitions = test_blkid_probe_enable_partitions,
        .probe_set_partitions_flags = test_blkid_probe_set_partitions_flags,
        .probe_get_wholedisk_devno = test_blkid_probe_get_wholedisk_devno,
        .probe_lookup_value = grub2_blkid_probe_lookup_value,
        .do_safeprobe = test_blkid_do_safeprobe,
        .free_probe = test_blkid_free_probe,
        .probe_get_partitions = test_blkid_probe_get_partitions,
        .partlist_numof_partitions = test_blkid_partlist_numof_partitions,
        .partlist_get_partition = test_blkid_partlist_get_partition,
        .partition_get_flags = test_blkid_partition_get_flags,
        .partition_get_uuid = test_blkid_partition_get_uuid,
        .partlist_get_table = test_blkid_partlist_get_table,
        .parttable_get_type = test_blkid_parttable_get_type,
        .devno_to_wholedisk = test_blkid_devno_to_wholedisk,
        .devno_to_devname = test_blkid_devno_to_devname,
};

static void bootman_select_set_grub2_vtables(void)
{
        /* override test ops for legacy testing */
        cbm_blkid_set_vtable(&grub2_blkid_ops);
        cbm_system_set_vtable(&SystemTestOps);
}

/**
 * We're operating with GRUB2, and NO boot device
 */
START_TEST(bootman_select_grub2_without_boot)
{
        static PlaygroundConfig config = { "4.2.1-121.kvm", NULL, 0, .uefi = false };
        setenv("CBM_TEST_FSTYPE", "ext4", 1);
        autofree(BootManager) *m = NULL;
        bootman_select_set_grub2_vtables();

        /* Reinit the boot manager with no supporting boot device */
        m = prepare_playground(&config);
        nuke_boot_device(&config);
        boot_manager_set_prefix(m, PLAYGROUND_ROOT);

        ensure_bootloader_is(m, "grub2");
}
END_TEST

/**
 * ############ BEGIN EDGE CASE TESTS #################
 */

/**
 * We have a GPT disk that contains a legacy partition, we are with UEFI available
 * We should select UEFI bootloader when /sys/firmware/efi exists
 */
START_TEST(bootman_select_edge_uefi_with_legacy_part_native)
{
        static PlaygroundConfig config = { "4.2.1-121.kvm", NULL, 0, .uefi = false };
        setenv("CBM_TEST_FSTYPE", "ext4", 1);
        autofree(BootManager) *m = NULL;
        bootman_select_set_legacy_vtables();

        /* At this point we would've selected extlinux */
        m = prepare_playground(&config);
        ensure_bootloader_is(m, "extlinux");

        /* Construct /sys/firmware/efi. Now we need it to be UEFI bootloader */
        fail_if(!nc_mkdir_p(PLAYGROUND_ROOT "/sys/firmware/efi", 00755), "Failed to emulate EFI");
        boot_manager_set_prefix(m, PLAYGROUND_ROOT);
        ensure_bootloader_is(m, UEFI_BOOTLOADER_NAME);
}
END_TEST

/**
 * We have a GPT disk that contains a legacy partition, we are with UEFI available
 * We should only ever select extlinux
 */
START_TEST(bootman_select_edge_uefi_with_legacy_part_image)
{
        static PlaygroundConfig config = { "4.2.1-121.kvm", NULL, 0, .uefi = false };
        setenv("CBM_TEST_FSTYPE", "ext4", 1);
        autofree(BootManager) *m = NULL;
        bootman_select_set_legacy_vtables();

        /* At this point we would've selected extlinux */
        m = prepare_playground(&config);
        ensure_bootloader_is(m, "extlinux");

        boot_manager_set_prefix(m, PLAYGROUND_ROOT);
        ensure_bootloader_is(m, "extlinux");
}
END_TEST

static Suite *core_suite(void)
{
        Suite *s = NULL;
        TCase *tc = NULL;

        s = suite_create("bootman_select");

        /* UEFI tests */
        tc = tcase_create("bootman_select_uefi_functions");
        tcase_add_test(tc, bootman_select_uefi_with_boot);
        tcase_add_test(tc, bootman_select_uefi_without_boot);
        suite_add_tcase(s, tc);

        /* extlinux tests */
        tc = tcase_create("bootman_select_extlinux_functions");
        tcase_add_test(tc, bootman_select_extlinux_with_boot);
        suite_add_tcase(s, tc);

        /* grub2 tests */
        tc = tcase_create("bootman_select_grub2_functions");
        tcase_add_test(tc, bootman_select_grub2_without_boot);
        suite_add_tcase(s, tc);

        /* edge cases */
        tc = tcase_create("Bootman_select_edge_cases");
        tcase_add_test(tc, bootman_select_edge_uefi_with_legacy_part_native);
        tcase_add_test(tc, bootman_select_edge_uefi_with_legacy_part_image);
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
