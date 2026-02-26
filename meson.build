project('Aetheris', 'cpp',
        version : '0.1.0',
        default_options : ['cpp_std=c++20', 'warning_level=2', 'optimization=2'])

cc = meson.get_compiler('cpp')

# ── Core deps ─────────────────────────────────────────────────────────────────
vulkan_dep = dependency('vulkan')
glm_dep    = dependency('glm', fallback : ['glm', 'glm_dep'])
enet_dep   = dependency('enet', required: true, static: true)
glfw_dep   = dependency('glfw3', fallback : ['glfw', 'glfw_dep'], static: true)

# ── Windows-only link deps ────────────────────────────────────────────────────
platform_deps = []
if host_machine.system() == 'windows'
  platform_deps += cc.find_library('ws2_32')
  platform_deps += cc.find_library('winmm')
  add_project_link_arguments(
      '-static-libgcc', '-static-libstdc++',
      language: 'cpp')
endif

# ── Third-party include dirs ──────────────────────────────────────────────────
vkb_inc        = include_directories('thirdparty/vk-bootstrap/src')
vma_inc        = include_directories('thirdparty/VulkanMemoryAllocator/include')
vk_headers_inc = include_directories('thirdparty/Vulkan-Headers/include')
entt_inc       = include_directories('thirdparty/entt')

subdir('shared')
subdir('client')
subdir('server')