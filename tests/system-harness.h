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

#pragma once

#include "system_stub.h"

static inline int test_mount(__cbm_unused__ const char *source, __cbm_unused__ const char *target,
                             __cbm_unused__ const char *filesystemtype,
                             __cbm_unused__ unsigned long mountflags,
                             __cbm_unused__ const void *data)
{
        return 0;
}

static inline int test_umount(__cbm_unused__ const char *target)
{
        return 0;
}

static inline int test_system(__cbm_unused__ const char *command)
{
        return 0;
}

/**
 * Default vtable for testing. Copy into a local struct and override specific
 * fields.
 */
CbmSystemOps SystemTestOps = {
        .mount = test_mount, .umount = test_umount, .system = test_system,
};

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
