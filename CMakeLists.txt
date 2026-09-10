cmake_minimum_required(VERSION 3.27)
project(zquake VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options
option(ZQ_ENABLE_AUDIO "Enable audio subsystem" ON)
option(ZQ_ENABLE_NETWORK "Enable network subsystem" ON)
option(ZQ_ENABLE_TESTS "Enable unit tests" ON)
option(ZQ_BUILD_TOOLS "Build dev tools (QCC)" OFF)

# Warnings
if(MSVC)
    add_compile_options(/W4 /WX)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Platform detection
if(WIN32)
    add_definitions(-DZQ_PLATFORM_WINDOWS)
elseif(UNIX AND NOT APPLE)
    add_definitions(-DZQ_PLATFORM_LINUX)
endif()

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_BINARY_DIR}/shaders/include)

# Find dependencies
find_package(SDL2 REQUIRED)
find_package(Vulkan REQUIRED)

# GLSLang for shader compilation
find_program(GLSLC glslc)
if(NOT GLSLC)
    message(WARNING "glslc not found, shaders will not be compiled")
endif()

# Shader sources
set(SHADER_SRC_DIR "${CMAKE_SOURCE_DIR}/shaders")
set(SHADER_SPV_DIR "${CMAKE_BINARY_DIR}/shaders/spv")
set(SHADER_INC_DIR "${CMAKE_BINARY_DIR}/shaders/include")

# Compile GLSL → SPV at build time
set(SPV_FILES)
if(GLSLC)
    foreach(SHADER forward_vertex.glsl forward_fragment.glsl point_light.glsl sprite_vertex.glsl hud_vertex.glsl raytrace_compute.glsl blit_vertex.glsl blit_fragment.glsl bvh_morton_compute.glsl bvh_sort_compute.glsl bvh_build_compute.glsl bvh_aabb_compute.glsl)
        set(BASE "${SHADER}")
        string(REPLACE ".glsl" "" BASE "${BASE}")
        set(SPV_FILE "${SHADER_SPV_DIR}/${BASE}.spv")
        set(GLSL_FILE "${SHADER_SRC_DIR}/${SHADER}")
        
        # Determine shader stage from filename
        set(SHADER_STAGE "")
        if(SHADER MATCHES ".*vertex.*")
            set(SHADER_STAGE "vertex")
        elseif(SHADER MATCHES ".*fragment.*")
            set(SHADER_STAGE "fragment")
        elseif(SHADER MATCHES ".*light.*")
            set(SHADER_STAGE "vertex")
        elseif(SHADER MATCHES ".*sprite.*")
            set(SHADER_STAGE "vertex")
        elseif(SHADER MATCHES ".*compute.*")
            set(SHADER_STAGE "compute")
        elseif(SHADER MATCHES ".*geometry.*")
            set(SHADER_STAGE "geometry")
        endif()
        
        if(EXISTS "${GLSL_FILE}")
            add_custom_command(
                OUTPUT "${SPV_FILE}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_SPV_DIR}"
                COMMAND ${GLSLC} -fshader-stage=${SHADER_STAGE} -std=450core "${GLSL_FILE}" -o "${SPV_FILE}"
                DEPENDS "${GLSL_FILE}"
                COMMENT "Compiling ${SHADER} to SPIR-V"
                VERBATIM
            )
            list(APPEND SPV_FILES "${SPV_FILE}")
        endif()
    endforeach()
    
    add_custom_target(shaders ALL DEPENDS ${SPV_FILES})
endif()

# Core library
add_library(zq_core STATIC
    src/core/fixed/fixed_t.cpp
    src/core/fixed/fixed_math.cpp
    src/core/fixed/fixed_tables.cpp
    src/core/memory/allocator.cpp
    src/core/container/string.cpp
    src/core/logging/logger.cpp
    src/core/io/file_io.cpp
)

# Filesystem library
add_library(zq_filesystem STATIC
    src/filesystem/virtual_fs.cpp
    src/filesystem/pak_archive.cpp
    src/filesystem/path_util.cpp
)
target_link_libraries(zq_filesystem PUBLIC zq_core)

# Subsystems library
add_library(zq_subsystems STATIC
    src/subsystems/net_addr.cpp
    src/subsystems/net_driver.cpp
    src/subsystems/protocol.cpp
    src/subsystems/reliable_channel.cpp
    src/subsystems/server.cpp
    src/subsystems/client.cpp
)
target_link_libraries(zq_subsystems PUBLIC zq_core)

# Vulkan library
add_library(zq_vulkan STATIC
    src/vulkan/vulkan_api.cpp
    src/vulkan/vulkan_pipeline.cpp
    src/vulkan/vulkan_image.cpp
    src/vulkan/vulkan_buffer.cpp
    src/vulkan/command_pool.cpp
    src/vulkan/render_command.cpp
    src/vulkan/render_thread.cpp
)
target_link_libraries(zq_vulkan PUBLIC zq_core Vulkan::Vulkan SDL2::SDL2)

