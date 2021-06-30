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

#include <dirent.h>

#include "nica/array.h"
#include "nica/hashmap.h"
#include "probe.h"
#include "util.h"

typedef struct BootManager BootManager;

typedef enum {
        BOOTLOADER_OPERATION_MIN = 1 << 0,
        BOOTLOADER_OPERATION_REMOVE = 1 << 1,
        BOOTLOADER_OPERATION_INSTALL = 1 << 2,
        BOOTLOADER_OPERATION_UPDATE = 1 << 3,
        BOOTLOADER_OPERATION_NO_CHECK = 1 << 4,
        BOOTLOADER_OPERATION_MAX = 1 << 5
} BootLoaderOperation;

typedef enum {
        FSTYPE_VFAT = 1 << 0,
        FSTYPE_EXT2 = 1 << 1,
        FSTYPE_EXT3 = 1 << 2,
        FSTYPE_EXT4 = 1 << 3,
} FilesystemType;

/**
 * Maximum length for each component in a kernel identifier
 */
#define CBM_KELEM_LEN 31

/**
 * Represents the currently running system kernel
 */
typedef struct SystemKernel {
        char version[CBM_KELEM_LEN + 1]; /**<Current version number */
        char ktype[CBM_KELEM_LEN + 1];   /**<Kernel type */
        int release;                     /**<Release number */
} SystemKernel;

/**
 * Represents a kernel in it's complete configuration
 */
typedef struct Kernel {
        /* Metadata */
        struct {
                char *bpath;   /**<Basename of this kernel path */
                char *version; /**<Version of this kernel */
                int release;   /**<Release number of this kernel */
                char *ktype;   /**<Type of this kernel */
                char *cmdline; /**<Contents of the cmdline file */
                bool boots;    /**<Is this known to boot? */
        } meta;

        /* Source paths */
        struct {
                char *path;             /**<Path to this kernel */
                char *cmdline_file;     /**<Path to the cmdline file */
                char *kconfig_file;     /**<Path to the kconfig file */
                char *initrd_file;      /**<System initrd file */
                char *user_initrd_file; /**<User's initrd file */
                char *kboot_file;       /**<Path to the k_booted_$(uname -r) file */
                char *module_dir;       /**<Path to the modules directory */
                char *sysmap_file;      /**<Path to the System.map file */
                char *vmlinux_file;     /**<Path to the vmlinux file */
                char *headers_dir;      /**<Path to the kernels header directory */
        } source;

        /* Target (basename) paths */
        struct {
                char *path;        /**<Basename path of the kernel for the target */
                char *legacy_path; /**<Old path prior to namespacing (basename) */
                char *initrd_path; /**<Basename path of initrd for the target */
        } target;
} Kernel;

typedef NcArray KernelArray;

/**
 * Represenative of the system configuration of a given target prefix.
 * This is populated upon examination by @boot_manager_set_prefix.
 */
typedef struct SystemConfig {
        char *prefix;                /**<Prefix for all operations */
        CbmDeviceProbe *root_device; /**<The physical root device */
        char *boot_device;           /**<The physical boot device */
        int wanted_boot_mask;        /**<The required bootloader mask */
} SystemConfig;

/**
 * Construct a new BootManager
 *
 * @note In no way is this API supposed to be used in a parallel fashion.
 * The bootloader implementation is reliant on a static config in an extern
 * module, and we do not create "new" instances here. We simply encapsulate
 * much through an OOP-like interface to make relevant bits available
 * throughout the architecture.
 */
BootManager *boot_manager_new(void);

/**
 * Free an already constructed BootManager
 */
void boot_manager_free(BootManager *manager);

/**
 * Display all the installed kernel files
 *
 * @note In future this will switch to an int-error return for integration
 * with other tooling.
 *
 * @return NULL terminated array of allocated strings or NULL.
 */
char **boot_manager_list_kernels(BootManager *manager);

/**
 * Main actor of the operation, apply all relevant update and GC operations
 *
 * @note In future this will switch to an int-error return for integration
 * with other tooling.
 *
 * @return True if the operation succeeded.
 */
bool boot_manager_update(BootManager *manager);

