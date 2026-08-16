#include "pattern_comparator.h"

namespace fpdz {

bool PatternComparator::canBeat(const CardPattern& challenge, const CardPattern& current) {
    if (!challenge.isValid() || !current.isValid()) return false;

    // Any bomb beats any non-bomb
    if (challenge.isBomb() && !current.isBomb()) return true;
    if (!challenge.isBomb() && current.isBomb()) return false;

    // Both bombs: compare by bomb level first, then by main rank
    if (challenge.isBomb() && current.isBomb()) {
        int cLevel = bombLevel(challenge.type);
        int dLevel = bombLevel(current.type);
        if (cLevel != dLevel) return cLevel > dLevel;
        return rankWeight(challenge.mainRank) > rankWeight(current.mainRank);
    }

    // Both non-bombs: must be same type and same length
    if (challenge.type != current.type) return false;
    if (challenge.mainLength != current.mainLength) return false;

    // Compare main rank weight
    return rankWeight(challenge.mainRank) > rankWeight(current.mainRank);
}

int PatternComparator::compare(const CardPattern& a, const CardPattern& b) {
    auto ka = a.key();
    auto kb = b.key();
    if (ka < kb) return -1;
    if (ka > kb) return 1;
    return 0;
}

} // namespace fpdz
