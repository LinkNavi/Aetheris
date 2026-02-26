glslc = find_program('glslc', required : true)

# ── Compile shaders ───────────────────────────────────────────────────────────
terrain_vert_spv = custom_target('terrain_vert_spv',
                                 input            : 'shaders/terrain.vert',
                                 output           : 'terrain_vert.spv',
                                 command          : [glslc, '@INPUT@', '-o', '@OUTPUT@'],
                                 build_by_default : true)

terrain_frag_spv = custom_target('terrain_frag_spv',
                                 input            : 'shaders/terrain.frag',
                                 output           : 'terrain_frag.spv',
                                 command          : [glslc, '@INPUT@', '-o', '@OUTPUT@'],
                                 build_by_default : true)

viewmodel_vert_spv = custom_target('viewmodel_vert_spv',
                                   input            : 'shaders/viewmodel.vert',
                                   output           : 'viewmodel_vert.spv',
                                   command          : [glslc, '@INPUT@', '-o', '@OUTPUT@'],
                                   build_by_default : true)

viewmodel_frag_spv = custom_target('viewmodel_frag_spv',
                                   input            : 'shaders/viewmodel.frag',
                                   output           : 'viewmodel_frag.spv',
                                   command          : [glslc, '@INPUT@', '-o', '@OUTPUT@'],
                                   build_by_default : true)

# ── Client executable ─────────────────────────────────────────────────────────
client_src = files(
  'src/main.cpp',
  'src/mesh_builder.cpp',
  'src/window.cpp',
  'src/vk_init.cpp',
  'src/input.cpp',
  'src/player.cpp',
  'src/view_model.cpp',
  '../thirdparty/vk-bootstrap/src/VkBootstrap.cpp',
)

executable('client', client_src,
           include_directories : [
             'include',
             vkb_inc,
             vma_inc,
             vk_headers_inc,
             entt_inc,
             tinygltf_inc,
           ],
           dependencies : [vulkan_dep, glfw_dep, glm_dep, enet_dep, platform_deps, shared_dep],
           link_depends : [terrain_vert_spv, terrain_frag_spv,
                           viewmodel_vert_spv, viewmodel_frag_spv],
           install      : true)
