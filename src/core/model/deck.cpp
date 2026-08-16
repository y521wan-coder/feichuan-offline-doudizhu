#include "deck.h"
#include <algorithm>
#include <random>
#include <cassert>
#include <limits>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#endif

namespace fpdz {

namespace {

class StableRandom64 {
public:
    explicit StableRandom64(uint64_t seed) : m_state(seed) {}

    uint64_t next() {
        uint64_t value = (m_state += 0x9E3779B97F4A7C15ULL);
        value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
        value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
        return value ^ (value >> 31);
    }

private:
    uint64_t m_state;
};

template <typename Next>
uint64_t unbiasedBounded(uint64_t bound, Next&& next) {
    assert(bound > 0);
    const uint64_t rejectionLimit = (std::numeric_limits<uint64_t>::max() - bound + 1) % bound;
    for (;;) {
        const uint64_t value = next();
        if (value >= rejectionLimit) return value % bound;
    }
}

bool operatingSystemRandom(uint64_t* value) {
    if (!value) return false;
#ifdef _WIN32
    return BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(value), sizeof(*value),
                           BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
    std::random_device randomDevice;
    *value = static_cast<uint64_t>(randomDevice());
    *value ^= static_cast<uint64_t>(randomDevice()) << 32;
    return true;
#endif
}

} // namespace

std::vector<Card> Deck::createDoubleDeck() {
    std::vector<Card> deck;
    deck.reserve(TOTAL_CARDS);

    for (CardId i = 0; i < TOTAL_CARDS; ++i) {
        Card card = Card::create(i);
        assert(card.isValid());
        deck.push_back(card);
    }

    assert(deck.size() == TOTAL_CARDS);
    return deck;
}

void Deck::shuffle(std::vector<Card>& deck, uint64_t seed) {
    StableRandom64 random(seed);
    for (size_t remaining = deck.size(); remaining > 1; --remaining) {
        const size_t index = static_cast<size_t>(
            unbiasedBounded(static_cast<uint64_t>(remaining), [&]() { return random.next(); }));
        std::swap(deck[remaining - 1], deck[index]);
    }
}

bool Deck::secureShuffle(std::vector<Card>& deck) {
    for (size_t remaining = deck.size(); remaining > 1; --remaining) {
        uint64_t value = 0;
        const uint64_t bound = static_cast<uint64_t>(remaining);
        const uint64_t rejectionLimit = (std::numeric_limits<uint64_t>::max() - bound + 1) % bound;
        do {
            if (!operatingSystemRandom(&value)) return false;
        } while (value < rejectionLimit);
        std::swap(deck[remaining - 1], deck[static_cast<size_t>(value % bound)]);
    }
    return true;
}

uint64_t Deck::deterministicBounded(uint64_t seed, uint64_t bound) {
    if (bound == 0) return 0;
    StableRandom64 random(seed);
    return unbiasedBounded(bound, [&]() { return random.next(); });
}

uint64_t Deck::secureBounded(uint64_t bound) {
    if (bound == 0) return 0;
    uint64_t value = 0;
    const uint64_t rejectionLimit = (std::numeric_limits<uint64_t>::max() - bound + 1) % bound;
    do {
        if (!operatingSystemRandom(&value)) return deterministicBounded(generateRandomSeed(), bound);
    } while (value < rejectionLimit);
    return value % bound;
}

uint64_t Deck::generateRandomSeed() {
    uint64_t seed = 0;
    if (operatingSystemRandom(&seed)) return seed;
    std::random_device randomDevice;
    seed = static_cast<uint64_t>(randomDevice());
    seed ^= static_cast<uint64_t>(randomDevice()) << 32;
    return seed;
}

} // namespace fpdz
