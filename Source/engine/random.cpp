#include "engine/random.hpp"

#include <cstdint>
#include <limits>
#include <random>

namespace devilution {

// Current game seed
uint32_t sglGameSeed;

#if JWK_USE_BETTER_RANDOM_NUMBERS
std::mt19937 diabloGenerator{std::random_device{}()};
std::uniform_int_distribution<uint32_t> diabloDistribution;

void SetRndSeed(uint32_t seed)
{
	diabloGenerator.seed(seed); // performance-wise, it's not a free operation to re-seed the mt19937.  Fine as long as it's not done inside a tight loop.  Currently, the game does a reseed for each active monster per tick in ProcessMonsters() but that's only 200 reseeds per tick.
	sglGameSeed = seed;
}

uint32_t GetLCGEngineState()
{
	return sglGameSeed;
}

uint32_t AdvanceRndSeed()
{
	sglGameSeed = diabloGenerator();
	return sglGameSeed;
}

uint32_t GenerateRnd(uint32_t v)
{
	if (v > 0) {
		return diabloDistribution(diabloGenerator, decltype(diabloDistribution)::param_type{0, v - 1});
	}
	return 0;
}

bool FlipCoin(uint32_t frequency)
{
	return GenerateRnd(frequency) == 0;
}

uint32_t GenerateRndInRange(uint32_t min, uint32_t max)
{
    return diabloDistribution(diabloGenerator, decltype(diabloDistribution)::param_type{min, max});
}

#else // original code

/** Borland C/C++ psuedo-random number generator needed for vanilla compatibility */
std::linear_congruential_engine<uint32_t, 0x015A4E35, 1, 0> diabloGenerator;


void SetRndSeed(uint32_t seed)
{
	diabloGenerator.seed(seed);
	sglGameSeed = seed;
}

uint32_t GetLCGEngineState()
{
	return sglGameSeed;
}

uint32_t GenerateSeed()
{
	sglGameSeed = diabloGenerator();
	return sglGameSeed;
}

int32_t AdvanceRndSeed()
{
	const int32_t seed = static_cast<int32_t>(GenerateSeed());
	// since abs(INT_MIN) is undefined behavior, handle this value specially
	return seed == std::numeric_limits<int32_t>::min() ? std::numeric_limits<int32_t>::min() : abs(seed);
}

int32_t GenerateRnd(int32_t v)
{
	if (v <= 0)
		return 0;
	int32_t seed = AdvanceRndSeed();
#if 1 // jwk - avoid rare negative number bug even though it breaks compatability with vanilla game
	while (seed < 0)
	{
		seed = AdvanceRndSeed();
	}
	if (v <= 0x7FFF) // use the high bits to correct for LCG bias
		return (seed >> 16) % v;
	return seed % v;
#else // original code
	if (v <= 0x7FFF) // use the high bits to correct for LCG bias
		return (AdvanceRndSeed() >> 16) % v;
	return AdvanceRndSeed() % v;
#endif
}

bool FlipCoin(uint32_t frequency)
{
	// Casting here because GenerateRnd takes a signed argument when it should take and yield unsigned.
	return GenerateRnd(static_cast<int32_t>(frequency)) == 0;
}

uint32_t GenerateRndInRange(uint32_t lowest, uint32_t highest)
{
	return lowest + GenerateRnd(highest - lowest + 1);
}

#endif // JWK_USE_BETTER_RANDOM_NUMBERS

void DiscardRandomValues(unsigned count)
{
	while (count != 0) {
		AdvanceRndSeed();
		count--;
	}
}

} // namespace devilution
