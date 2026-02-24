project('Aetheris', 'cpp',
  version : '0.1.0',
  default_options : ['cpp_std=c++20', 'warning_level=2', 'optimization=2'])

vulkan_dep = dependency('vulkan')
glm_dep    = dependency('glm')
enet_dep   = dependency('libenet')

vkb_inc        = include_directories('thirdparty/vk-bootstrap/src')
vma_inc        = include_directories('thirdparty/VulkanMemoryAllocator/include')
vk_headers_inc = include_directories('thirdparty/Vulkan-Headers/include')
entt_inc       = include_directories('thirdparty/entt')

subdir('shared')
subdir('client')
subdir('server')
