py_mod = import('python')
py3 = py_mod.find_installation('python3')

module_data = [
]

#
# Generic Python module setup
#
module_name = fs.name(meson.current_source_dir()).to_lower().underscorify().replace('_', '-')
mod_install_dir = join_paths(sy_modules_dir, fs.name(meson.current_source_dir()))

# Define Orbbec SDK build and install paths
orbbec_sdk_source = '/home/llorentelab/Desktop/pyorbbecsdk'
orbbec_sdk_build = orbbec_sdk_source / 'build'
orbbec_sdk_install = orbbec_sdk_source / 'install'

# Declare Orbbec SDK as a dependency
orbbec_sdk_path = orbbec_sdk_install / 'lib'
orbbec_sdk_dep = declare_dependency(
    include_directories: include_directories(orbbec_sdk_source / 'sdk' / 'include'),
    link_args: ['-L' + orbbec_sdk_path, '-lpyorbbecsdk']
)

# Set Python path to include Orbbec SDK
env = environment()
env.prepend('PYTHONPATH', orbbec_sdk_path)

# Install udev rules (this might need to be done separately with elevated privileges)
udev_rules_install = run_command('sudo', 'bash', orbbec_sdk_source / 'scripts' / 'install_udev_rules.sh', check: false)
if udev_rules_install.returncode() == 0
    run_command('sudo', 'udevadm', 'control', '--reload-rules', check: false)
    run_command('sudo', 'udevadm', 'trigger', check: false)
endif

# Install the module
install_subdir(meson.current_source_dir(),
    install_dir: mod_install_dir,
    strip_directory: true,
    exclude_files: ['meson.build', 'pyorbbecsdk']
)

# Create a symbolic link
run_command(ln_exe,
    ['-snf',
    meson.current_source_dir(),
    join_paths(meson.current_build_dir(), '..', module_name + '.srclink')],
    check: true
)