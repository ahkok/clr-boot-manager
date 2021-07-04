project(
    'clr-boot-manager',
    ['c'],
    version: run_command('cat', 'VERSION').stdout().split()[0],
    license: [
        'LGPL-2.1',
    ],
    default_options: [
        'c_std=c11',
        'prefix=/usr',
        'sysconfdir=/etc',
        'datadir=/usr/share',
        'with-kernel-conf-dir=/etc/kernel',
        'with-kernel-vendor-conf-dir=/usr/share/kernel',
    ],
)

# Ensure a sane minimum set of flags exists
am_cflags = [
    '-fstack-protector',
    '-pedantic',
    '-Wstrict-prototypes',
    '-Wundef',
    '-fno-common',
    '-Werror-implicit-function-declaration',
    '-Wformat',
    '-Wformat-security',
    '-Werror=format-security',
    '-Wconversion',
    '-Wunused-variable',
    '-Wunreachable-code',
    '-Wall',
    '-W',
    '-D_FORTIFY_SOURCE=2',
    '-Wno-missing-field-initializers',
]

add_global_arguments(am_cflags, language: 'c')

# Cache compiler object
ccompiler = meson.get_compiler('c')

# pkgconfig deps
dep_blkid = dependency('blkid')
dep_check = dependency('check', version: '>= 0.9')

# other deps
dep_btrfs = ccompiler.find_library('btrfsutil')
if not ccompiler.has_header('btrfsutil.h')
    error('Cannot find btrfsutil.h. Is btrfs-progs-dev(el) installed?')
endif

# Grab necessary paths
path_prefix = get_option('prefix')
path_bindir = join_paths(path_prefix, get_option('bindir'))
path_sysconfdir = join_paths(path_prefix, get_option('sysconfdir'))
path_datadir = join_paths(path_prefix, get_option('datadir'))

# Configure project details
cdata = configuration_data()
cdata.set_quoted('PACKAGE_NAME', meson.project_name())
cdata.set_quoted('PACKAGE_VERSION', meson.project_version())
cdata.set_quoted('SYSCONFDIR', path_sysconfdir)

require_efi = false

# What bootloader are we using?
with_bootloader = get_option('with-bootloader')
if with_bootloader == 'systemd-boot'
    cdata.set('HAVE_SYSTEMD_BOOT', 1)
elif with_bootloader == 'shim-systemd-boot'
    cdata.set('HAVE_SHIM_SYSTEMD_BOOT', 1)
    cdata.set('HAVE_SYSTEMD_BOOT', 1)
    require_efi = true
endif

# Let's find gnu-efi / efi-var
if require_efi == true
    message('EFI variable support required, looking for headers/libs')
    dir_gnu_efi = get_option('with-gnu-efi')
    if dir_gnu_efi == ''
        dir_gnu_efi = '/usr/include/efi'
    endif
    dir_efivar = get_option('with-efi-var')
    if dir_efivar == ''
        dir_efivar =  '/usr/include/efivar'
    endif

    dep_efivar = ccompiler.find_library('efivar')
    dep_efiboot = ccompiler.find_library('efiboot')

    if not ccompiler.has_header('efiboot.h', args: '-I@0@'.format(dir_efivar))
        error('Cannot find efiboot.h. Is efivar-dev(el) installed?')
    endif
    if not ccompiler.has_header('efivar.h', args: '-I@0@'.format(dir_efivar))
        error('Cannot find efivar.h. Is efivar-dev(el) installed?')
    endif
    if not ccompiler.has_header('efi.h', args: '-I@0@'.format(dir_gnu_efi))
        error('Cannot find efi.h. Is gnu-efi-dev(el) installed?')
    endif

    # Ensure we add to the global cflags so we can compile with efi bits
    add_global_arguments(['-isystem', dir_efivar], language: 'c')
    add_global_arguments(['-isystem', dir_gnu_efi], language: 'c')

    # Require architecture specific bits too
    gnu_efi_arch = join_paths(dir_gnu_efi, build_machine.cpu_family())
    add_global_arguments(['-isystem', gnu_efi_arch], language: 'c')
    message('gnu efi arch: @0@'.format(gnu_efi_arch))

    # Required for efivar as they're not defining GNU_SOURCE, etc.
    add_global_arguments(['-D_POSIX_C_SOURCE=201112L'], language: 'c')
endif

# Defaults to /usr/lib/kernel
with_kernel_dir = get_option('with-kernel-dir')
if with_kernel_dir == ''
    with_kernel_dir = join_paths(path_prefix, 'lib', 'kernel')
endif

# Defaults to /usr/lib/initrd.d
with_initrd_dir = get_option('with-initrd-dir')
if with_initrd_dir == ''
    with_initrd_dir = join_paths(path_prefix, 'lib', 'initrd.d')
endif

# Defaults to /etc/kernel/initrd.d
with_user_initrd_dir = get_option('with-user-initrd-dir')
if with_user_initrd_dir == ''
    with_user_initrd_dir = join_paths(path_sysconfdir, 'kernel', 'initrd.d')
