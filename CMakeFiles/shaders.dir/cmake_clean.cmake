file(REMOVE_RECURSE
  "CMakeFiles/shaders"
  "shaders/spv/blit_fragment.spv"
  "shaders/spv/blit_vertex.spv"
  "shaders/spv/bvh_aabb_compute.spv"
  "shaders/spv/bvh_build_compute.spv"
  "shaders/spv/bvh_morton_compute.spv"
  "shaders/spv/bvh_sort_compute.spv"
  "shaders/spv/forward_fragment.spv"
  "shaders/spv/forward_vertex.spv"
  "shaders/spv/hud_vertex.spv"
  "shaders/spv/point_light.spv"
  "shaders/spv/raytrace_compute.spv"
  "shaders/spv/sprite_vertex.spv"
)

# Per-language clean rules from dependency scanning.
foreach(lang )
  include(CMakeFiles/shaders.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
