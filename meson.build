project('project-acorn', 'c',
  version: '0.1.0',
  default_options: [
    'warning_level=3',
    'werror=true',
    'c_std=c11'
  ]
)

# Add compiler flags
add_project_arguments([
  '-Wall',
  '-Wextra',
  '-Werror',
], language: 'c')

# Find SDL2 dependency
sdl2_dep = dependency('sdl2', required: true)

# Source files
sources = [
  '5150emu.c',
  'intel8086.c',
]

# Build executable
executable('acorn',
  sources,
  dependencies: [sdl2_dep],
  install: true
)
