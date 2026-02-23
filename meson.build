project('Aetheris', 'cpp',
  version : '0.1.0',
  default_options : ['cpp_std=c++20', 'warning_level=2', 'optimization=2'])

# Dependencies
vulkan_dep = dependency('vulkan')
glm_dep    = dependency('glm')


# VMA and vk-bootstrap are header-only / compiled inline
# Put them in thirdparty/
vkb_inc  = include_directories('thirdparty/vk-bootstrap/src')
vma_inc  = include_directories('thirdparty/VulkanMemoryAllocator/include')

subdir('shared')
subdir('client')
#subdir('server')
