project('mpd-ynca', 'cpp', default_options : ['cpp_std=gnu++20'])

boost = dependency('boost', modules : ['program_options'], version : '>=1.87.0')
mpd = dependency('libmpdclient')
threads = dependency('threads')

executable(
    'mpd-ynca',
    'mpd-ynca.cpp',
    install : true,
    cpp_args : [
        '-DSYSCONFDIR=' + get_option('sysconfdir'),
        '-Wno-infinite-recursion',
    ],
    dependencies : [
        boost,
        mpd,
        threads
    ]
)

install_data(
    'ynca.conf',
    rename : 'mpd-ynca.conf',
    install_dir : get_option('sysconfdir')
)
