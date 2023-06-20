project('mpd-ynca', 'cpp', default_options : ['cpp_std=gnu++17'])

boost = dependency('boost', modules : ['system', 'program_options'])
mpd = dependency('libmpdclient')
threads = dependency('threads')

executable(
    'mpd-ynca',
    'mpd-ynca.cpp',
    cpp_args : '-DSYSCONFDIR=' + get_option('sysconfdir'),
    dependencies : [boost, mpd, threads],
    install : true
)

install_data(
    'ynca.conf',
    rename : 'mpd-ynca.conf',
    install_dir : get_option('sysconfdir')
)
