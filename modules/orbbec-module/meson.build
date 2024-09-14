# Specify OrbbecSDK path
# todo: generalize this
# this is convenient for now bc it makes building over and over again easy but still
orbbec_sdk_path = '/home/llorentelab/Desktop/OrbbecSDK'

# Create custom OrbbecSDK dependency
orbbec_sdk_dep = declare_dependency(
    include_directories: include_directories(orbbec_sdk_path + '/include'),
    link_args: ['-L' + orbbec_sdk_path + '/lib/linux_x64', '-lOrbbecSDK']
)

# Build definitions for module: camera-generic
module_hdr = [
    'orbbecmodule.h',
    'orbbecSettingsDialog.h',
]
module_moc_hdr = [
    'orbbecmodule.h',
    'orbbecSettingsDialog.h',
]

module_src = [
    'orbbecmodule.cpp',
    'orbbecSettingsDialog.cpp',
]

module_moc_src = [
    'orbbecmodule.cpp',
    'orbbecSettingsDialog.cpp',
]
module_ui = []

v4l_dep = dependency('libv4l2')
module_deps = [
    opencv_dep,
    orbbec_sdk_dep,
    v4l_dep,
]
module_data = []

# Generic module setup
module_name = fs.name(meson.current_source_dir()).to_lower().underscorify().replace('_', '-')
mod_install_dir = join_paths(sy_modules_dir, fs.name(meson.current_source_dir()))
module_moc = qt.preprocess(
    moc_headers: module_moc_hdr,
    moc_sources: module_moc_src,
    ui_files: module_ui,
    qresources: module_res,
    dependencies: module_deps,
    moc_extra_arguments: ['--no-notes'],
)

orbbec_sdk_inc = include_directories(orbbec_sdk_path + '/include')

mod = shared_module(module_name,
    [module_hdr, module_moc_hdr,
     module_src, module_moc_src,
     module_moc],
    name_prefix: '',
    dependencies: [syntalos_fabric_dep,
                   module_deps],
    include_directories: [orbbec_sdk_inc],
    cpp_args: module_args,
    install: true,
    install_dir: mod_install_dir,
)

mod_data = configuration_data()
mod_data.set('lib_name', fs.name(mod.full_path()))
configure_file(
    input: module_lib_def_tmpl,
    output: 'module.toml',
    configuration: mod_data,
    install: true,
    install_dir: mod_install_dir
)
install_data(
    module_data,
    install_dir: mod_install_dir,
    preserve_path: true
)
foreach fname : module_data
    fs.copyfile(fname)
endforeach

module_hdr = []
module_src = []
module_moc_hdr = []
module_moc_src = []
module_ui = []
module_res = []
module_deps = []
module_args = []
