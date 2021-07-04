# First things first, set up libnica
libnica_sources = [
    'libnica/src/array.c',
    'libnica/src/hashmap.c',
    'libnica/src/files.c',
    'libnica/src/inifile.c',
    'libnica/src/list.c',
    'libnica/src/nc-string.c',
    'libnica/src/util.c',
]

libnica_includes = [
    include_directories('libnica/src/include'),
    include_directories('libnica/src/include/nica'),
    config_h_dir,
]

libnica = static_library(
    'nica',
    sources: libnica_sources,
    include_directories: libnica_includes,
)

link_libnica = declare_dependency(
    link_with: libnica,
    include_directories: [
        libnica_includes,
    ],
)

# Now lets build libcbm (clr-boot-manager core library)

libcbm_sources = [
    'bootloaders/systemd-class.c',
    'bootloaders/systemd-boot.c',
    'bootloaders/extlinux.c',
    'bootloaders/syslinux.c',
    'bootloaders/syslinux-common.c',
    'bootloaders/mbr.c',
    'bootman/bootman.c',
    'bootman/kernel.c',
    'bootman/sysconfig.c',
    'bootman/timeout.c',
    'bootman/update.c',
    'lib/blkid_stub.c',
    'lib/cmdline.c',
    'lib/files.c',
    'lib/os-release.c',
    'lib/log.c',
    'lib/probe.c',
    'lib/system_stub.c',
    'lib/writer.c',
    'lib/util.c',
]

if with_grub2_backend == true
libcbm_sources += [
    'bootloaders/grub2.c',
]
endif

libcbm_includes = [
    include_directories('bootloaders'),
    include_directories('bootman'),
    include_directories('lib'),
]

libcbm_dependencies = [
    link_libnica,
    dep_blkid,
    dep_btrfs,
]

# Special constraints for efi functionality
if require_efi == true
    libcbm_dependencies += dep_efiboot
    libcbm_dependencies += dep_efivar
    libcbm_sources += [
        'bootloaders/shim-systemd.c',
        'lib/bootvar.c',
    ]
endif

libcbm = static_library(
    'cbm',
    sources: libcbm_sources,
    c_args: ['-D_BOOTMAN_INTERNAL_'],
    dependencies: libcbm_dependencies,
    include_directories: libcbm_includes,
)

link_libcbm = declare_dependency(
    link_with: libcbm,
    include_directories: libcbm_includes + libnica_includes,
)

# Now clr-boot-manager itself
clr_boot_manager_sources = [
    'cli/cli.c',
    'cli/main.c',
    'cli/ops/kernels.c',
    'cli/ops/report_booted.c',
    'cli/ops/timeout.c',
    'cli/ops/update.c',
]


clr_boot_manager = executable(
    'clr-boot-manager',
    sources: clr_boot_manager_sources,
    include_directories: [
        include_directories('cli'),
    ],
    dependencies: link_libcbm,
    install: true,
)
