#pragma once
#include "core/container/string.hpp"
#include "core/container/array.hpp"

namespace zq::engine {

struct World {
    String name;
    String picture;
    Array<void*> entities;
};

}
