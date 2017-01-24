/*
 * This file is part of clr-boot-manager.
 *
 * Copyright © 2016-2017 Intel Corporation
 *
 * clr-boot-manager is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 */

#define _GNU_SOURCE

#include <blkid.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <glob.h>
#include <libgen.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <unistd.h>

#include "files.h"
#include "log.h"
#include "nica/files.h"
#include "util.h"

DEF_AUTOFREE(DIR, closedir)

/**
 * Legacy boot bit, i.e. partition flag on a GPT disk
 */
#define CBM_MBR_BOOT_FLAG (1ULL << 2)

/**
 * By default we call sync() - for testing however we disable this due to timeout
 * issues.
 */
static bool cbm_should_sync = true;

void cbm_sync(void)
{
        if (cbm_should_sync) {
                sync();
        }
}

bool cbm_files_match(const char *p1, const char *p2)
{
        autofree(CbmMappedFile) *m1 = CBM_MAPPED_FILE_INIT;
        autofree(CbmMappedFile) *m2 = CBM_MAPPED_FILE_INIT;

        if (!cbm_mapped_file_open(p1, m1)) {
                return false;
        }

        if (!cbm_mapped_file_open(p2, m2)) {
                return false;
        }

        /* If the lengths are different they're clearly not the same file */
        if (m1->length != m2->length) {
                return false;
        }

        /* Compare both buffers */
        if (memcmp(m1->buffer, m2->buffer, m1->length) == 0) {
                return true;
        }
        return false;
}

char *get_boot_device()
{
        glob_t glo = { 0 };
        char read_buf[4096];
        glo.gl_offs = 1;
        int fd = -1;
        ;
        ssize_t size = 0;
        autofree(char) *uuid = NULL;
        autofree(char) *p = NULL;

        glob("/sys/firmware/efi/efivars/LoaderDevicePartUUID*", GLOB_DOOFFS, NULL, &glo);

        if (glo.gl_pathc < 1) {
                globfree(&glo);
                goto next;
        }

        /* Read the uuid */
        fd = open(glo.gl_pathv[1], O_RDONLY | O_NOCTTY | O_CLOEXEC);
        globfree(&glo);

        if (fd < 0) {
                LOG_ERROR("Unable to read LoaderDevicePartUUID");
                return NULL;
        }

        size = read(fd, read_buf, sizeof(read_buf));
        close(fd);
        if (size < 1) {
                goto next;
        }

        uuid = calloc((size_t)(size + 1), sizeof(char));
        int j = 0;
        for (ssize_t i = 0; i < size; i++) {
                char c = read_buf[i];
                if (!isalnum(c) && c != '-' && c != '_') {
                        continue;
                }
                if (c == '_' || c == '-') {
                        uuid[j] = '-';
                } else {
                        uuid[j] = (char)tolower(read_buf[i]);
                }
                ++j;
        }
        read_buf[j] = '\0';

        if (asprintf(&p, "/dev/disk/by-partuuid/%s", uuid) < 0) {
                DECLARE_OOM();
                return NULL;
        }

        if (nc_file_exists(p)) {
                return strdup(p);
        }
next:

        if (nc_file_exists("/dev/disk/by-partlabel/ESP")) {
                return strdup("/dev/disk/by-partlabel/ESP");
        }
        return NULL;
}

/**
 * Use libblkid to determine the parent disk for the given path,
 * in order to facilitate partition enumeration
 */
static bool get_parent_disk_devno(char *path, dev_t *diskdevno)
{
        struct stat st = { 0 };
        dev_t ret;

        if (stat(path, &st) != 0) {
                return false;
        }

        if (major(st.st_dev) == 0) {
                LOG_ERROR("Invalid block device: %s", path);
                return false;
        }

        if (blkid_devno_to_wholedisk(st.st_dev, NULL, 0, &ret) < 0) {
                return false;
        }
        *diskdevno = ret;
        return true;
}