endif

# Defaults to /etc/kernel
with_kernel_conf_dir = get_option('with-kernel-conf-dir')
if with_kernel_conf_dir == ''
    with_kernel_conf_dir = join_paths(path_sysconfdir, 'kernel')
endif

# Defaults to /usr/lib/modules
with_kernel_modules_dir = get_option('with-kernel-modules-dir')
if with_kernel_modules_dir == ''
    with_kernel_modules_dir = join_paths(path_prefix, 'lib', 'modules')
endif

# Defaults to /usr/share/kernel
with_kernel_vendor_conf_dir = get_option('with-kernel-vendor-conf-dir')
if with_kernel_vendor_conf_dir == ''
    with_kernel_vendor_conf_dir = join_paths(path_datadir, 'kernel')
endif

with_boot_dir = get_option('with-boot-dir')
with_kernel_namespace = get_option('with-kernel-namespace')
with_vendor_prefix = get_option('with-vendor-prefix')

# Systemd configuration
with_systemd_system_unit_dir = get_option('with-systemd-system-unit-dir')
if with_systemd_system_unit_dir == ''
    # Automatically discover systemd unit location from pkgconfig
    message('Asking pkg-config for systemd\'s system unit directory')
    dep_systemd = dependency('systemd')
    with_systemd_system_unit_dir = dep_systemd.get_pkgconfig_variable('systemdsystemunitdir')

    if with_systemd_system_unit_dir == ''
        error('Cannot locate the systemd system unit directory')
    endif
endif

# Vendor configuration
with_uefi_entry_label = get_option('with-uefi-entry-label')
if with_uefi_entry_label == ''
   with_uefi_entry_label = 'Linux bootloader'
endif

# Set config.h options up for our general directories, etc.
cdata.set_quoted('KERNEL_DIRECTORY', with_kernel_dir)
cdata.set_quoted('INITRD_DIRECTORY', with_initrd_dir)
cdata.set_quoted('USER_INITRD_DIRECTORY', with_user_initrd_dir)
cdata.set_quoted('KERNEL_MODULES_DIRECTORY', with_kernel_modules_dir)
cdata.set_quoted('KERNEL_NAMESPACE', with_kernel_namespace)
cdata.set_quoted('BOOT_DIRECTORY', with_boot_dir)
cdata.set_quoted('VENDOR_PREFIX', with_vendor_prefix)
cdata.set_quoted('KERNEL_CONF_DIRECTORY', with_kernel_conf_dir)
cdata.set_quoted('VENDOR_KERNEL_CONF_DIRECTORY', with_kernel_vendor_conf_dir)
cdata.set_quoted('UEFI_ENTRY_LABEL', with_uefi_entry_label)

with_grub2_backend = get_option('with-grub2-backend')
if with_grub2_backend == true
   cdata.set('GRUB2_BACKEND_ENABLED', with_grub2_backend)
endif

# Helps the test suites
test_top_dir = meson.current_source_dir()

# Write config.h now
config_h = configure_file(
     configuration: cdata,
     output: 'config.h',
)
config_h_dir = include_directories('.')

# Data files (systemd unit + shell completions + manpages)
subdir('data')
subdir('data/completions/bash')
subdir('data/completions/zsh')
subdir('man')

# Aggregate library + binary configuration
subdir('src')

# Now set up tests
subdir('tests')

report = [
    '    Build configuration:',
    '    ====================',
    '',
    '    version:                                @0@'.format(meson.project_version()),
    '    prefix:                                 @0@'.format(path_prefix),
    '    datadir:                                @0@'.format(path_datadir),
    '    sysconfdir:                             @0@'.format(path_sysconfdir),
    '    systemd unit directory:                 @0@'.format(with_systemd_system_unit_dir),
    '    uefi entry label:                       @0@'.format(with_uefi_entry_label),
    '',
    '    Identification:',
    '    ===============',
    '',
    '    kernel namespace:                       @0@'.format(with_kernel_namespace),
    '    vendor prefix:                          @0@'.format(with_vendor_prefix),
    '',
    '    Directories:',
    '    ============',
    '',
    '    boot directory:                         @0@'.format(with_boot_dir),
    '    kernel directory:                       @0@'.format(with_kernel_dir),
    '    kernel modules directory:               @0@'.format(with_kernel_modules_dir),
    '    kernel config directory:                @0@'.format(with_kernel_conf_dir),
    '    kernel vendor config directory:         @0@'.format(with_kernel_vendor_conf_dir),
    '',
    '    Booting:',
    '    ========',
    '',
    '    bootloader:                             @0@'.format(with_bootloader),
    '    efi variable support:                   @0@'.format(require_efi),
    '    grub backend:                           @0@'.format(with_grub2_backend),
]

# Output some stuff to validate the build config
message('\n\n\n' + '\n'.join(report) + '\n\n')
