glfw_dep = dependency('glfw3')

client_src = files(
  'src/main.cpp',
  'src/window.cpp',
  'src/vk_init.cpp',
  '../thirdparty/vk-bootstrap/src/VkBootstrap.cpp',
)

executable('client', client_src,
  include_directories : ['include', vkb_inc, vma_inc, vk_headers_inc],
  dependencies : [vulkan_dep, glfw_dep, glm_dep, shared_dep])