char *get_parent_disk(char *path)
{
        dev_t devt;
        autofree(char) *node = NULL;

        if (!get_parent_disk_devno(path, &devt)) {
                return NULL;
        }

        if (asprintf(&node, "/dev/block/%u:%u", major(devt), minor(devt)) < 0) {
                DECLARE_OOM();
                return NULL;
        }
        return realpath(node, NULL);
}

char *get_legacy_boot_device(char *path)
{
        blkid_probe probe = NULL;
        blkid_partlist parts = NULL;
        int part_count = 0;
        char *ret = NULL;
        autofree(char) *parent_disk = NULL;

        parent_disk = get_parent_disk(path);
        if (!parent_disk) {
                return NULL;
        }

        probe = blkid_new_probe_from_filename(parent_disk);
        if (!probe) {
                LOG_ERROR("Unable to blkid probe %s", parent_disk);
                return NULL;
        }

        blkid_probe_enable_superblocks(probe, 1);
        blkid_probe_set_superblocks_flags(probe, BLKID_SUBLKS_TYPE);
        blkid_probe_enable_partitions(probe, 1);
        blkid_probe_set_partitions_flags(probe, BLKID_PARTS_ENTRY_DETAILS);

        if (blkid_do_safeprobe(probe) != 0) {
                LOG_ERROR("Error probing filesystem of %s: %s", parent_disk, strerror(errno));
                goto clean;
        }

        parts = blkid_probe_get_partitions(probe);

        part_count = blkid_partlist_numof_partitions(parts);
        if (part_count <= 0) {
                /* No partitions */
                goto clean;
        }

        for (int i = 0; i < part_count; i++) {
                blkid_partition part = blkid_partlist_get_partition(parts, i);
                const char *part_id = NULL;
                unsigned long long flags;
                autofree(char) *pt_path = NULL;

                flags = blkid_partition_get_flags(part);
                if (flags & CBM_MBR_BOOT_FLAG) {
                        part_id = blkid_partition_get_uuid(part);
                        if (!part_id) {
                                LOG_ERROR("Not a valid GPT disk");
                                goto clean;
                        }
                        if (asprintf(&pt_path, "/dev/disk/by-partuuid/%s", part_id) < 0) {
                                DECLARE_OOM();
                                goto clean;
                        }
                        ret = realpath(pt_path, NULL);
                        break;
                }
        }

clean:
        blkid_free_probe(probe);
        errno = 0;
        return ret;
}

char *cbm_get_file_parent(const char *p)
{
        char *r = realpath(p, NULL);
        if (!r) {
                return NULL;
        }
        return dirname(r);
}

bool file_set_text(const char *path, char *text)
{
        FILE *fp = NULL;
        bool ret = false;

        if (nc_file_exists(path) && unlink(path) < 0) {
                return false;
        }
        cbm_sync();

        fp = fopen(path, "w");

        if (!fp) {
                goto end;
        }

        if (fprintf(fp, "%s", text) < 0) {
                goto end;
        }
        ret = true;
end:
        if (fp) {
                fclose(fp);
        }
        cbm_sync();

        return ret;
}

bool file_get_text(const char *path, char **out_buf)
{
        autofree(CbmMappedFile) *mapped_file = CBM_MAPPED_FILE_INIT;

        if (!out_buf) {
                return false;
        }

        *out_buf = NULL;

        if (!cbm_mapped_file_open(path, mapped_file)) {
                return false;
        }

        *out_buf = strdup(mapped_file->buffer);
        if (!*out_buf) {
                return false;
        }
        return true;
}

