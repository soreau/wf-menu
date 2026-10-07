project(
    'wf-menu',
    'c',
    'cpp',
    version: '0.1.0',
    license: 'MIT',
    meson_version: '>=0.51.0',
    default_options: [
        'cpp_std=c++17',
    ],
)

add_project_arguments(['-DWAYFIRE_PLUGIN', '-I' + join_paths(meson.project_source_root(), 'include')], language: ['cpp'])
add_project_link_arguments(['-rdynamic','-fPIC'], language:'cpp')

wayland_client = dependency('wayland-client')
wayfire = dependency('wayfire', version: '>=0.11.0')
gtkmm  = dependency('gtkmm-4.0')

subdir('proto')
subdir('client')
subdir('plugin')
subdir('metadata')
