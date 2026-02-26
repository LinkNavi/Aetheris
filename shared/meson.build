shared_src = files(
  'src/chunk.cpp',
  'src/marching_cubes.cpp',
  'src/noise_gen.cpp',
  'src/gltf_loader.cpp',
)

stb_inc = include_directories('../thirdparty')

shared_lib = static_library('shared', shared_src,
  include_directories : ['include', vma_inc, vk_headers_inc, stb_inc, tinygltf_inc],
  dependencies        : [glm_dep])

shared_dep = declare_dependency(
  link_with           : shared_lib,
  include_directories : ['include'])
