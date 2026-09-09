#include "engine/model.hpp"
#include "core/fixed/fixed_math.hpp"

namespace zq::engine {

Model* Model::LoadModel(const String& name) {
    (void)name;
    return nullptr;
}

void Model::FreeModel(Model* model) {
    if (model) {
        delete model;
    }
}

} // namespace zq::engine