/**
 * Update the uname for this BootManager
 *
 * @note We already initialise with the host uname on creation, however this
 * uname may be overridden for the purposes of validation
 *
 * @param uname The new uname to use (utsname.release)
 * @return True if this was a valid uname to use and we could parse it.
 */
bool boot_manager_set_uname(BootManager *manager, const char *uname);

/**
 * Set the prefix to apply to all filesystem paths
 *
 * @note The path must exist for this function call to work
 *
 * @param prefix Path to use for prefix operations
 *
 * @return True if the operation succeeded
 */
bool boot_manager_set_prefix(BootManager *manager, char *prefix);

/**
 * Override the internal boot directory, forcing a reconfiguration.
 * This should only be used if the client has no need to mount the configured
 * ESP, i.e. because it is already mounted by the user
 */
bool boot_manager_set_boot_dir(BootManager *manager, const char *bootdir);

/**
 * Get the current wanted boot mask
 *
 * @return wanted boot mask (gpt/legacy/uefi, fstype) mask
 */
int boot_manager_get_wanted_boot_mask(BootManager *self);

/**
 * Get the current filesystem prefix
 *
 * @note This string is owned by BootManager, do not modify or free
 *
 * @return current prefix
 */
const char *boot_manager_get_prefix(BootManager *manager);

/**
 * Return the location used for kernel probing, incorporating the
 * prefix
 *
 * @note This string is owned by BootManager, do not modify or free
 *
 * @return current kernel directory
 */
const char *boot_manager_get_kernel_dir(BootManager *manager);

/**
 * Return the vendor prefix, if any
 *
 * @note This string is owned by BootManager, do not modify or free
 */
const char *boot_manager_get_vendor_prefix(BootManager *manager);

/**
 * Set the OS name
 */
void boot_manager_set_os_name(BootManager *manager, char *os_name);

/**
 * Return the OS name
 *
 * @note This string is owned by BootManager, do not modify or free
 */
const char *boot_manager_get_os_name(BootManager *manager);

/**
 * Return the OS ID (used in class values)
 *
 * @note This string is owned by BootManager, do not modify or free
 */
const char *boot_manager_get_os_id(BootManager *self);

/**
 * Discover a list of known kernels
 *
 * @return a newly allocated NcArray of Kernel's
 */
KernelArray *boot_manager_get_kernels(BootManager *manager);

/**
 * Detect potential kernel availability, returning a bool based on results
 *
 * @note Looks for kernel directory rather than kernel files
 *
 * @param path Path to use as prefix
 * @return a bool indicating if the kernel directory exists
 */
bool boot_manager_detect_kernel_dir(char *path);

/**
 * Inspect a kernel file, returning a machine-usable description
 *
 * @note free the returned struct with free_kernel
 *
 * @param path Path of the file to inspect
 * @return a newly allocated Kernel, or NULL if this is an invalid kernel
 */
Kernel *boot_manager_inspect_kernel(BootManager *manager, char *path);

/**
 * Attempt installation of the given kernel
 *
 * @param kernel A valid kernel instance
 *
 * @return a boolean value, indicating success of failure
 */
bool boot_manager_install_kernel(BootManager *manager, const Kernel *kernel);

/**
 * Return the fully qualified boot directory, including the prefix
 */
char *boot_manager_get_boot_dir(BootManager *manager);

/**
 * Return the bootloader private data
 */
void *boot_manager_get_data(BootManager *manager);

/**
 * Set the bootloader's private data
 */
void boot_manager_set_data(BootManager *manager, void *data);

/**
 * Attempt to uninstall a previously installed kernel
 *
 * @param kernel A valid kernel instance
 *
 * @return a boolean value, indicating success or failure
 */

bool boot_manager_remove_kernel(BootManager *manager, const Kernel *kernel);

/**
 * Attempt to set the default kernel entry
 */
bool boot_manager_set_default_kernel(BootManager *manager, const Kernel *kernel);

/**
 * Attempt to get the default kernel entry
 */
char *boot_manager_get_default_kernel(BootManager *manager);

/**
 * Return the CbmDeviceProbe for the root partition
 *
 * @note This struct belongs to BootManager and should not be freed. Also
 * note that it is only initialised during @boot_manager_set_prefix
 */
const CbmDeviceProbe *boot_manager_get_root_device(BootManager *manager);

/**
 * Attempt installation of the bootloader
 */
