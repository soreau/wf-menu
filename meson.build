project(
    'wf-menu',
    'c',
    'cpp',
    version: '0.1.0',
    license: 'MIT',
    meson_version: '>=0.51.0',
    default_options: [
        'cpp_std=c++17',
        'c_std=c11',
    ],
)

add_project_arguments(['-DWAYFIRE_PLUGIN'], language: ['cpp', 'c'])
add_project_link_arguments(['-rdynamic','-fPIC'], language:'cpp')

wayland_client = dependency('wayland-client')
wayfire = dependency('wayfire', version: '>=0.11.0')
gtkmm  = dependency('gtkmm-4.0')

subdir('proto')
subdir('client')
subdir('plugin')
subdir('metadata')
