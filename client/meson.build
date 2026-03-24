glslc = find_program('glslc', required : true)
imgui_inc = include_directories('../thirdparty/imgui', '../thirdparty/imgui/backends')
imgui_src = files(
  '../thirdparty/imgui/imgui.cpp',
  '../thirdparty/imgui/imgui_draw.cpp',
  '../thirdparty/imgui/imgui_tables.cpp',
  '../thirdparty/imgui/imgui_widgets.cpp',
  '../thirdparty/imgui/backends/imgui_impl_glfw.cpp',
  '../thirdparty/imgui/backends/imgui_impl_vulkan.cpp',
)

foreach s : [
  ['terrain_vert',     'shaders/terrain.vert'],
  ['terrain_frag',     'shaders/terrain.frag'],
  ['viewmodel_vert',   'shaders/viewmodel.vert'],
  ['viewmodel_frag',   'shaders/viewmodel.frag'],
  ['player_vert',      'shaders/player.vert'],
  ['player_frag',      'shaders/player.frag'],
  ['tree_trunk_vert',  'shaders/tree_trunk.vert'],
  ['tree_trunk_frag',  'shaders/tree_trunk.frag'],
  ['tree_leaf_vert',   'shaders/tree_leaf.vert'],
  ['tree_leaf_frag',   'shaders/tree_leaf.frag'],
  ['water_vert',       'shaders/water.vert'],
  ['water_frag',       'shaders/water.frag'],
  ['sky_vert',         'shaders/sky.vert'],
  ['sky_frag',         'shaders/sky.frag'],
  ['godray_vert',      'shaders/godray.vert'],
  ['godray_frag',      'shaders/godray.frag'],
]
  custom_target(s[0] + '_spv',
    input: s[1], output: s[0] + '.spv',
    command: [glslc, '@INPUT@', '-o', '@OUTPUT@'],
    build_by_default: true)
endforeach

client_src = files(
  'src/main.cpp',
  'src/mesh_builder.cpp',
  'src/window.cpp',
  'src/vk_init.cpp',
  'src/tree_renderer.cpp',
  'src/main_menu.cpp',
  'src/input.cpp',
  'src/player.cpp',
  'src/view_model.cpp',
  '../thirdparty/vk-bootstrap/src/VkBootstrap.cpp',
)
foreach asset : ['atlas.png', 'arm.glb', 'player.glb', 'grimoire.glb']
  configure_file(input : '../' + asset, output : asset, copy : true)
endforeach
executable('client', client_src + imgui_src,
           include_directories : [
             'include', vkb_inc, vma_inc, vk_headers_inc,
             entt_inc, tinygltf_inc, imgui_inc, fastnoiselite_inc,
           ],
           dependencies : [vulkan_dep, glfw_dep, glm_dep, enet_dep, platform_deps, shared_dep],
           install : true)
