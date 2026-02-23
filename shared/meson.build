shared_src = files(
  'src/chunk.cpp',
  'src/marching_cubes.cpp',
)

shared_lib = static_library('shared', shared_src,
  include_directories : ['include', vma_inc],
  dependencies : [glm_dep])

shared_dep = declare_dependency(
  link_with : shared_lib,
  include_directories : ['include'])
