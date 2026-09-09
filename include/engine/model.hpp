#pragma once
#include "core/container/string.hpp"
#include "core/fixed/fixed_math.hpp"

namespace zq::engine {

class Model {
public:
    enum Type { mod_brush, mod_sprite, mod_alias, mod_alias2 };
    
    struct Header {
        int ident;
        int version;
        zq::math::Vec3 bounds[2];
        zq::math::Vec3 radius;
        zq::math::Vec3 origin;
        int headnodes[4];
        int visleafs;
        int facecount;
        int surfcount;
    };
    
    static Model* LoadModel(const String& name);
    static void FreeModel(Model* model);
    
private:
    String name_;
    Type type_ = mod_brush;
    bool loaded_ = false;
};

}
