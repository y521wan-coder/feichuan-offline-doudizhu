#pragma once

#include "card_pattern.h"

namespace fpdz {

class PatternComparator {
public:
    // Can 'challenge' beat 'current'?
    // Returns true if challenge can legally beat current
    static bool canBeat(const CardPattern& challenge, const CardPattern& current);

    // Compare two patterns of the same type
    // Returns positive if a > b, negative if a < b, 0 if equal
    static int compare(const CardPattern& a, const CardPattern& b);
};

} // namespace fpdz