# Render library
add_library(zq_render STATIC
    src/render/font8x8.cpp
    src/render/software_renderer.cpp
    src/render/ray_scene.cpp
    src/render/raytracer.cpp
    src/app/rt_scene.cpp
)
target_link_libraries(zq_render PUBLIC zq_core SDL2::SDL2 Vulkan::Vulkan)

# Engine library
add_library(zq_engine STATIC
    src/engine/console.cpp
    src/engine/cvar_system.cpp
    src/engine/cvar_parser.cpp
    src/engine/host.cpp
    src/engine/world.cpp
    src/engine/entity.cpp
    src/engine/model.cpp
    src/engine/particle.cpp
    src/engine/game.cpp
    src/engine/input.cpp
    src/engine/bsp.cpp
    src/engine/mdl_model.cpp
    src/engine/move.cpp
    src/engine/gameframe.cpp
    src/engine/game_builtins.cpp
    src/engine/area_nodes.cpp
    src/engine/monster_move.cpp
)
target_link_libraries(zq_engine PUBLIC zq_core zq_filesystem zq_subsystems zq_vulkan zq_vm)

# VM library
add_library(zq_vm STATIC
    src/vm/vm.cpp
    src/vm/bytecode.cpp
    src/vm/opcode.cpp
    src/vm/builtin_registry.cpp
    src/vm/prog_vm.cpp
    src/vm/prog_builtins.cpp
    src/vm/libstd/sv_globals.cpp
    src/vm/libstd/cl_parse.cpp
    src/vm/libstd/pr_edict.cpp
)
target_link_libraries(zq_vm PUBLIC zq_core)

# Main executable
add_executable(zquake
    src/app/entry_point.cpp
    src/app/world_builder.cpp
)
target_link_libraries(zquake PRIVATE zq_vm zq_engine zq_render zq_vulkan zq_subsystems zq_filesystem zq_core SDL2::SDL2)

# Test executable
if(ZQ_ENABLE_TESTS)
    enable_testing()
    
    find_package(Catch2 3.0 QUIET)
    if(NOT Catch2_FOUND)
        # Fallback: use FetchContent for Catch2
        include(FetchContent)
        FetchContent_Declare(Catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2.git
            GIT_TAG v3.7.1
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(Catch2)
        list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    endif()
    
    add_executable(zquake_tests
        tests/main.cpp
        tests/core/fixed_test.cpp
        tests/core/container_test.cpp
        tests/core/memory_test.cpp
        tests/core/vm_test.cpp
        tests/core/progs_vm_test.cpp
        tests/core/console_test.cpp
        tests/core/bsp_test.cpp
        tests/core/move_test.cpp
        tests/core/gameplay_test.cpp
        tests/core/ray_scene_test.cpp
        tests/filesystem/fs_test.cpp
    )
    target_compile_definitions(zquake_tests PRIVATE
        ZQ_TEST_PROGS_PATH="${CMAKE_SOURCE_DIR}/quake/qw-qc/qwprogs.dat")
    target_link_libraries(zquake_tests PRIVATE
        zq_core zq_filesystem zq_engine zq_subsystems zq_vulkan zq_vm zq_render
        Catch2::Catch2WithMain
    )
    add_test(NAME zquake_tests COMMAND zquake_tests)
    
    # Vulkan render smoke test (separate executable)
    add_executable(zquake_smoke
        tests/vulkan_smoke.cpp
    )
    target_link_libraries(zquake_smoke PRIVATE zq_vulkan SDL2::SDL2 zq_core)

    # Orientation / mouse-mapping render test (separate executable)
    add_executable(zquake_orientation
        tests/orientation_test.cpp
        src/app/world_builder.cpp
    )
    target_link_libraries(zquake_orientation PRIVATE
        zq_engine zq_vulkan SDL2::SDL2 zq_core)

    # Diagnostic against the real pak0.pak provided by the user
    add_executable(zquake_realdata
        tests/real_data_test.cpp
    )
    target_link_libraries(zquake_realdata PRIVATE
        zq_engine zq_filesystem zq_core)

    # Inspect real map models/faces
    add_executable(zquake_inspect
        tests/inspect_models.cpp
    )
    target_link_libraries(zquake_inspect PRIVATE
        zq_engine zq_filesystem zq_core)

    # Render the spawn camera view of the real map to a file
    add_executable(zquake_spawnview
        tests/spawn_view.cpp
        src/app/world_builder.cpp
    )
    target_link_libraries(zquake_spawnview PRIVATE
        zq_engine zq_vulkan SDL2::SDL2 zq_core)

    # GPU ray-tracer regression test (manual, like vulkan_smoke)
    add_executable(zquake_raytrace
        tests/raytrace_test.cpp
        src/app/world_builder.cpp
    )
    target_link_libraries(zquake_raytrace PRIVATE
        zq_render zq_engine zq_filesystem zq_vulkan SDL2::SDL2 zq_core)
endif()

# Print build summary
message(STATUS "=== zquake Build Configuration ===")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Platform: ${CMAKE_SYSTEM_NAME}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Audio: ${ZQ_ENABLE_AUDIO}")
message(STATUS "  Network: ${ZQ_ENABLE_NETWORK}")
message(STATUS "  Tests: ${ZQ_ENABLE_TESTS}")
message(STATUS "===================================")
