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

#pragma once

#define _GNU_SOURCE

#include <stdbool.h>
#include <sys/types.h>

#include "util.h"

/**
 * A CbmCbmDeviceProbe is the result of a cbm_probe_path operation, caching
 * fields useful to clr-boot-manager in terms of partition analysis.
 */
typedef struct CbmDeviceProbe {
        char *uuid;      /**< UUID for all partition types */
        char *part_uuid; /**< PartUUID for GPT partitions */
        char *luks_uuid; /**< Parent LUKS UUID for the partition */
        char *btrfs_sub; /**< Btrfs subvolume of the rootfs */
        bool gpt;        /**<Whether this device belongs to a GPT disk */
} CbmDeviceProbe;

/**
 * Probe the given path for all relevant information
 */
CbmDeviceProbe *cbm_probe_path(const char *path);

/**
 * Free an existing probe
 */
void cbm_probe_free(CbmDeviceProbe *probe);

DEF_AUTOFREE(CbmDeviceProbe, cbm_probe_free)

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
