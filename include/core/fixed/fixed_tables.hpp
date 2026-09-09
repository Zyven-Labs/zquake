#include "core/fixed/fixed_t.hpp"

namespace zq::math {

// 16384-entry sin/cos lookup table for full 0-2pi range
// This ensures bit-exact determinism across platforms
struct FixedTables {
    int32_t sin[16384];
    int32_t cos[16384];
    int32_t inv[16384];  // Inverse lookup table for division
    
    FixedTables();
};

// Get a pointer to the singleton tables
const FixedTables& FixedTables_Get();

} // namespace zq::math
