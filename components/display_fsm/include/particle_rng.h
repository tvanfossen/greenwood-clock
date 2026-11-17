/**
 * @file particle_rng.h
 * @brief Tiny deterministic xorshift32 RNG shared by the particle fields (seedable for tests).
 */
#ifndef PARTICLE_RNG_H
#define PARTICLE_RNG_H

#include <stdint.h>

/**
 * @brief xorshift32 step (state 0 is remapped so the sequence never sticks).
 * @version 1
 * @utility
 * @return Next 32-bit pseudo-random value.
 */
static inline uint32_t prng_next(uint32_t *s)
{
    uint32_t x = *s ? *s : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

/**
 * @brief Uniform float in [0, 1).
 * @version 1
 * @utility
 * @return Uniform random value.
 */
static inline float prng_unit(uint32_t *s)
{
    return (float)(prng_next(s) >> 8) * (1.0f / 16777216.0f);
}

/**
 * @brief Uniform float in [lo, hi).
 * @version 1
 * @utility
 * @return Uniform random value.
 */
static inline float prng_range(uint32_t *s, float lo, float hi)
{
    return lo + (hi - lo) * prng_unit(s);
}

#endif /* PARTICLE_RNG_H */
