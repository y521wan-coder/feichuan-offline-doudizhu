#pragma once

#include "card.h"
#include <vector>
#include <cstdint>

namespace fpdz {

class Deck {
public:
    // Create a double deck of 108 cards
    static std::vector<Card> createDoubleDeck();

    // Shuffle with a fixed seed for reproducibility
    static void shuffle(std::vector<Card>& deck, uint64_t seed);

    // Shuffle directly from the operating-system entropy source. This is used
    // for real games and deliberately cannot be replayed from a public seed.
    static bool secureShuffle(std::vector<Card>& deck);

    // Deterministic and unbiased bounded draw for tests/training.
    static uint64_t deterministicBounded(uint64_t seed, uint64_t bound);

    // Unbiased bounded draw from the operating-system entropy source.
    static uint64_t secureBounded(uint64_t bound);

    // Generate a random seed
    static uint64_t generateRandomSeed();
};

} // namespace fpdz
