glslc = find_program('glslc', required : true)
# Add near the top, after glslc
imgui_inc = include_directories('../thirdparty/imgui', '../thirdparty/imgui/backends')

imgui_src = files(
  '../thirdparty/imgui/imgui.cpp',
  '../thirdparty/imgui/imgui_draw.cpp',
  '../thirdparty/imgui/imgui_tables.cpp',
  '../thirdparty/imgui/imgui_widgets.cpp',
  '../thirdparty/imgui/backends/imgui_impl_glfw.cpp',
  '../thirdparty/imgui/backends/imgui_impl_vulkan.cpp',

)
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
player_vert_spv = custom_target('player_vert_spv',
                                input            : 'shaders/player.vert',
                                output           : 'player_vert.spv',
                                command          : [glslc, '@INPUT@', '-o', '@OUTPUT@'],
                                build_by_default : true)
 
player_frag_spv = custom_target('player_frag_spv',
                                input            : 'shaders/player.frag',
                                output           : 'player_frag.spv',
                                command          : [glslc, '@INPUT@', '-o', '@OUTPUT@'],
                                build_by_default : true)
tree_trunk_vert_spv = custom_target('tree_trunk_vert_spv',
    input: 'shaders/tree_trunk.vert', output: 'tree_trunk_vert.spv',
    command: [glslc, '@INPUT@', '-o', '@OUTPUT@'], build_by_default: true)

tree_trunk_frag_spv = custom_target('tree_trunk_frag_spv',
    input: 'shaders/tree_trunk.frag', output: 'tree_trunk_frag.spv',
    command: [glslc, '@INPUT@', '-o', '@OUTPUT@'], build_by_default: true)

tree_leaf_vert_spv = custom_target('tree_leaf_vert_spv',
    input: 'shaders/tree_leaf.vert', output: 'tree_leaf_vert.spv',
    command: [glslc, '@INPUT@', '-o', '@OUTPUT@'], build_by_default: true)

tree_leaf_frag_spv = custom_target('tree_leaf_frag_spv',
    input: 'shaders/tree_leaf.frag', output: 'tree_leaf_frag.spv',
    command: [glslc, '@INPUT@', '-o', '@OUTPUT@'], build_by_default: true)
water_vert_spv = custom_target('water_vert_spv',
    input: 'shaders/water.vert', output: 'water_vert.spv',
    command: [glslc, '@INPUT@', '-o', '@OUTPUT@'], build_by_default: true)

water_frag_spv = custom_target('water_frag_spv',
    input: 'shaders/water.frag', output: 'water_frag.spv',
    command: [glslc, '@INPUT@', '-o', '@OUTPUT@'], build_by_default: true)
# ── Client executable ─────────────────────────────────────────────────────────
client_src = files(
  'src/main.cpp',
  'src/mesh_builder.cpp',
  'src/window.cpp',
  'src/vk_init.cpp',
'src/water_renderer.cpp',

'src/tree_renderer.cpp',
'src/main_menu.cpp',
  'src/input.cpp',
  'src/player.cpp',
  'src/view_model.cpp',
  '../thirdparty/vk-bootstrap/src/VkBootstrap.cpp',
)
foreach asset : ['atlas.png', 'arm.glb', 'player.glb']
  configure_file(
    input  : '../' + asset,
    output : asset,
    copy   : true
  )
endforeach
executable('client', client_src + imgui_src,
           include_directories : [
             'include',
             vkb_inc,
             vma_inc,
             vk_headers_inc,
             entt_inc,
             tinygltf_inc,
             imgui_inc,
           ],
           dependencies : [vulkan_dep, glfw_dep, glm_dep, enet_dep, platform_deps, shared_dep],
           link_depends : [terrain_vert_spv, terrain_frag_spv,
                  viewmodel_vert_spv, viewmodel_frag_spv,
                   player_vert_spv, player_frag_spv],
           install      : true)
