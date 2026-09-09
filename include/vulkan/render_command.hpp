#pragma once
#include <vulkan/vulkan.hpp>
#include <vector>
#include <cstdint>

namespace zq::vk {

enum class CommandType {
    DrawIndexed,
    Draw,
    Clear,
    Present
};

struct DrawIndexedCommand {
    CommandType type = CommandType::DrawIndexed;
    uint32_t index_count;
    uint32_t instance_count;
    uint32_t first_index;
    int32_t vertex_offset;
    uint32_t first_instance;
};

struct RenderCommand {
    CommandType type;
    union {
        DrawIndexedCommand draw;
        uint32_t clear_color[4];
    };
};

}