bool copy_file(const char *src, const char *target, mode_t mode)
{
        struct stat sst = { 0 };
        ssize_t sz;
        int sfd = -1;
        int dfd = -1;
        bool ret = false;
        ssize_t written;

        sfd = open(src, O_RDONLY);
        if (sfd < 0) {
                return false;
        }
        dfd = open(target, O_WRONLY | O_TRUNC | O_CREAT, mode);
        if (dfd < 0) {
                goto end;
        }
        if (fstat(sfd, &sst) != 0) {
                goto end;
        }

        sz = sst.st_size;
        for (;;) {
                written = sendfile(dfd, sfd, NULL, (size_t)sz);
                if (written == sz) {
                        break;
                } else if (written < 0) {
                        goto end;
                }
                sz -= written;
        }
        ret = true;

end:
        if (sfd > 0) {
                close(sfd);
        }
        if (dfd > 0) {
                close(dfd);
        }
        return ret;
}

bool copy_file_atomic(const char *src, const char *target, mode_t mode)
{
        autofree(char) *new_name = NULL;
        struct stat st = { 0 };

        if (asprintf(&new_name, "%s.TmpWrite", target) < 0) {
                return false;
        }

        if (!copy_file(src, new_name, mode)) {
                (void)unlink(new_name);
                return false;
        }
        cbm_sync();

        /* Delete target if needed  */
        if (stat(target, &st) == 0) {
                if (!S_ISDIR(st.st_mode) && unlink(target) != 0) {
                        return false;
                }
                cbm_sync();
        } else {
                errno = 0;
        }

        if (rename(new_name, target) != 0) {
                return false;
        }
        /* vfat protect */
        cbm_sync();

        return true;
}

bool cbm_is_mounted(const char *path, bool *error)
{
        autofree(FILE_MNT) *tab = NULL;
        struct mntent *ent = NULL;
        struct mntent mnt = { 0 };
        char buf[8192];

        if (error) {
                *error = false;
        }

        tab = setmntent("/proc/self/mounts", "r");
        if (!tab) {
                if (error) {
                        *error = true;
                }
                return false;
        }

        while ((ent = getmntent_r(tab, &mnt, buf, sizeof(buf)))) {
                if (mnt.mnt_dir && streq(path, mnt.mnt_dir)) {
                        return true;
                }
        }

        return false;
}

char *cbm_get_mountpoint_for_device(const char *device)
{
        autofree(FILE_MNT) *tab = NULL;
        struct mntent *ent = NULL;
        struct mntent mnt = { 0 };
        char buf[8192];
        autofree(char) *abs_path = NULL;

        abs_path = realpath(device, NULL);
        if (!abs_path) {
                return NULL;
        }

        tab = setmntent("/proc/self/mounts", "r");
        if (!tab) {
                return NULL;
        }

        while ((ent = getmntent_r(tab, &mnt, buf, sizeof(buf)))) {
                if (!mnt.mnt_fsname) {
                        continue;
                }
                autofree(char) *mnt_device = realpath(mnt.mnt_fsname, NULL);
                if (!mnt_device) {
                        continue;
                }
                if (streq(abs_path, mnt_device)) {
                        return strdup(mnt.mnt_dir);
                }
        }
        return NULL;
}

bool cbm_system_has_uefi()
{
        return nc_file_exists("/sys/firmware/efi");
}

void cbm_set_sync_filesystems(bool should_sync)
{
        cbm_should_sync = should_sync;
}

bool cbm_mapped_file_open(const char *path, CbmMappedFile *file)
{
        if (!file) {
                return false;
        }
        int fd = -1;
        struct stat st = { 0 };
        ssize_t length = -1;
        char *buffer = NULL;

        fd = open(path, O_RDONLY);
        if (fd < 0) {
                return false;
        }
        if (fstat(fd, &st) != 0) {
                close(fd);
                return false;
        }
        length = st.st_size;

        buffer = mmap(NULL, (size_t)length, PROT_READ, MAP_PRIVATE, fd, 0);
        if (!buffer) {
                close(fd);
                return false;
        }
        file->length = (size_t)length;
        file->buffer = buffer;
        file->fd = fd;
        return true;
}

void cbm_mapped_file_close(CbmMappedFile *file)
{
        if (!file) {
                return;
        }
        munmap(file->buffer, file->length);
        close(file->fd);
        memset(file, 0, sizeof(CbmMappedFile));
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
