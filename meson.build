project('grep', 'c',
  default_options: [
    'c_std=gnu11',
    'warning_level=2',
    'werror=true',
  ],
)

source = [
  'grep.c',
  'grep_impl.c',
]

header = [
  'grep_impl.h',
]

deps = [
  dependency('glib-2.0'),
]

cc_flags = [
  '-Wall',
  '-O0',
  '-D_GNU_SOURCE',
]

executable('grep',
  source + header,
  dependencies: deps
)
