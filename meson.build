project('simple-cc-app', 'c', default_options : ['c_std=c11'])

cc = meson.get_compiler('c')

device_code_build_script = meson.current_source_dir() / 'build_device_code.sh'
device_build_dir = meson.current_build_dir() / 'device_build'
pcc_app_dev_src_dir = meson.current_source_dir() / 'device'
doca_dep = dependency('doca-common')
doca_pcc_dep = dependency('doca-pcc')
doca_argp_dep = dependency('doca-argp')
flexio_dep = dependency('libflexio')
doca_lib_dir = doca_dep.get_variable(pkgconfig : 'libdir')

enable_tx_counter_sampling = get_option('enable_tx_counter_sampling') ? 'true' : 'false'
dpacc_mcpu_flag = get_option('dpacc_mcpu_flag')
if dpacc_mcpu_flag == ''
    dpacc_mcpu_flag = 'nv-dpa-bf3'
endif

# Build device program (DPACC)
pcc_app_name = 'pcc_simple_cc_app'
pcc_dev_stubs_keep_dir_path = device_build_dir / pcc_app_name
app_builds = find_program(device_code_build_script)
run_command(app_builds,
    pcc_app_dev_src_dir,
    device_build_dir,
    doca_lib_dir,
    pcc_app_name,
    pcc_dev_stubs_keep_dir_path,
    enable_tx_counter_sampling,
    dpacc_mcpu_flag,
    check: true)

pcc_simple_cc_app = cc.find_library(pcc_app_name, dirs : pcc_dev_stubs_keep_dir_path, static: true)

host_srcs = files(
    'host/pcc.c',
    'host/pcc_core.c',
)

host_inc = include_directories('host')
flexio_inc = include_directories('/opt/mellanox/flexio/include')

executable('simple_cc_host',
    host_srcs,
    include_directories : [host_inc, flexio_inc],
    dependencies : [doca_dep, doca_pcc_dep, doca_argp_dep, flexio_dep, pcc_simple_cc_app],
    install : true,
)
