# libtermux-render and termux/render/* (render.h, buffer.h, tlog) are on system include path.
cc = meson.get_compiler('c')
termux_dep = cc.find_library('termux-render', required: true)
termux_inc = include_directories('.')

wlr_deps += [termux_dep, declare_dependency(include_directories: termux_inc)]
wlr_files += files(
  'backend.c',
  'input.c',
  'output.c',
  'termux_render.c',
)

features += { 'termux-backend': true }