bool boot_manager_modify_bootloader(BootManager *manager, int ops);

/**
 * Set boot manager flag, determining if it should or not update efi variables
 */
void boot_manager_set_update_efi_vars(BootManager *self, bool update_efi_vars);

/**
 * Returns the boot manager's update_efi_vars flag
 */
bool boot_manager_is_update_efi_vars(BootManager *self);

/**
 * Determine the default timeout based on the contents of
 * SYSCONFDIR/boot_timeout
 */
int boot_manager_get_timeout_value(BootManager *manager);

/**
 * Determine the default kernel for the given type if it is in the set
 * This does not create a new instance, simply a pointer to the existing
 * kernel in the @kernels set.
 */
Kernel *boot_manager_get_default_for_type(BootManager *manager, KernelArray *kernels,
                                          const char *type);

/**
 * Map a discovered set of kernels into type->kernelarray.
 * @note These new KernelArray's only contain references to the existing
 * kernels in the mapping.
 */
NcHashmap *boot_manager_map_kernels(BootManager *manager, KernelArray *kernels);

/**
 * Set the timeout to be used in the bootloader
 *
 * @param timeout New timeout value
 */
bool boot_manager_set_timeout_value(BootManager *manager, int timeout);

bool boot_manager_needs_install(BootManager *manager);

bool boot_manager_needs_update(BootManager *manager);

/**
 * Get the SystemKernel, if any, for this BootManager.
 *
 * @note This should not be freed, this is static memory
 */
const SystemKernel *boot_manager_get_system_kernel(BootManager *manager);

/**
 * Attempt to find the currently matching kernel from the given kernel array
 * @note This just returns a pointer, this should not be freed.
 */
Kernel *boot_manager_get_running_kernel(BootManager *manager, KernelArray *kernels);

/**
 * Attempt to find the currently matching kernel from the given kernel array
 * This particular approach will not attempt to match the version, only the type
 * and release, as a fallback safety blanket.
 *
 * @note This just returns a pointer, this should not be freed.
 */
Kernel *boot_manager_get_running_kernel_fallback(BootManager *manager, KernelArray *kernels);

/**
 * Attempt to find the newest (highest release number) last known booting
 * kernel.
 */
Kernel *boot_manager_get_last_booted(BootManager *manager, KernelArray *kernels);

/**
 * Parse the running kernel and try to figure out the type, etc.
 */
bool cbm_parse_system_kernel(const char *inp, SystemKernel *kernel);

/**
 * Free a previously allocated sysconfig
 */
void cbm_free_sysconfig(SystemConfig *config);

/**
 * Inspect a given root path and return a new SystemConfig for it
 */
SystemConfig *cbm_inspect_root(const char *path);

/**
 * Determine if the given SystemConfig is sane for use
 */
bool cbm_is_sysconfig_sane(SystemConfig *config);

/**
 * Free a kernel type
 */
void free_kernel(Kernel *t);

static inline void kernel_array_free(void *v)
{
        KernelArray *a = v;
        nc_array_free(&a, (array_free_func)free_kernel);
}

/**
 * Enumerates freestanding initrds. A "freestanding" initrd is an initrd which is
 * not associated with a specific version of the kernel and is added to each
 * boot configuration entry. Such initrd is expected to not contain any kernel
 * modules. There could be any number of freestanding initrds configured.
 * They are appended in arbitrary order after the kernel-specific initrd in the
 * bootloader configuration file.
 */
bool boot_manager_enumerate_initrds_freestanding(BootManager *self);

/**
 * Copy freestanding initrd
 */
bool boot_manager_copy_initrd_freestanding(BootManager *self);

/**
 * Remove old freestanding initrd
 */
bool boot_manager_remove_initrd_freestanding(BootManager * self);

/*
 * Iterate initrd elements
 */
void boot_manager_initrd_iterator_init(const BootManager *manager, NcHashmapIter *iter);

/**
 * Get freestanding initrd to bootloader entry
 */
bool boot_manager_initrd_iterator_next(NcHashmapIter *iter, char **name);

DEF_AUTOFREE(BootManager, boot_manager_free)
DEF_AUTOFREE(KernelArray, kernel_array_free)
DEF_AUTOFREE(Kernel, free_kernel)
DEF_AUTOFREE(DIR, closedir)

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
