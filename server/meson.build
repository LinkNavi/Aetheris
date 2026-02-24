enet_dep = dependency('libenet')

server_src = files(
  'src/main.cpp',
  'src/chunk_manager.cpp',
)

executable('server', server_src,
  include_directories : ['include'],
  dependencies : [glm_dep, enet_dep, shared_dep])
