server_src = files(
  'src/main.cpp',
  'src/network.cpp',
  'src/world.cpp',
)

executable('server', server_src,
  include_directories : ['include'],
  dependencies : [glm_dep, shared_dep])
