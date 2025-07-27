# SPDX-License-Identifier: CC0-1.0

gpiod_dep = dependency('gpiod', required : false)
hw_sync_cpp_args = []
if gpiod_dep.found()
    hw_sync_cpp_args += ['-DHAVE_GPIOD']
endif

hw_sync = executable('libcamera-hw-sync', 'libcamera-hw-sync.cpp',
                     install : true,
                     install_tag : 'bin',
                     dependencies : gpiod_dep,
                     cpp_args : hw_sync_cpp_args)
