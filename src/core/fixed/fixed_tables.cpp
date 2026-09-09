#include "core/fixed/fixed_tables.hpp"
#include <cmath>

namespace zq::math {

FixedTables::FixedTables() {
    for (int i = 0; i < 16384; i++) {
        double angle = (2.0 * M_PI * i) / 16384.0;
        sin[i] = static_cast<int32_t>(std::sin(angle) * 65536.0);
        cos[i] = static_cast<int32_t>(std::cos(angle) * 65536.0);
    }
    
    for (int i = 0; i < 16384; i++) {
        inv[i] = static_cast<int32_t>(256.0 / (i + 1));
    }
}

const FixedTables& FixedTables_Get() {
    static const FixedTables tables;
    return tables;
}

} // namespace zq::math
