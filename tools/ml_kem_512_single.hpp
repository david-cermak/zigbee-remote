#pragma once
#pragma once
#pragma once
#pragma once
#pragma once
#pragma once

/**
 * Following content is taken from
 * https://github.com/itzmeanjan/ml-kem/blob/b43b819e880a6b1e4e165211060ef8fc030b9b6d/include/ml_kem/internals/utility/force_inline.hpp
 */

#ifdef _MSC_VER
// MSVC
#define forceinline __forceinline

#elif defined(__GNUC__)
// GCC
#if defined(__cplusplus) && __cplusplus >= 201103L
#define forceinline inline __attribute__((__always_inline__))
#else
#define forceinline inline
#endif

#elif defined(__CLANG__)
// Clang
#if __has_attribute(__always_inline__)
#define forceinline inline __attribute__((__always_inline__))
#else
#define forceinline inline
#endif

#else
// Others
#define forceinline inline

#endif
#pragma once
#pragma once
#pragma once
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>

// Keccak-p[1600, 12] and Keccak-p[1600, 24] (aka Keccak-f[1600]) permutation
namespace keccak {

// Logarithmic base 2 of bit width of lane i.e. log2(LANE_BW)
static constexpr size_t L = 6;

// Bit width of each lane of Keccak-f[1600] state
static constexpr size_t LANE_BW = 1ul << L;

// Bit length of Keccak-f[1600] permutation state
static constexpr size_t STATE_BIT_LEN = 1600;

// Byte length of Keccak-f[1600] permutation state
static constexpr size_t STATE_BYTE_LEN = STATE_BIT_LEN / std::numeric_limits<uint8_t>::digits;

// # -of lanes (each of 64 -bit width) in Keccak-f[1600] state
static constexpr size_t LANE_CNT = STATE_BIT_LEN / LANE_BW;

// Maximum number of rounds Keccak-p[b, nr] permutation can be applied s.t. b = 1600, w = b/ 25, l = log2(w), nr = 12 + 2l.
static constexpr size_t MAX_NUM_ROUNDS = 12 + 2 * L;

/**
 * Leftwards circular rotation offset of 25 lanes ( each lane is 64 -bit wide ) of state array, as provided in table 2
 * below algorithm 2 in section 3.2.2 of https://dx.doi.org/10.6028/NIST.FIPS.202.
 *
 * Note, following offsets are obtained by performing % 64 (bit width of lane) on offsets provided in above mentioned link.
 */
static constexpr size_t ROT[LANE_CNT]{ 0 % LANE_BW,   1 % LANE_BW,   190 % LANE_BW, 28 % LANE_BW, 91 % LANE_BW, 36 % LANE_BW,  300 % LANE_BW,
                                       6 % LANE_BW,   55 % LANE_BW,  276 % LANE_BW, 3 % LANE_BW,  10 % LANE_BW, 171 % LANE_BW, 153 % LANE_BW,
                                       231 % LANE_BW, 105 % LANE_BW, 45 % LANE_BW,  15 % LANE_BW, 21 % LANE_BW, 136 % LANE_BW, 210 % LANE_BW,
                                       66 % LANE_BW,  253 % LANE_BW, 120 % LANE_BW, 78 % LANE_BW };

/**
 * Precomputed table used for looking up source index during application of `π` step mapping function on Keccak-f[1600] state.
 * print('to <= from')
 * for y in range(5):
 *    for x in range(5):
 *        print(f'{y * 5 + x} <= {x * 5 + (x + 3 * y) % 5}')
 *
 * Table generated using above Python code snippet. See section 3.2.3 of the specification https://dx.doi.org/10.6028/NIST.FIPS.202.
 */
static constexpr size_t PERM[LANE_CNT]{ 0, 6, 12, 18, 24, 3, 9, 10, 16, 22, 1, 7, 13, 19, 20, 4, 5, 11, 17, 23, 2, 8, 14, 15, 21 };

/**
 * Computes single bit of Keccak-f[1600] round constant (at compile-time), using binary LFSR, defined by primitive polynomial `x^8 + x^6 + x^5 + x^4 + 1`.
 *
 * See algorithm 5 in section 3.2.5 of http://dx.doi.org/10.6028/NIST.FIPS.202.
 *
 * Taken from https://github.com/itzmeanjan/elephant/blob/2a21c7e/include/keccak.hpp#L24-L59.
 */
static consteval bool
rc(const size_t t)
{
  // Step 1 of algorithm 5
  if (t % 255 == 0) {
    return 1;
  }

  // Step 2 of algorithm 5
  //
  // Note, step 3.a of algorithm 5 is also being executed in this statement ( for first iteration, with i = 1 ) !
  uint16_t r = 0b10000000;

  // Step 3 of algorithm 5
  for (size_t i = 1; i <= t % 255; i++) {
    const uint16_t b0 = r & 1;

    r = static_cast<uint16_t>((r & 0b011111111) ^ ((((r >> 8) & 1) ^ b0) << 8));
    r = static_cast<uint16_t>((r & 0b111101111) ^ ((((r >> 4) & 1) ^ b0) << 4));
    r = static_cast<uint16_t>((r & 0b111110111) ^ ((((r >> 3) & 1) ^ b0) << 3));
    r = static_cast<uint16_t>((r & 0b111111011) ^ ((((r >> 2) & 1) ^ b0) << 2));

    // Step 3.f of algorithm 5
    //
    // Note, this statement also executes step 3.a for upcoming iterations ( i.e. when i > 1 )
    r >>= 1;
  }

  return static_cast<bool>((r >> 7) & 1);
}

/**
 * Computes 64 -bit round constant (at compile-time), which is XOR-ed into the very first lane ( = lane(0, 0) ) of Keccak-f[1600] permutation state.
 *
 * Taken from https://github.com/itzmeanjan/elephant/blob/2a21c7e/include/keccak.hpp#L61-L74.
 */
static consteval uint64_t
compute_rc(const size_t r_idx)
{
  uint64_t tmp = 0;

  for (size_t j = 0; j < (L + 1); j++) {
    const size_t boff = (1 << j) - 1;
    tmp |= static_cast<uint64_t>(rc(j + 7 * r_idx)) << boff;
  }

  return tmp;
}

// Compile-time evaluate Keccak-f[1600] round constants.
static consteval std::array<uint64_t, MAX_NUM_ROUNDS>
compute_rcs()
{
  std::array<uint64_t, MAX_NUM_ROUNDS> res{};

  for (size_t i = 0; i < MAX_NUM_ROUNDS; i++) {
    res[i] = compute_rc(i);
  }

  return res;
}

// Round constants to be XORed with lane (0, 0) of Keccak-f[1600] permutation state. See section 3.2.5 of https://dx.doi.org/10.s6028/NIST.FIPS.202.
static constexpr std::array<uint64_t, MAX_NUM_ROUNDS> RC = compute_rcs();

/**
 * Keccak-f[1600] round function, applying all five step mapping functions, updating state array.
 * Note this implementation of round function applies four consecutive rounds in a single call i.e. if you invoke it to apply round `i`
 *
 * - It first applies round `i`
 * - Then round `i+1`
 * - And then round `i+2`
 * - And finally round `i+3`
 *
 * See section 3.3 of https://dx.doi.org/10.6028/NIST.FIPS.202.
 * This implementation collects a lot of inspiration from https://github.com/bwesterb/armed-keccak.git.
 */
static forceinline constexpr void
roundx4(uint64_t* const state, const size_t ridx)
{
  std::array<uint64_t, 5> bc{}, d{};
  uint64_t t = 0;

// Round ridx + 0
#if defined __clang__
  // Following https://clang.llvm.org/docs/LanguageExtensions.html#extensions-for-loop-hint-optimizations

#pragma clang loop unroll(enable)
#pragma clang loop vectorize(enable)
#pragma clang loop interleave(enable)
#elif defined __GNUG__
  // Following https://gcc.gnu.org/onlinedocs/gcc/Loop-Specific-Pragmas.html#Loop-Specific-Pragmas

#pragma GCC unroll 5
#pragma GCC ivdep
#endif
  for (size_t i = 0; i < 25; i += 5) {
    bc[0] ^= state[i + 0];
    bc[1] ^= state[i + 1];
    bc[2] ^= state[i + 2];
    bc[3] ^= state[i + 3];
    bc[4] ^= state[i + 4];
  }

  d[0] = bc[4] ^ std::rotl(bc[1], 1);
  d[1] = bc[0] ^ std::rotl(bc[2], 1);
  d[2] = bc[1] ^ std::rotl(bc[3], 1);
  d[3] = bc[2] ^ std::rotl(bc[4], 1);
  d[4] = bc[3] ^ std::rotl(bc[0], 1);

  bc[0] = state[0] ^ d[0];
  t = state[6] ^ d[1];
  bc[1] = std::rotl(t, ROT[6]);
  t = state[12] ^ d[2];
  bc[2] = std::rotl(t, ROT[12]);
  t = state[18] ^ d[3];
  bc[3] = std::rotl(t, ROT[18]);
  t = state[24] ^ d[4];
  bc[4] = std::rotl(t, ROT[24]);

  state[0] = bc[0] ^ (bc[2] & ~bc[1]) ^ RC[ridx];
  state[6] = bc[1] ^ (bc[3] & ~bc[2]);
  state[12] = bc[2] ^ (bc[4] & ~bc[3]);
  state[18] = bc[3] ^ (bc[0] & ~bc[4]);
  state[24] = bc[4] ^ (bc[1] & ~bc[0]);

  t = state[10] ^ d[0];
  bc[2] = std::rotl(t, ROT[10]);
  t = state[16] ^ d[1];
  bc[3] = std::rotl(t, ROT[16]);
  t = state[22] ^ d[2];
  bc[4] = std::rotl(t, ROT[22]);
  t = state[3] ^ d[3];
  bc[0] = std::rotl(t, ROT[3]);
  t = state[9] ^ d[4];
  bc[1] = std::rotl(t, ROT[9]);

  state[10] = bc[0] ^ (bc[2] & ~bc[1]);
  state[16] = bc[1] ^ (bc[3] & ~bc[2]);
  state[22] = bc[2] ^ (bc[4] & ~bc[3]);
  state[3] = bc[3] ^ (bc[0] & ~bc[4]);
  state[9] = bc[4] ^ (bc[1] & ~bc[0]);

  t = state[20] ^ d[0];
  bc[4] = std::rotl(t, ROT[20]);
  t = state[1] ^ d[1];
  bc[0] = std::rotl(t, ROT[1]);
  t = state[7] ^ d[2];
  bc[1] = std::rotl(t, ROT[7]);
  t = state[13] ^ d[3];
  bc[2] = std::rotl(t, ROT[13]);
  t = state[19] ^ d[4];
  bc[3] = std::rotl(t, ROT[19]);

  state[20] = bc[0] ^ (bc[2] & ~bc[1]);
  state[1] = bc[1] ^ (bc[3] & ~bc[2]);
  state[7] = bc[2] ^ (bc[4] & ~bc[3]);
  state[13] = bc[3] ^ (bc[0] & ~bc[4]);
  state[19] = bc[4] ^ (bc[1] & ~bc[0]);

  t = state[5] ^ d[0];
  bc[1] = std::rotl(t, ROT[5]);
  t = state[11] ^ d[1];
  bc[2] = std::rotl(t, ROT[11]);
  t = state[17] ^ d[2];
  bc[3] = std::rotl(t, ROT[17]);
  t = state[23] ^ d[3];
  bc[4] = std::rotl(t, ROT[23]);
  t = state[4] ^ d[4];
  bc[0] = std::rotl(t, ROT[4]);

  state[5] = bc[0] ^ (bc[2] & ~bc[1]);
  state[11] = bc[1] ^ (bc[3] & ~bc[2]);
  state[17] = bc[2] ^ (bc[4] & ~bc[3]);
  state[23] = bc[3] ^ (bc[0] & ~bc[4]);
  state[4] = bc[4] ^ (bc[1] & ~bc[0]);

  t = state[15] ^ d[0];
  bc[3] = std::rotl(t, ROT[15]);
  t = state[21] ^ d[1];
  bc[4] = std::rotl(t, ROT[21]);
  t = state[2] ^ d[2];
  bc[0] = std::rotl(t, ROT[2]);
  t = state[8] ^ d[3];
  bc[1] = std::rotl(t, ROT[8]);
  t = state[14] ^ d[4];
  bc[2] = std::rotl(t, ROT[14]);

  state[15] = bc[0] ^ (bc[2] & ~bc[1]);
  state[21] = bc[1] ^ (bc[3] & ~bc[2]);
  state[2] = bc[2] ^ (bc[4] & ~bc[3]);
  state[8] = bc[3] ^ (bc[0] & ~bc[4]);
  state[14] = bc[4] ^ (bc[1] & ~bc[0]);

  // Round ridx + 1
  std::fill(bc.begin(), bc.end(), 0x00);

#if defined __clang__
#pragma clang loop unroll(enable)
#pragma clang loop vectorize(enable)
#pragma clang loop interleave(enable)
#elif defined __GNUG__
#pragma GCC unroll 5
#pragma GCC ivdep
#endif
  for (size_t i = 0; i < 25; i += 5) {
    bc[0] ^= state[i + 0];
    bc[1] ^= state[i + 1];
    bc[2] ^= state[i + 2];
    bc[3] ^= state[i + 3];
    bc[4] ^= state[i + 4];
  }

  d[0] = bc[4] ^ std::rotl(bc[1], 1);
  d[1] = bc[0] ^ std::rotl(bc[2], 1);
  d[2] = bc[1] ^ std::rotl(bc[3], 1);
  d[3] = bc[2] ^ std::rotl(bc[4], 1);
  d[4] = bc[3] ^ std::rotl(bc[0], 1);

  bc[0] = state[0] ^ d[0];
  t = state[16] ^ d[1];
  bc[1] = std::rotl(t, ROT[6]);
  t = state[7] ^ d[2];
  bc[2] = std::rotl(t, ROT[12]);
  t = state[23] ^ d[3];
  bc[3] = std::rotl(t, ROT[18]);
  t = state[14] ^ d[4];
  bc[4] = std::rotl(t, ROT[24]);

  state[0] = bc[0] ^ (bc[2] & ~bc[1]) ^ RC[ridx + 1];
  state[16] = bc[1] ^ (bc[3] & ~bc[2]);
  state[7] = bc[2] ^ (bc[4] & ~bc[3]);
  state[23] = bc[3] ^ (bc[0] & ~bc[4]);
  state[14] = bc[4] ^ (bc[1] & ~bc[0]);

  t = state[20] ^ d[0];
  bc[2] = std::rotl(t, ROT[10]);
  t = state[11] ^ d[1];
  bc[3] = std::rotl(t, ROT[16]);
  t = state[2] ^ d[2];
  bc[4] = std::rotl(t, ROT[22]);
  t = state[18] ^ d[3];
  bc[0] = std::rotl(t, ROT[3]);
  t = state[9] ^ d[4];
  bc[1] = std::rotl(t, ROT[9]);

  state[20] = bc[0] ^ (bc[2] & ~bc[1]);
  state[11] = bc[1] ^ (bc[3] & ~bc[2]);
  state[2] = bc[2] ^ (bc[4] & ~bc[3]);
  state[18] = bc[3] ^ (bc[0] & ~bc[4]);
  state[9] = bc[4] ^ (bc[1] & ~bc[0]);

  t = state[15] ^ d[0];
  bc[4] = std::rotl(t, ROT[20]);
  t = state[6] ^ d[1];
  bc[0] = std::rotl(t, ROT[1]);
  t = state[22] ^ d[2];
  bc[1] = std::rotl(t, ROT[7]);
  t = state[13] ^ d[3];
  bc[2] = std::rotl(t, ROT[13]);
  t = state[4] ^ d[4];
  bc[3] = std::rotl(t, ROT[19]);

  state[15] = bc[0] ^ (bc[2] & ~bc[1]);
  state[6] = bc[1] ^ (bc[3] & ~bc[2]);
  state[22] = bc[2] ^ (bc[4] & ~bc[3]);
  state[13] = bc[3] ^ (bc[0] & ~bc[4]);
  state[4] = bc[4] ^ (bc[1] & ~bc[0]);

  t = state[10] ^ d[0];
  bc[1] = std::rotl(t, ROT[5]);
  t = state[1] ^ d[1];
  bc[2] = std::rotl(t, ROT[11]);
  t = state[17] ^ d[2];
  bc[3] = std::rotl(t, ROT[17]);
  t = state[8] ^ d[3];
  bc[4] = std::rotl(t, ROT[23]);
  t = state[24] ^ d[4];
  bc[0] = std::rotl(t, ROT[4]);

  state[10] = bc[0] ^ (bc[2] & ~bc[1]);
  state[1] = bc[1] ^ (bc[3] & ~bc[2]);
  state[17] = bc[2] ^ (bc[4] & ~bc[3]);
  state[8] = bc[3] ^ (bc[0] & ~bc[4]);
  state[24] = bc[4] ^ (bc[1] & ~bc[0]);

  t = state[5] ^ d[0];
  bc[3] = std::rotl(t, ROT[15]);
  t = state[21] ^ d[1];
  bc[4] = std::rotl(t, ROT[21]);
  t = state[12] ^ d[2];
  bc[0] = std::rotl(t, ROT[2]);
  t = state[3] ^ d[3];
  bc[1] = std::rotl(t, ROT[8]);
  t = state[19] ^ d[4];
  bc[2] = std::rotl(t, ROT[14]);

  state[5] = bc[0] ^ (bc[2] & ~bc[1]);
  state[21] = bc[1] ^ (bc[3] & ~bc[2]);
  state[12] = bc[2] ^ (bc[4] & ~bc[3]);
  state[3] = bc[3] ^ (bc[0] & ~bc[4]);
  state[19] = bc[4] ^ (bc[1] & ~bc[0]);

  // Round ridx + 2
  std::fill(bc.begin(), bc.end(), 0x00);

#if defined __clang__
#pragma clang loop unroll(enable)
#pragma clang loop vectorize(enable)
#pragma clang loop interleave(enable)
#elif defined __GNUG__
#pragma GCC unroll 5
#pragma GCC ivdep
#endif
  for (size_t i = 0; i < 25; i += 5) {
    bc[0] ^= state[i + 0];
    bc[1] ^= state[i + 1];
    bc[2] ^= state[i + 2];
    bc[3] ^= state[i + 3];
    bc[4] ^= state[i + 4];
  }

  d[0] = bc[4] ^ std::rotl(bc[1], 1);
  d[1] = bc[0] ^ std::rotl(bc[2], 1);
  d[2] = bc[1] ^ std::rotl(bc[3], 1);
  d[3] = bc[2] ^ std::rotl(bc[4], 1);
  d[4] = bc[3] ^ std::rotl(bc[0], 1);

  bc[0] = state[0] ^ d[0];
  t = state[11] ^ d[1];
  bc[1] = std::rotl(t, ROT[6]);
  t = state[22] ^ d[2];
  bc[2] = std::rotl(t, ROT[12]);
  t = state[8] ^ d[3];
  bc[3] = std::rotl(t, ROT[18]);
  t = state[19] ^ d[4];
  bc[4] = std::rotl(t, ROT[24]);

  state[0] = bc[0] ^ (bc[2] & ~bc[1]) ^ RC[ridx + 2];
  state[11] = bc[1] ^ (bc[3] & ~bc[2]);
  state[22] = bc[2] ^ (bc[4] & ~bc[3]);
  state[8] = bc[3] ^ (bc[0] & ~bc[4]);
  state[19] = bc[4] ^ (bc[1] & ~bc[0]);

  t = state[15] ^ d[0];
  bc[2] = std::rotl(t, ROT[10]);
  t = state[1] ^ d[1];
  bc[3] = std::rotl(t, ROT[16]);
  t = state[12] ^ d[2];
  bc[4] = std::rotl(t, ROT[22]);
  t = state[23] ^ d[3];
  bc[0] = std::rotl(t, ROT[3]);
  t = state[9] ^ d[4];
  bc[1] = std::rotl(t, ROT[9]);

  state[15] = bc[0] ^ (bc[2] & ~bc[1]);
  state[1] = bc[1] ^ (bc[3] & ~bc[2]);
  state[12] = bc[2] ^ (bc[4] & ~bc[3]);
  state[23] = bc[3] ^ (bc[0] & ~bc[4]);
  state[9] = bc[4] ^ (bc[1] & ~bc[0]);

  t = state[5] ^ d[0];
  bc[4] = std::rotl(t, ROT[20]);
  t = state[16] ^ d[1];
  bc[0] = std::rotl(t, ROT[1]);
  t = state[2] ^ d[2];
  bc[1] = std::rotl(t, ROT[7]);
  t = state[13] ^ d[3];
  bc[2] = std::rotl(t, ROT[13]);
  t = state[24] ^ d[4];
  bc[3] = std::rotl(t, ROT[19]);

  state[5] = bc[0] ^ (bc[2] & ~bc[1]);
  state[16] = bc[1] ^ (bc[3] & ~bc[2]);
  state[2] = bc[2] ^ (bc[4] & ~bc[3]);
  state[13] = bc[3] ^ (bc[0] & ~bc[4]);
  state[24] = bc[4] ^ (bc[1] & ~bc[0]);

  t = state[20] ^ d[0];
  bc[1] = std::rotl(t, ROT[5]);
  t = state[6] ^ d[1];
  bc[2] = std::rotl(t, ROT[11]);
  t = state[17] ^ d[2];
  bc[3] = std::rotl(t, ROT[17]);
  t = state[3] ^ d[3];
  bc[4] = std::rotl(t, ROT[23]);
  t = state[14] ^ d[4];
  bc[0] = std::rotl(t, ROT[4]);

  state[20] = bc[0] ^ (bc[2] & ~bc[1]);
  state[6] = bc[1] ^ (bc[3] & ~bc[2]);
  state[17] = bc[2] ^ (bc[4] & ~bc[3]);
  state[3] = bc[3] ^ (bc[0] & ~bc[4]);
  state[14] = bc[4] ^ (bc[1] & ~bc[0]);

  t = state[10] ^ d[0];
  bc[3] = std::rotl(t, ROT[15]);
  t = state[21] ^ d[1];
  bc[4] = std::rotl(t, ROT[21]);
  t = state[7] ^ d[2];
  bc[0] = std::rotl(t, ROT[2]);
  t = state[18] ^ d[3];
  bc[1] = std::rotl(t, ROT[8]);
  t = state[4] ^ d[4];
  bc[2] = std::rotl(t, ROT[14]);

  state[10] = bc[0] ^ (bc[2] & ~bc[1]);
  state[21] = bc[1] ^ (bc[3] & ~bc[2]);
  state[7] = bc[2] ^ (bc[4] & ~bc[3]);
  state[18] = bc[3] ^ (bc[0] & ~bc[4]);
  state[4] = bc[4] ^ (bc[1] & ~bc[0]);

  // Round ridx + 3
  std::fill(bc.begin(), bc.end(), 0x00);

#if defined __clang__
#pragma clang loop unroll(enable)
#pragma clang loop vectorize(enable)
#pragma clang loop interleave(enable)
#elif defined __GNUG__
#pragma GCC unroll 5
#pragma GCC ivdep
#endif
  for (size_t i = 0; i < 25; i += 5) {
    bc[0] ^= state[i + 0];
    bc[1] ^= state[i + 1];
    bc[2] ^= state[i + 2];
    bc[3] ^= state[i + 3];
    bc[4] ^= state[i + 4];
  }

  d[0] = bc[4] ^ std::rotl(bc[1], 1);
  d[1] = bc[0] ^ std::rotl(bc[2], 1);
  d[2] = bc[1] ^ std::rotl(bc[3], 1);
  d[3] = bc[2] ^ std::rotl(bc[4], 1);
  d[4] = bc[3] ^ std::rotl(bc[0], 1);

  bc[0] = state[0] ^ d[0];
  t = state[1] ^ d[1];
  bc[1] = std::rotl(t, ROT[6]);
  t = state[2] ^ d[2];
  bc[2] = std::rotl(t, ROT[12]);
  t = state[3] ^ d[3];
  bc[3] = std::rotl(t, ROT[18]);
  t = state[4] ^ d[4];
  bc[4] = std::rotl(t, ROT[24]);

  state[0] = bc[0] ^ (bc[2] & ~bc[1]) ^ RC[ridx + 3];
  state[1] = bc[1] ^ (bc[3] & ~bc[2]);
  state[2] = bc[2] ^ (bc[4] & ~bc[3]);
  state[3] = bc[3] ^ (bc[0] & ~bc[4]);
  state[4] = bc[4] ^ (bc[1] & ~bc[0]);

  t = state[5] ^ d[0];
  bc[2] = std::rotl(t, ROT[10]);
  t = state[6] ^ d[1];
  bc[3] = std::rotl(t, ROT[16]);
  t = state[7] ^ d[2];
  bc[4] = std::rotl(t, ROT[22]);
  t = state[8] ^ d[3];
  bc[0] = std::rotl(t, ROT[3]);
  t = state[9] ^ d[4];
  bc[1] = std::rotl(t, ROT[9]);

  state[5] = bc[0] ^ (bc[2] & ~bc[1]);
  state[6] = bc[1] ^ (bc[3] & ~bc[2]);
  state[7] = bc[2] ^ (bc[4] & ~bc[3]);
  state[8] = bc[3] ^ (bc[0] & ~bc[4]);
  state[9] = bc[4] ^ (bc[1] & ~bc[0]);

  t = state[10] ^ d[0];
  bc[4] = std::rotl(t, ROT[20]);
  t = state[11] ^ d[1];
  bc[0] = std::rotl(t, ROT[1]);
  t = state[12] ^ d[2];
  bc[1] = std::rotl(t, ROT[7]);
  t = state[13] ^ d[3];
  bc[2] = std::rotl(t, ROT[13]);
  t = state[14] ^ d[4];
  bc[3] = std::rotl(t, ROT[19]);

  state[10] = bc[0] ^ (bc[2] & ~bc[1]);
  state[11] = bc[1] ^ (bc[3] & ~bc[2]);
  state[12] = bc[2] ^ (bc[4] & ~bc[3]);
  state[13] = bc[3] ^ (bc[0] & ~bc[4]);
  state[14] = bc[4] ^ (bc[1] & ~bc[0]);

  t = state[15] ^ d[0];
  bc[1] = std::rotl(t, ROT[5]);
  t = state[16] ^ d[1];
  bc[2] = std::rotl(t, ROT[11]);
  t = state[17] ^ d[2];
  bc[3] = std::rotl(t, ROT[17]);
  t = state[18] ^ d[3];
  bc[4] = std::rotl(t, ROT[23]);
  t = state[19] ^ d[4];
  bc[0] = std::rotl(t, ROT[4]);

  state[15] = bc[0] ^ (bc[2] & ~bc[1]);
  state[16] = bc[1] ^ (bc[3] & ~bc[2]);
  state[17] = bc[2] ^ (bc[4] & ~bc[3]);
  state[18] = bc[3] ^ (bc[0] & ~bc[4]);
  state[19] = bc[4] ^ (bc[1] & ~bc[0]);

  t = state[20] ^ d[0];
  bc[3] = std::rotl(t, ROT[15]);
  t = state[21] ^ d[1];
  bc[4] = std::rotl(t, ROT[21]);
  t = state[22] ^ d[2];
  bc[0] = std::rotl(t, ROT[2]);
  t = state[23] ^ d[3];
  bc[1] = std::rotl(t, ROT[8]);
  t = state[24] ^ d[4];
  bc[2] = std::rotl(t, ROT[14]);

  state[20] = bc[0] ^ (bc[2] & ~bc[1]);
  state[21] = bc[1] ^ (bc[3] & ~bc[2]);
  state[22] = bc[2] ^ (bc[4] & ~bc[3]);
  state[23] = bc[3] ^ (bc[0] & ~bc[4]);
  state[24] = bc[4] ^ (bc[1] & ~bc[0]);
}

/**
 * Keccak-f[1600] permutation, applying either 12 or 24 rounds (as requested by template argument) of permutation on state of dimension 5 x 5 x 64 ( = 1600 )
 * -bits, using algorithm 7 defined in section 3.3 of SHA3 specification https://dx.doi.org/10.6028/NIST.FIPS.202.
 */
template<size_t num_rounds>
forceinline constexpr void
permute(uint64_t state[LANE_CNT])
  requires((num_rounds == 12) || (num_rounds == MAX_NUM_ROUNDS))
{
  constexpr size_t start_at_round = MAX_NUM_ROUNDS - num_rounds;
  constexpr size_t STEP_BY = 4;

  static_assert(num_rounds % STEP_BY == 0, "Requested number of keccak-p[1600] rounds need to be a multiple of 4 for manual unrolling to work.");

  for (size_t i = start_at_round; i < MAX_NUM_ROUNDS; i += STEP_BY) {
    roundx4(state, i);
  }
}

}
#pragma once
#include <cassert>
#include <cstdint>
#include <span>

// Commonly used functions for SHA3 implementation
namespace sha3_utils {

// Given a byte array of length 8, this routine can be used for interpreting those 8 -bytes in little-endian order, as a 64 -bit unsigned integer.
static forceinline constexpr uint64_t
le_bytes_to_u64(std::span<const uint8_t, sizeof(uint64_t)> bytes)
{
  static_assert(std::endian::native == std::endian::little);

  return (static_cast<uint64_t>(bytes[7]) << 56u) | (static_cast<uint64_t>(bytes[6]) << 48u) | (static_cast<uint64_t>(bytes[5]) << 40u) |
         (static_cast<uint64_t>(bytes[4]) << 32u) | (static_cast<uint64_t>(bytes[3]) << 24u) | (static_cast<uint64_t>(bytes[2]) << 16u) |
         (static_cast<uint64_t>(bytes[1]) << 8u) | (static_cast<uint64_t>(bytes[0]) << 0u);
}

// Given a 64 -bit unsigned integer as input, this routine can be used for interpreting those 8 -bytes in little-endian byte order.
static forceinline constexpr void
u64_to_le_bytes(uint64_t word, std::span<uint8_t, sizeof(word)> bytes)
{
  static_assert(std::endian::native == std::endian::little);

  bytes[0] = static_cast<uint8_t>(word >> 0u);
  bytes[1] = static_cast<uint8_t>(word >> 8u);
  bytes[2] = static_cast<uint8_t>(word >> 16u);
  bytes[3] = static_cast<uint8_t>(word >> 24u);
  bytes[4] = static_cast<uint8_t>(word >> 32u);
  bytes[5] = static_cast<uint8_t>(word >> 40u);
  bytes[6] = static_cast<uint8_t>(word >> 48u);
  bytes[7] = static_cast<uint8_t>(word >> 56u);
}

}
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>

// Keccak family of sponge functions
namespace sponge {

static constexpr size_t KECCAK_WORD_BYTE_LEN = keccak::LANE_BW / std::numeric_limits<uint8_t>::digits;

// Compile-time check domain separator bit length.
constexpr bool
check_domain_separator(const size_t dom_sep_bit_len)
{
  return dom_sep_bit_len <= 6u;
}

/**
 * Given `mlen` (>=0) -bytes message, this routine consumes it into Keccak[c] permutation state s.t. `offset` ( second parameter ) denotes how many bytes are
 * already consumed into rate portion of the state.
 *
 * - `num_bits_in_rate` portion of sponge will have bitwidth of 1600 - c.
 * - `offset` must ∈ [0, `num_bytes_in_rate`).
 *
 * This function implementation collects inspiration from https://github.com/itzmeanjan/turboshake/blob/e1a6b950/src/sponge.rs#L4-L56.
 */
template<size_t num_bits_in_rate, size_t num_rounds>
static forceinline constexpr void
absorb(uint64_t state[keccak::LANE_CNT], size_t& offset, std::span<const uint8_t> msg)
{
  constexpr size_t num_bytes_in_rate = num_bits_in_rate / std::numeric_limits<uint8_t>::digits;

  std::array<uint8_t, num_bytes_in_rate> block{};
  auto block_span = std::span(block);

  size_t msg_offset = 0;
  while (msg_offset < msg.size()) {
    const size_t remaining_num_bytes = msg.size() - msg_offset;
    const size_t absorbable_num_bytes = std::min(remaining_num_bytes, num_bytes_in_rate - offset);
    const size_t effective_block_byte_len = offset + absorbable_num_bytes;
    const size_t padded_effective_block_byte_len = (effective_block_byte_len + (KECCAK_WORD_BYTE_LEN - 1)) & (-KECCAK_WORD_BYTE_LEN);
    const size_t padded_effective_block_begins_at = offset & (-KECCAK_WORD_BYTE_LEN);

    std::fill_n(block_span.subspan(padded_effective_block_begins_at).begin(), padded_effective_block_byte_len - padded_effective_block_begins_at, 0x00);
    std::copy_n(msg.subspan(msg_offset).begin(), absorbable_num_bytes, block_span.subspan(offset).begin());

    size_t state_word_index = padded_effective_block_begins_at / KECCAK_WORD_BYTE_LEN;
    for (size_t i = padded_effective_block_begins_at; i < padded_effective_block_byte_len; i += KECCAK_WORD_BYTE_LEN) {
      auto msg_chunk = std::span<const uint8_t, KECCAK_WORD_BYTE_LEN>(block_span.subspan(i, KECCAK_WORD_BYTE_LEN));
      auto msg_word = sha3_utils::le_bytes_to_u64(msg_chunk);

      state[state_word_index] ^= msg_word;
      state_word_index++;
    }

    offset += absorbable_num_bytes;
    msg_offset += absorbable_num_bytes;

    if (offset == num_bytes_in_rate) [[unlikely]] {
      keccak::permute<num_rounds>(state);
      offset = 0;
    }
  }
}

/**
 * Given that N message bytes are already consumed into Keccak[c] permutation state, this routine finalizes sponge state and makes it ready for squeezing, by
 * appending (along with domain separation bits) `10*1` padding bits to input message s.t. total absorbed message byte length becomes multiple of `rate/ 8`
 * -bytes.
 *
 * - `num_bits_in_rate` portion of sponge will have bitwidth of 1600 - c.
 * - `offset` must ∈ [0, `num_bytes_in_rate`)
 *
 * This function implementation collects some motivation from https://github.com/itzmeanjan/turboshake/blob/e1a6b950/src/sponge.rs#L58-L81.
 */
template<uint8_t domain_separator, size_t ds_bit_len, size_t num_bits_in_rate, size_t num_rounds>
static forceinline constexpr void
finalize(uint64_t state[keccak::LANE_CNT], size_t& offset)
  requires(check_domain_separator(ds_bit_len))
{
  constexpr size_t num_bytes_in_rate = num_bits_in_rate / std::numeric_limits<uint8_t>::digits;
  constexpr size_t num_words_in_rate = num_bytes_in_rate / std::numeric_limits<uint8_t>::digits;

  const auto state_word_index = offset / KECCAK_WORD_BYTE_LEN;
  const auto byte_index_in_state_word = offset % KECCAK_WORD_BYTE_LEN;
  const auto shl_bit_offset = byte_index_in_state_word * std::numeric_limits<uint8_t>::digits;

  constexpr uint8_t mask = (1u << ds_bit_len) - 1u;
  constexpr uint8_t pad_byte = (1u << ds_bit_len) | (domain_separator & mask);

  state[state_word_index] ^= static_cast<uint64_t>(pad_byte) << shl_bit_offset;
  state[num_words_in_rate - 1] ^= UINT64_C(0x80) << 56;

  keccak::permute<num_rounds>(state);
  offset = 0;
}

/**
 * Given that Keccak[c] permutation state is finalized, this routine can be invoked for squeezing `olen` -bytes out of rate portion of the state.
 *
 * - `num_bits_in_rate` portion of sponge will have bitwidth of 1600 - c.
 * - `squeezable` denotes how many bytes can be squeezed without permutating the sponge state.
 * - When `squeezable` becomes 0, state needs to be permutated again, after which `num_bytes_in_rate` can again be squeezed from rate portion of the state.
 *
 * This function implementation collects motivation from https://github.com/itzmeanjan/turboshake/blob/e1a6b950/src/sponge.rs#L83-L118.
 */
template<size_t num_bits_in_rate, size_t num_rounds>
static forceinline constexpr void
squeeze(uint64_t state[keccak::LANE_CNT], size_t& squeezable, std::span<uint8_t> out)
{
  constexpr size_t num_bytes_in_rate = num_bits_in_rate / std::numeric_limits<uint8_t>::digits;

  std::array<uint8_t, num_bytes_in_rate> blk_bytes{};
  auto blk_bytes_span = std::span(blk_bytes);

  size_t out_offset = 0;
  while (out_offset < out.size()) {
    const size_t state_byte_offset = num_bytes_in_rate - squeezable;
    const size_t remaining_num_bytes = out.size() - out_offset;
    const size_t squeezable_num_bytes = std::min(remaining_num_bytes, squeezable);
    const size_t effective_block_byte_len = state_byte_offset + squeezable_num_bytes;
    const size_t padded_effective_block_byte_len = (effective_block_byte_len + (KECCAK_WORD_BYTE_LEN - 1)) & (-KECCAK_WORD_BYTE_LEN);
    const size_t padded_effective_block_begins_at = state_byte_offset & (-KECCAK_WORD_BYTE_LEN);

    size_t state_word_index = padded_effective_block_begins_at / KECCAK_WORD_BYTE_LEN;
    for (size_t i = padded_effective_block_begins_at; i < padded_effective_block_byte_len; i += KECCAK_WORD_BYTE_LEN) {
      auto chunk_bytes = std::span<uint8_t, KECCAK_WORD_BYTE_LEN>(blk_bytes_span.subspan(i, KECCAK_WORD_BYTE_LEN));
      sha3_utils::u64_to_le_bytes(state[state_word_index], chunk_bytes);

      state_word_index++;
    }

    std::copy_n(blk_bytes_span.subspan(state_byte_offset).begin(), squeezable_num_bytes, out.subspan(out_offset).begin());

    squeezable -= squeezable_num_bytes;
    out_offset += squeezable_num_bytes;

    if (squeezable == 0) [[unlikely]] {
      keccak::permute<num_rounds>(state);
      squeezable = num_bytes_in_rate;
    }
  }
}

}
#include <bit>
#include <cstdint>
#include <limits>
#include <sys/types.h>

// SHAKE256 Extendable Output Function : Keccak[512](M || 1111, d)
namespace shake256 {

// Number of rounds keccak-p[1600] is applied.
static constexpr size_t NUM_KECCAK_ROUNDS = keccak::MAX_NUM_ROUNDS;

// SHAKE256 XOF offers at max 256-bits of security.
static constexpr size_t TARGET_BIT_SECURITY_LEVEL = 256;

// Width of capacity portion of the sponge, in bits.
static constexpr size_t CAPACITY = 2 * TARGET_BIT_SECURITY_LEVEL;

// Width of rate portion of the sponge, in bits.
static constexpr size_t RATE = 1600 - CAPACITY;

// Domain separator bits, used for finalization.
static constexpr uint8_t DOM_SEP = 0b00001111;

// Bit-width of domain separator, starting from least significant bit.
static constexpr size_t DOM_SEP_BW = std::bit_width(DOM_SEP);

/**
 * SHAKE256 eXtendable Output Function (XOF).
 *
 * See SHA3 extendable output function definition in section 6.2 of SHA3 specification https://dx.doi.org/10.6028/NIST.FIPS.202.
 */
struct shake256_t
{
private:
  uint64_t state[keccak::LANE_CNT]{};
  size_t offset = 0;
  alignas(4) bool finalized = false; // all message bytes absorbed ?
  size_t squeezable = 0;

public:
  forceinline constexpr shake256_t() = default;
  forceinline constexpr size_t squeezable_num_bytes() const { return squeezable; }

  /**
   * Given N -many bytes input message, this routine consumes those into keccak[512] sponge state.
   *
   * Note, this routine can be called arbitrary number of times, each time with arbitrary bytes of input message, until
   * keccak[512] state is finalized ( by calling routine with the same name ). Once the sponge is finalized, it can't
   * absorb any more message bytes.
   */
  forceinline constexpr void absorb(std::span<const uint8_t> msg)
  {
    if (!finalized) {
      sponge::absorb<RATE, NUM_KECCAK_ROUNDS>(state, offset, msg);
    }
  }

  /**
   * After consuming arbitrary many input bytes, this routine is invoked when no more input bytes remaining to be
   * consumed by keccak[512] state.
   *
   * Note, once this routine is called, calling absorb() or finalize() again, on same SHAKE256 object, doesn't do
   * anything. After finalization, one might intend to read arbitrary many bytes by squeezing sponge, which is done by
   * calling read() function, as many times required.
   */
  forceinline constexpr void finalize()
  {
    if (!finalized) {
      sponge::finalize<DOM_SEP, DOM_SEP_BW, RATE, NUM_KECCAK_ROUNDS>(state, offset);

      finalized = true;
      squeezable = RATE / std::numeric_limits<uint8_t>::digits;
    }
  }

  /**
   * After sponge state is finalized, arbitrary many output bytes can be squeezed by calling this function
   * any number of times required.
   */
  forceinline constexpr void squeeze(std::span<uint8_t> dig)
  {
    if (finalized) {
      sponge::squeeze<RATE, NUM_KECCAK_ROUNDS>(state, squeezable, dig);
    }
  }

  /**
   * Reset the internal state of the SHAKE256 XOF hasher.
   * Now it can again be used for another `absorb() -> finalize() -> squeeze()` cycle.
   */
  forceinline constexpr void reset()
  {
    std::fill(std::begin(state), std::end(state), 0);
    offset = 0;
    finalized = false;
    squeezable = 0;
  }

  /**
   * Given that sponge is already finalized, this routine can be used for zeroizing first n -bytes of
   * permutation state s.t. n <= 200 and applying permutation.
   */
  forceinline void ratchet(const size_t byte_len)
  {
    if (finalized) {
      const auto ratchetable_portion_byte_len = std::min(byte_len, keccak::STATE_BYTE_LEN);

      auto state_as_bytes = reinterpret_cast<uint8_t*>(state);
      std::memset(state_as_bytes, 0, ratchetable_portion_byte_len);

      keccak::permute<NUM_KECCAK_ROUNDS>(state);
    }
  }
};

}
#pragma once
#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>

// TurboSHAKE256 eXtendable Output Function
namespace turboshake256 {

// 12 -rounds Keccak-p[1600, 12] is applied.
static constexpr size_t NUM_KECCAK_ROUNDS = keccak::MAX_NUM_ROUNDS / 2;

// TurboSHAKE256 XOF offers at max 256-bits of security.
static constexpr size_t TARGET_BIT_SECURITY_LEVEL = 256;

// Width of capacity portion of the sponge, in bits.
static constexpr size_t CAPACITY = 2 * TARGET_BIT_SECURITY_LEVEL;

// Width of rate portion of the sponge, in bits.
static constexpr size_t RATE = 1600 - CAPACITY;

/**
 * TurboSHAKE256 eXtendable Output Function (XOF).
 *
 * See TurboSHAKE extendable output function definition in section 2 of RFC 9861 https://datatracker.ietf.org/doc/rfc9861.
 */
struct turboshake256_t
{
private:
  uint64_t state[keccak::LANE_CNT]{};
  size_t offset = 0;
  alignas(4) bool finalized = false; // all message bytes absorbed ?
  size_t squeezable = 0;

public:
  forceinline constexpr turboshake256_t() = default;
  forceinline constexpr size_t squeezable_num_bytes() const { return squeezable; }

  /**
   * Given N -many bytes input message, this routine consumes those into keccak[256] sponge state.
   *
   * Note, this routine can be called arbitrary number of times, each time with arbitrary bytes of input message, until
   * keccak[256] state is finalized ( by calling routine with similar name ). Once the sponge is finalized, it can't
   * absorb any more message bytes.
   */
  forceinline constexpr void absorb(std::span<const uint8_t> msg)
  {
    if (!finalized) {
      sponge::absorb<RATE, NUM_KECCAK_ROUNDS>(state, offset, msg);
    }
  }

  /**
   * After consuming arbitrary many input bytes, this routine is invoked when no more input bytes remaining to be consumed by keccak[256] state.
   * This function expects a domain separator byte ∈ [0x01, 0x7f]. If not supplied, it will take 0x1f as default value.
   *
   * Note, once this routine is called, calling absorb() or finalize() again, on same TurboSHAKE256 object, doesn't do
   * anything. After finalization, one might intend to read arbitrary many bytes by squeezing sponge, which is done by
   * calling squeeze() function, as many times required.
   */
  template<uint8_t dom_sep = 0x1f>
  forceinline constexpr void finalize()
    requires((dom_sep >= 0x01) && (dom_sep <= 0x7f))
  {
    if (!finalized) {
      sponge::finalize<dom_sep, std::bit_width(dom_sep) - 1, RATE, NUM_KECCAK_ROUNDS>(state, offset);

      finalized = true;
      squeezable = RATE / std::numeric_limits<uint8_t>::digits;
    }
  }

  /**
   * After sponge state is finalized, arbitrary many output bytes can be squeezed by calling this function
   * any number of times required.
   */
  forceinline constexpr void squeeze(std::span<uint8_t> dig)
  {
    if (finalized) {
      sponge::squeeze<RATE, NUM_KECCAK_ROUNDS>(state, squeezable, dig);
    }
  }

  /**
   * Reset the internal state of the TurboSHAKE256 XOF hasher.
   * Now it can again be used for another `absorb() -> finalize() -> squeeze()` cycle.
   */
  forceinline constexpr void reset()
  {
    std::fill(std::begin(state), std::end(state), 0);
    offset = 0;
    finalized = false;
    squeezable = 0;
  }

  /**
   * Given that sponge is already finalized, this routine can be used for zeroizing first n -bytes of
   * permutation state s.t. n <= 200 and applying permutation.
   */
  forceinline void ratchet(const size_t byte_len)
  {
    if (finalized) {
      const auto ratchetable_portion_byte_len = std::min(byte_len, keccak::STATE_BYTE_LEN);

      auto state_as_bytes = reinterpret_cast<uint8_t*>(state);
      std::memset(state_as_bytes, 0, ratchetable_portion_byte_len);

      keccak::permute<NUM_KECCAK_ROUNDS>(state);
    }
  }
};

}
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <random>
#include <type_traits>

namespace randomshake {

// Compile-time check to ensure that endianness is little, as correctness of program depends on it.
consteval auto
check_endianness()
{
  return std::endian::native == std::endian::little;
}

/**
 * Ensures that value is materialized (and not optimized away), but doesn't clobber memory, like google-benchmark does.
 * Taken from https://theunixzoo.co.uk/blog/2021-10-14-preventing-optimisations.html.
 */
template<typename Tp>
forceinline void
DoNotOptimize(Tp& value)
{
  asm volatile("" : "+r,m"(value) : :);
}

// Enum listing supported eXtendable Output Functions (XOFs), which can be used for producing pseudo-random byte stream.
enum class xof_kind_t : uint8_t
{
  SHAKE256,      // Based on 24-rounds keccak permutation.
  TURBOSHAKE256, // Based on 12-rounds keccak permutation. Almost doubles the throughput. The default choice.
};

// A helper trait, for selecting which XOF to use in RandomSHAKE CSPRNG.
template<xof_kind_t xof_kind>
struct xof_selector_t
{};

// Specialization for SHAKE256 XOF.
template<>
struct xof_selector_t<xof_kind_t::SHAKE256>
{
  using type = shake256::shake256_t;

  // Bit width of the rate portion of keccak sponge for SHAKE256 XOF.
  static constexpr size_t rate = shake256::RATE;

  // Required seed byte length to initialize the SHAKE256 XOF.
  static constexpr size_t seed_byte_len = rate / std::numeric_limits<uint8_t>::digits;

  /**
   * Everytime these many bytes are squeezed from the underlying keccak sponge,
   * we zeroize first `ratchet_byte_len`-bytes of Keccak permutation state and re-apply 24-rounds permutation.
   *
   * Ratchet period gets computed as `8 x RATE-of-the-underlying-keccak-sponge` bits.
   */
  static constexpr size_t ratchet_period_byte_len = shake256::RATE;

  // First these many bytes of keccak permutation are zeroized during ratcheting.
  static constexpr size_t ratchet_byte_len = shake256::TARGET_BIT_SECURITY_LEVEL / std::numeric_limits<uint8_t>::digits;
};

// Specialization for TurboSHAKE256 XOF.
template<>
struct xof_selector_t<xof_kind_t::TURBOSHAKE256>
{
  using type = turboshake256::turboshake256_t;

  // Bit width of the rate portion of keccak sponge for TurboSHAKE256 XOF.
  static constexpr size_t rate = turboshake256::RATE;

  // Required seed byte length to initialize the TurboSHAKE256 XOF.
  static constexpr size_t seed_byte_len = rate / std::numeric_limits<uint8_t>::digits;

  /**
   * Everytime these many bytes are squeezed from the underlying keccak sponge,
   * we zeroize first `ratchet_byte_len`-bytes of Keccak permutation state and re-apply 12-rounds permutation.
   *
   * Ratchet period gets computed as `8 x RATE-of-the-underlying-keccak-sponge` bits.
   */
  static constexpr size_t ratchet_period_byte_len = turboshake256::RATE;

  // First these many bytes of keccak permutation state are zeroized during ratcheting.
  static constexpr size_t ratchet_byte_len = turboshake256::TARGET_BIT_SECURITY_LEVEL / std::numeric_limits<uint8_t>::digits;
};

/**
 * RandomSHAKE - TurboSHAKE256 (by default) or SHAKE256-backed Cryptographically Secure Pseudo-Random Number Generator (CSPRNG).
 *
 * Allowing both (a) `std::random_device` sampled seed, (b) User provided seed-based initialization of CSPRNG.
 * After every `ratchet_period_byte_len`-many bytes are squeezed from the underlying XOF instance, we perform
 * ratcheting i.e. zeroing out of first `ratchet_byte_len` -many bytes of Keccak permutation state and re-applying
 * permutation.
 *
 * Design of RandomSHAKE CSPRNG API collects inspiration from https://seth.rocks/articles/cpprandom.
 */
template<typename UIntType = uint8_t, xof_kind_t xof_kind = xof_kind_t::TURBOSHAKE256>
  requires(std::is_unsigned_v<UIntType> && check_endianness())
struct randomshake_t
{
private:
  xof_selector_t<xof_kind>::type state{};
  std::array<uint8_t, xof_selector_t<xof_kind>::ratchet_period_byte_len> buffer{};
  size_t buffer_offset = 0u;

public:
  using result_type = UIntType;

  static constexpr auto seed_byte_len = xof_selector_t<xof_kind>::seed_byte_len;
  static constexpr auto min = std::numeric_limits<result_type>::min;
  static constexpr auto max = std::numeric_limits<result_type>::max;

  /**
   * Samples `seed_byte_len` -many bytes from std::random_device and initializes the chosen XOF - making it ready for use.
   * Before you use this constructor, I strongly advise you to read https://en.cppreference.com/w/cpp/numeric/random/random_device.
   */
  forceinline randomshake_t()
  {
    std::array<uint8_t, seed_byte_len> seed{};
    auto seed_span = std::span(seed);

    std::random_device rd{};
    const auto entropy = rd.entropy();
    if (entropy == 0.) {
      std::cout << "[RANDOMSHAKE WARNING] Non-deterministic seed generator has zero entropy ! "
                   "Read https://en.cppreference.com/w/cpp/numeric/random/random_device/entropy for more insight.\n";
    }

    constexpr size_t step_by = sizeof(std::random_device::result_type);
    size_t seed_offset = 0;
    while (seed_offset < seed_span.size()) {
      const auto v = rd();

      static_assert(seed_byte_len % step_by == 0, "Seed byte length must be a multiple of `step_by`, for following memcpy to work correctly !");
      std::memcpy(seed_span.subspan(seed_offset, step_by).data(), reinterpret_cast<const uint8_t*>(&v), step_by);

      seed_offset += step_by;
    }

    state.reset();
    state.absorb(seed_span);
    state.finalize();
    state.squeeze(buffer);
  }

  /**
   * Explicit constructor. Expects user to supply us with `seed_byte_len` -bytes seed, which is used for initializing
   * the chosen XOF. It is user's responsibility to ensure that the supplied seed has sufficient entropy.
   */
  forceinline explicit constexpr randomshake_t(std::span<const uint8_t, seed_byte_len> seed)
  {
    state.reset();
    state.absorb(seed);
    state.finalize();
    state.squeeze(buffer);
  }

  // Delete copy and move constructors - as this CSPRNG instance is neither copyable nor movable.
  randomshake_t(const randomshake_t&) = delete;
  randomshake_t(randomshake_t&&) = delete;
  randomshake_t& operator=(const randomshake_t&) = delete;
  randomshake_t& operator=(randomshake_t&&) = delete;

  // Zeroize internal state when destroying an instance of CSPRNG.
  ~randomshake_t()
  {
    state.reset();
    DoNotOptimize(state);

    buffer.fill(0);
    DoNotOptimize(buffer);

    buffer_offset = 0;
  }

  // Squeezes a random value of type `result_type`.
  [[nodiscard("Internal state of CSPRNG has changed, you should consume this value")]] forceinline result_type operator()()
  {
    constexpr size_t required_num_bytes = sizeof(result_type);
    const size_t readble_num_bytes = buffer.size() - buffer_offset;

    static_assert(xof_selector_t<xof_kind>::ratchet_period_byte_len % required_num_bytes == 0,
                  "Buffer size nust be a multiple of `required_num_bytes`, for following ratchet()->squeeze() to work correctly !");

    // When the buffer is exhausted, it's time to ratchet and fill the buffer with new ready-to-use random bytes.
    if (readble_num_bytes == 0) {
      state.ratchet(xof_selector_t<xof_kind>::ratchet_byte_len);
      state.squeeze(buffer);
      buffer_offset = 0;
    }

    result_type result{};

    auto src_ptr = reinterpret_cast<const uint8_t*>(buffer.data()) + buffer_offset;
    auto dst_ptr = reinterpret_cast<uint8_t*>(&result);

    std::memcpy(dst_ptr, src_ptr, required_num_bytes);
    buffer_offset += required_num_bytes;

    return result;
  }

  // Squeezes n(>=0) random bytes, instead of getting one at a time, as done by the above functor.
  forceinline void generate(std::span<uint8_t> output)
  {
    size_t out_offset = 0;

    while (out_offset < output.size()) {
      const size_t readable_num_bytes = buffer.size() - buffer_offset;
      const size_t required_num_bytes = output.size() - out_offset;
      const size_t copyable_num_bytes = std::min(readable_num_bytes, required_num_bytes);

      auto src_ptr = reinterpret_cast<const uint8_t*>(buffer.data()) + buffer_offset;
      auto dst_ptr = reinterpret_cast<uint8_t*>(output.data()) + out_offset;

      std::memcpy(dst_ptr, src_ptr, copyable_num_bytes);

      buffer_offset += copyable_num_bytes;
      out_offset += copyable_num_bytes;

      if (buffer_offset == buffer.size()) {
        state.ratchet(xof_selector_t<xof_kind>::ratchet_byte_len);
        state.squeeze(buffer);
        buffer_offset = 0;
      }
    }
  }
};

}
#include <bit>
#include <limits>
#include <cstdint>

namespace ml_kem_field {

// Ml_kem Prime Field Modulus ( = 3329 )
inline constexpr uint32_t Q = (1u << 8) * 13 + 1;

// Bit width of Ml_kem Prime Field Modulus ( = 12 )
inline constexpr size_t Q_BIT_WIDTH = std::bit_width(Q);

// Precomputed Barrett Reduction Constant
//
// Note,
//
// k = ceil(log2(Q)) = 12,
// r = floor((1 << 2k) / Q) = 5039
//
// See https://www.nayuki.io/page/barrett-reduction-algorithm.
inline constexpr uint32_t R = (1u << (2 * Q_BIT_WIDTH)) / Q;

// Prime field Zq | q = 3329, with arithmetic operations defined over it.
//
// Collects inspiration from https://github.com/itzmeanjan/dilithium/blob/3fe6ab61/include/field.hpp.
struct zq_t
{
private:
  // Underlying value held in this type.
  //
  // Note, v is always kept in its canonical form i.e. v ∈ [0, Q).
  uint32_t v = 0u;

  // Given a 32 -bit unsigned integer `v` such that `v` ∈ [0, 2*Q), this routine can be invoked for reducing `v` modulo prime Q.
  static forceinline constexpr uint32_t reduce_once(const uint32_t v)
  {
    const uint32_t t0 = v - Q;
    const uint32_t t1 = -(t0 >> 31);
    const uint32_t t2 = Q & t1;
    const uint32_t t3 = t0 + t2;

    return t3;
  }

  // Given a 32 -bit unsigned integer `v` such that `v` ∈ [0, Q*Q), this routine can be invoked for reducing `v` modulo Q, using
  // barrett reduction technique, following algorithm description @ https://www.nayuki.io/page/barrett-reduction-algorithm.
  static forceinline constexpr uint32_t barrett_reduce(const uint32_t v)
  {
    const uint64_t t0 = static_cast<uint64_t>(v) * static_cast<uint64_t>(R);
    const uint32_t t1 = static_cast<uint32_t>(t0 >> (2 * Q_BIT_WIDTH));
    const uint32_t t2 = t1 * Q;
    const uint32_t t = v - t2;

    return reduce_once(t);
  }

public:
  // Constructor(s)
  forceinline constexpr zq_t() = default;
  forceinline constexpr zq_t(const uint16_t a /* Expects a ∈ [0, Q) */) { this->v = a; }
  static forceinline constexpr zq_t from_non_reduced(const uint16_t a /* Doesn't expect that a ∈ [0, Q) */) { return barrett_reduce(a); }

  // Returns canonical value held under Zq type. Returned value must ∈ [0, Q).
  forceinline constexpr uint32_t raw() const { return this->v; }

  static forceinline constexpr zq_t zero() { return zq_t(0u); }
  static forceinline constexpr zq_t one() { return zq_t(1u); }

  // Modulo addition of two Zq elements.
  forceinline constexpr zq_t operator+(const zq_t& rhs) const { return reduce_once(this->v + rhs.v); }
  forceinline constexpr void operator+=(const zq_t& rhs) { *this = *this + rhs; }

  // Modulo negation of a Zq element.
  forceinline constexpr zq_t operator-() const { return zq_t(Q - this->v); }

  // Modulo subtraction of one Zq element from another one.
  forceinline constexpr zq_t operator-(const zq_t& rhs) const { return *this + (-rhs); }
  forceinline constexpr void operator-=(const zq_t& rhs) { *this = *this - rhs; }

  // Modulo multiplication of two Zq elements.
  forceinline constexpr zq_t operator*(const zq_t& rhs) const { return barrett_reduce(this->v * rhs.v); }
  forceinline constexpr void operator*=(const zq_t& rhs) { *this = *this * rhs; }

  // Modulo exponentiation of Zq element.
  //
  // Taken from https://github.com/itzmeanjan/dilithium/blob/3fe6ab61/include/field.hpp#L144-L167.
  forceinline constexpr zq_t operator^(const size_t n) const
  {
    zq_t base = *this;

    const zq_t br[]{ zq_t(1), base };
    zq_t res = br[n & 0b1ul];

    const size_t zeros = std::countl_zero(n);
    const size_t till = std::numeric_limits<size_t>::digits - zeros;

    for (size_t i = 1; i < till; i++) {
      base = base * base;

      const zq_t br[]{ zq_t(1), base };
      res = res * br[(n >> i) & 0b1ul];
    }

    return res;
  }

  // Multiplicative inverse of Zq element. Also division of one Zq element by another one.
  //
  // Note, if Zq element is 0, we can't compute multiplicative inverse and 0 is returned.
  forceinline constexpr zq_t inv() const { return *this ^ static_cast<size_t>((Q - 2)); }
  forceinline constexpr zq_t operator/(const zq_t& rhs) const { return *this * rhs.inv(); }

  // Comparison operators, see https://en.cppreference.com/w/cpp/language/default_comparisons
  forceinline constexpr auto operator<=>(const zq_t&) const = default;

  // Samples a random Zq element, using pseudo random number generator.
  static forceinline zq_t random(randomshake::randomshake_t<>& csprng)
  {
    uint16_t res = 0;
    csprng.generate(std::span(reinterpret_cast<uint8_t*>(&res), sizeof(res)));

    return zq_t::from_non_reduced(static_cast<uint32_t>(res));
  }
};

}
#pragma once
#pragma once
#pragma once
#pragma once

// Following content is taken from https://github.com/itzmeanjan/raccoon/blob/bfa45f9f22ea7b98f5d6588a8513ff4182af79ca/include/raccoon/internals/utility/force_inline.hpp

#ifdef _MSC_VER
// MSVC
#define forceinline __forceinline

#elif defined(__GNUC__)
// GCC
#if defined(__cplusplus) && __cplusplus >= 201103L
#define forceinline inline __attribute__((__always_inline__))
#else
#define forceinline inline
#endif

#elif defined(__CLANG__)
// Clang
#if __has_attribute(__always_inline__)
#define forceinline inline __attribute__((__always_inline__))
#else
#define forceinline inline
#endif

#else
// Others
#define forceinline inline

#endif

namespace ml_kem_ntt {

inline constexpr size_t LOG2N = 8;
inline constexpr size_t N = 1 << LOG2N;

// First primitive 256 -th root of unity modulo q | q = 3329
//
// Meaning, 17 ** 256 == 1 mod q
inline constexpr auto ζ = ml_kem_field::zq_t(17);
static_assert((ζ ^ N) == ml_kem_field::zq_t::one(), "ζ must be 256th root of unity modulo Q");

// Multiplicative inverse of N/ 2 over Z_q | q = 3329 and N = 256
//
// Meaning (N/ 2) * INV_N = 1 mod q
inline constexpr auto INV_N = ml_kem_field::zq_t(N / 2).inv();

// Given a 64 -bit unsigned integer, this routine extracts specified many contiguous bits from ( least significant bits ) LSB side
// and reverses their bit order, returning bit reversed `mbw` -bit wide number.
//
// See https://github.com/itzmeanjan/falcon/blob/45b0593/include/ntt.hpp#L30-L38 for source of inspiration.
template<size_t mbw>
forceinline constexpr size_t
bit_rev(const size_t v)
{
  size_t v_rev = 0ul;

  for (size_t i = 0; i < mbw; i++) {
    const size_t bit = (v >> i) & 0b1;
    v_rev ^= bit << (mbw - 1ul - i);
  }

  return v_rev;
}

// Compile-time computed constants ( powers of ζ ), used for polynomial evaluation i.e. computation of NTT form.
inline constexpr std::array<ml_kem_field::zq_t, N / 2> NTT_ζ_EXP = []() -> auto {
  std::array<ml_kem_field::zq_t, N / 2> res{};

  for (size_t i = 0; i < res.size(); i++) {
    res[i] = ζ ^ bit_rev<LOG2N - 1>(i);
  }

  return res;
}();

// Compile-time computed constants ( negated powers of ζ ), used for polynomial interpolation i.e. computation of iNTT form.
inline constexpr std::array<ml_kem_field::zq_t, N / 2> INTT_ζ_EXP = []() -> auto {
  std::array<ml_kem_field::zq_t, N / 2> res{};

  for (size_t i = 0; i < res.size(); i++) {
    res[i] = -NTT_ζ_EXP[i];
  }

  return res;
}();

// Compile-time computed constants ( powers of ζ ), used when multiplying two degree-255 polynomials in NTT domain.
inline constexpr std::array<ml_kem_field::zq_t, N / 2> POLY_MUL_ζ_EXP = []() -> auto {
  std::array<ml_kem_field::zq_t, N / 2> res{};

  for (size_t i = 0; i < res.size(); i++) {
    res[i] = ζ ^ ((bit_rev<LOG2N - 1>(i) << 1) ^ 1);
  }

  return res;
}();

// Given a polynomial f with 256 coefficients over F_q | q = 3329, this routine computes number theoretic transform
// using Cooley-Tukey algorithm, producing polynomial f' s.t. its coefficients are placed in bit-reversed order.
//
// Note, this routine mutates input i.e. it's an in-place NTT implementation.
//
// Implementation inspired from https://github.com/itzmeanjan/falcon/blob/45b0593/include/ntt.hpp#L69-L144.
// See algorithm 9 of ML-KEM specification https://doi.org/10.6028/NIST.FIPS.203.
forceinline constexpr void
ntt(std::span<ml_kem_field::zq_t, N> poly)
{
  for (size_t l = LOG2N - 1; l >= 1; l--) {
    const size_t len = 1ul << l;
    const size_t lenx2 = len << 1;
    const size_t k_beg = N >> (l + 1);

    for (size_t start = 0; start < poly.size(); start += lenx2) {
      const size_t k_now = k_beg + (start >> (l + 1));
      // Looking up precomputed constant, though it can be computed using
      //
      // ζ ^ bit_rev<LOG2N - 1>(k_now)
      //
      // This is how these constants are generated !
      const ml_kem_field::zq_t ζ_exp = NTT_ζ_EXP[k_now];

      for (size_t i = start; i < start + len; i++) {
        auto tmp = ζ_exp;
        tmp *= poly[i + len];

        poly[i + len] = poly[i] - tmp;
        poly[i] += tmp;
      }
    }
  }
}

// Given a polynomial f with 256 coefficients over F_q | q = 3329, s.t. its coefficients are placed in bit-reversed order,
// this routine computes inverse number theoretic transform using Gentleman-Sande algorithm, producing polynomial f' s.t.
// its coefficients are placed in standard order.
//
// Note, this routine mutates input i.e. it's an in-place iNTT implementation.
//
// Implementation inspired from https://github.com/itzmeanjan/falcon/blob/45b0593/include/ntt.hpp#L146-L224.
// See algorithm 10 of ML-KEM specification https://doi.org/10.6028/NIST.FIPS.203.
forceinline constexpr void
intt(std::span<ml_kem_field::zq_t, N> poly)
{
  for (size_t l = 1; l < LOG2N; l++) {
    const size_t len = 1ul << l;
    const size_t lenx2 = len << 1;
    const size_t k_beg = (N >> l) - 1;

    for (size_t start = 0; start < poly.size(); start += lenx2) {
      const size_t k_now = k_beg - (start >> (l + 1));
      // Looking up precomputed constant, though it can be computed using
      //
      // -(ζ ^ bit_rev<LOG2N - 1>(k_now))
      //
      // Or simpler
      //
      // -NTT_ζ_EXP[k_now]
      const ml_kem_field::zq_t neg_ζ_exp = INTT_ζ_EXP[k_now];

      for (size_t i = start; i < start + len; i++) {
        const auto tmp = poly[i];

        poly[i] += poly[i + len];
        poly[i + len] = tmp - poly[i + len];
        poly[i + len] *= neg_ζ_exp;
      }
    }
  }

  for (size_t i = 0; i < poly.size(); i++) {
    poly[i] *= INV_N;
  }
}

// Given two degree-1 polynomials, this routine computes resulting degree-1 polynomial h.
// See algorithm 12 of ML-KEM specification https://doi.org/10.6028/NIST.FIPS.203.
forceinline constexpr void
basemul(std::span<const ml_kem_field::zq_t, 2> f, std::span<const ml_kem_field::zq_t, 2> g, std::span<ml_kem_field::zq_t, 2> h, const ml_kem_field::zq_t ζ)
{
  ml_kem_field::zq_t f0 = f[0];
  ml_kem_field::zq_t f1 = f[1];

  f0 *= g[0];
  f1 *= g[1];
  f1 *= ζ;
  f1 += f0;

  h[0] = f1;

  ml_kem_field::zq_t g0 = g[0];
  ml_kem_field::zq_t g1 = g[1];

  g1 *= f[0];
  g0 *= f[1];
  g1 += g0;

  h[1] = g1;
}

// Given two degree-255 polynomials in NTT form, this routine performs 128
// base case multiplications for 128 pairs of degree-1 polynomials s.t.
//
// f = (f0ˆ + f1ˆX, f2ˆ + f3ˆX, ..., f254ˆ + f255ˆX)
// g = (g0ˆ + g1ˆX, g2ˆ + g3ˆX, ..., g254ˆ + g255ˆX)
//
// h = f ◦ g
//
// See algorithm 11 of ML-KEM specification https://doi.org/10.6028/NIST.FIPS.203.
constexpr void
polymul(std::span<const ml_kem_field::zq_t, N> f, std::span<const ml_kem_field::zq_t, N> g, std::span<ml_kem_field::zq_t, N> h)
{
  using poly_t = std::span<const ml_kem_field::zq_t, 2>;
  using mut_poly_t = std::span<ml_kem_field::zq_t, 2>;

  for (size_t i = 0; i < f.size() / 2; i++) {
    const size_t off = i * 2;
    basemul(poly_t(f.subspan(off, 2)), poly_t(g.subspan(off, 2)), mut_poly_t(h.subspan(off, 2)), POLY_MUL_ζ_EXP[i]);
  }
}

}
#pragma once
#include <cstddef>

// Holds compile-time executable functions, ensuring that functions are invoked with proper arguments.
namespace ml_kem_params {

// Compile-time check to ensure that number of bits ( read `d` ) to consider during
// polynomial coefficient compression/ decompression is within tolerable bounds.
//
// See "Compression and Decompression" section on page 21 of ML-KEM specification https://doi.org/10.6028/NIST.FIPS.203.
consteval bool
check_d(const size_t d)
{
  return d < ml_kem_field::Q_BIT_WIDTH;
}

// Compile-time check to ensure that functions requiring `η` as parameter are invoked with proper argument.
consteval bool
check_eta(const size_t eta)
{
  return (eta == 2) || (eta == 3);
}

// Compile-time check to ensure that functions requiring `k` as parameter are invoked with proper argument.
consteval bool
check_k(const size_t k)
{
  return (k == 2) || (k == 3) || (k == 4);
}

// Compile-time check to ensure that polynomial to byte array encoding ( and decoding ) routines are invoked with proper params.
consteval bool
check_l(const size_t l)
{
  return (l == 1) || (l == 4) || (l == 5) || (l == 10) || (l == 11) || (l == 12);
}

// Compile-time check to ensure that operand matrices are having compatible dimension for matrix multiplication.
consteval bool
check_matrix_dim(const size_t a_cols, const size_t b_rows)
{
  return !static_cast<bool>(a_cols ^ b_rows);
}

// Compile-time check to ensure that both K-PKE, ML-KEM key generation routine is invoked with proper parameter set.
//
// See table 2 of ML-KEM specification https://doi.org/10.6028/NIST.FIPS.203.
consteval bool
check_keygen_params(const size_t k, const size_t eta1)
{
  bool flg0 = (k == 2) && (eta1 == 3);
  bool flg1 = (k == 3) && (eta1 == 2);
  bool flg2 = (k == 4) && (eta1 == 2);

  return flg0 || flg1 || flg2;
}

// Compile-time check to ensure that K-PKE encryption routine is invoked with proper parameter set.
//
// See table 2 of ML-KEM specification https://doi.org/10.6028/NIST.FIPS.203.
consteval bool
check_encrypt_params(const size_t k, const size_t η1, const size_t η2, const size_t du, const size_t dv)
{
  bool flg0 = (k == 2) && (η1 == 3) && (η2 == 2) && (du == 10) && (dv == 4);
  bool flg1 = (k == 3) && (η1 == 2) && (η2 == 2) && (du == 10) && (dv == 4);
  bool flg2 = (k == 4) && (η1 == 2) && (η2 == 2) && (du == 11) && (dv == 5);

  return flg0 || flg1 || flg2;
}

// Compile-time check to ensure that K-PKE decryption routine is invoked with proper parameter set.
//
// See table 2 of ML-KEM specification https://doi.org/10.6028/NIST.FIPS.203.
consteval bool
check_decrypt_params(const size_t k, const size_t du, const size_t dv)
{
  bool flg0 = (k == 2) && (du == 10) && (dv == 4);
  bool flg1 = (k == 3) && (du == 10) && (dv == 4);
  bool flg2 = (k == 4) && (du == 11) && (dv == 5);

  return flg0 || flg1 || flg2;
}

// Compile-time check to ensure that ML-KEM encapsulation routine is invoked with proper parameter set.
//
// See table 2 of ML-KEM specification https://doi.org/10.6028/NIST.FIPS.203.
consteval bool
check_encap_params(const size_t k, const size_t η1, const size_t η2, const size_t du, const size_t dv)
{
  return check_encrypt_params(k, η1, η2, du, dv);
}

// Compile-time check to ensure that ML-KEM encapsulation routine is invoked with proper parameter set.
//
// See table 2 of ML-KEM specification https://doi.org/10.6028/NIST.FIPS.203.
consteval bool
check_decap_params(const size_t k, const size_t η1, const size_t η2, const size_t du, const size_t dv)
{
  return check_encap_params(k, η1, η2, du, dv);
}

}
#include <span>

namespace ml_kem_utils {

// Given an element x ∈ Z_q | q = 3329, this routine compresses it by discarding some low-order bits, computing y ∈ [0, 2^d) | d < round(log2(q)).
//
// See formula 4.7 on page 21 of ML-KEM specification https://doi.org/10.6028/NIST.FIPS.203.
// Following implementation collects inspiration from https://github.com/FiloSottile/mlkem768/blob/cffbfb96/mlkem768.go#L395-L425.
template<size_t d>
forceinline constexpr ml_kem_field::zq_t
compress(const ml_kem_field::zq_t x)
  requires(ml_kem_params::check_d(d))
{
  constexpr uint16_t mask = (1u << d) - 1;

  const auto dividend = x.raw() << d;
  const auto quotient0 = static_cast<uint32_t>((static_cast<uint64_t>(dividend) * ml_kem_field::R) >> (ml_kem_field::Q_BIT_WIDTH * 2));
  const auto remainder = dividend - quotient0 * ml_kem_field::Q;

  const auto quotient1 = quotient0 + ((((ml_kem_field::Q / 2) - remainder) >> 31) & 1);
  const auto quotient2 = quotient1 + (((ml_kem_field::Q + (ml_kem_field::Q / 2) - remainder) >> 31) & 1);

  return ml_kem_field::zq_t(static_cast<uint16_t>(quotient2) & mask);
}

// Given an element x ∈ [0, 2^d) | d < round(log2(q)), this routine decompresses it back to y ∈ Z_q | q = 3329.
//
// See formula 4.8 on page 21 of ML-KEM specification https://doi.org/10.6028/NIST.FIPS.203.
template<size_t d>
forceinline constexpr ml_kem_field::zq_t
decompress(const ml_kem_field::zq_t x)
  requires(ml_kem_params::check_d(d))
{
  constexpr uint32_t t0 = 1u << d;
  constexpr uint32_t t1 = t0 >> 1;

  const uint32_t t2 = ml_kem_field::Q * x.raw();
  const uint32_t t3 = t2 + t1;
  const uint16_t t4 = static_cast<uint16_t>(t3 >> d);

  return ml_kem_field::zq_t(t4);
}

// Utility function to compress each of 256 coefficients of a degree-255 polynomial while mutating the input.
template<size_t d>
constexpr void
poly_compress(std::span<ml_kem_field::zq_t, ml_kem_ntt::N> poly)
  requires(ml_kem_params::check_d(d))
{
  for (size_t i = 0; i < poly.size(); i++) {
    poly[i] = compress<d>(poly[i]);
  }
}

// Utility function to decompress each of 256 coefficients of a degree-255 polynomial while mutating the input.
template<size_t d>
constexpr void
poly_decompress(std::span<ml_kem_field::zq_t, ml_kem_ntt::N> poly)
  requires(ml_kem_params::check_d(d))
{
  for (size_t i = 0; i < poly.size(); i++) {
    poly[i] = decompress<d>(poly[i]);
  }
}

}
#pragma once

namespace ml_kem_utils {

// Given a degree-255 polynomial, where significant portion of each ( total 256 of them ) coefficient ∈ [0, 2^l),
// this routine serializes the polynomial to a byte array of length 32 * l -bytes.
//
// See algorithm 5 of ML-KEM specification https://doi.org/10.6028/NIST.FIPS.203.
template<size_t l>
constexpr void
encode(std::span<const ml_kem_field::zq_t, ml_kem_ntt::N> poly, std::span<uint8_t, 32 * l> arr)
  requires(ml_kem_params::check_l(l))
{
  std::fill(arr.begin(), arr.end(), 0);

  if constexpr (l == 1) {
    constexpr size_t itr_cnt = ml_kem_ntt::N >> 3;
    constexpr uint32_t one = 0b1u;

    for (size_t i = 0; i < itr_cnt; i++) {
      const size_t off = i << 3;
      arr[i] = (static_cast<uint8_t>(poly[off + 7].raw() & one) << 7) | (static_cast<uint8_t>(poly[off + 6].raw() & one) << 6) |
               (static_cast<uint8_t>(poly[off + 5].raw() & one) << 5) | (static_cast<uint8_t>(poly[off + 4].raw() & one) << 4) |
               (static_cast<uint8_t>(poly[off + 3].raw() & one) << 3) | (static_cast<uint8_t>(poly[off + 2].raw() & one) << 2) |
               (static_cast<uint8_t>(poly[off + 1].raw() & one) << 1) | (static_cast<uint8_t>(poly[off + 0].raw() & one) << 0);
    }
  } else if constexpr (l == 4) {
    constexpr size_t itr_cnt = ml_kem_ntt::N >> 1;
    constexpr uint32_t msk = 0b1111u;

    for (size_t i = 0; i < itr_cnt; i++) {
      const size_t off = i << 1;
      arr[i] = (static_cast<uint8_t>(poly[off + 1].raw() & msk) << 4) | static_cast<uint8_t>(poly[off + 0].raw() & msk);
    }
  } else if constexpr (l == 5) {
    constexpr size_t itr_cnt = ml_kem_ntt::N >> 3;
    constexpr uint32_t mask5 = 0b11111u;
    constexpr uint32_t mask4 = 0b1111u;
    constexpr uint32_t mask3 = 0b111u;
    constexpr uint32_t mask2 = 0b11u;
    constexpr uint32_t mask1 = 0b1u;

    for (size_t i = 0; i < itr_cnt; i++) {
      const size_t poff = i << 3;
      const size_t boff = i * 5;

      const auto t0 = poly[poff + 0].raw();
      const auto t1 = poly[poff + 1].raw();
      const auto t2 = poly[poff + 2].raw();
      const auto t3 = poly[poff + 3].raw();
      const auto t4 = poly[poff + 4].raw();
      const auto t5 = poly[poff + 5].raw();
      const auto t6 = poly[poff + 6].raw();
      const auto t7 = poly[poff + 7].raw();

      arr[boff + 0] = (static_cast<uint8_t>(t1 & mask3) << 5) | (static_cast<uint8_t>(t0 & mask5) << 0);
      arr[boff + 1] = (static_cast<uint8_t>(t3 & mask1) << 7) | (static_cast<uint8_t>(t2 & mask5) << 2) | static_cast<uint8_t>((t1 >> 3) & mask2);
      arr[boff + 2] = (static_cast<uint8_t>(t4 & mask4) << 4) | static_cast<uint8_t>((t3 >> 1) & mask4);
      arr[boff + 3] = (static_cast<uint8_t>(t6 & mask2) << 6) | (static_cast<uint8_t>(t5 & mask5) << 1) | static_cast<uint8_t>((t4 >> 4) & mask1);
      arr[boff + 4] = (static_cast<uint8_t>(t7 & mask5) << 3) | static_cast<uint8_t>((t6 >> 2) & mask3);
    }
  } else if constexpr (l == 10) {
    constexpr size_t itr_cnt = ml_kem_ntt::N >> 2;
    constexpr uint32_t mask6 = 0b111111u;
    constexpr uint32_t mask4 = 0b1111u;
    constexpr uint32_t mask2 = 0b11u;

    for (size_t i = 0; i < itr_cnt; i++) {
      const size_t poff = i << 2;
      const size_t boff = i * 5;

      const auto t0 = poly[poff + 0].raw();
      const auto t1 = poly[poff + 1].raw();
      const auto t2 = poly[poff + 2].raw();
      const auto t3 = poly[poff + 3].raw();

      arr[boff + 0] = static_cast<uint8_t>(t0);
      arr[boff + 1] = static_cast<uint8_t>((t1 & mask6) << 2) | static_cast<uint8_t>((t0 >> 8) & mask2);
      arr[boff + 2] = static_cast<uint8_t>((t2 & mask4) << 4) | static_cast<uint8_t>((t1 >> 6) & mask4);
      arr[boff + 3] = static_cast<uint8_t>((t3 & mask2) << 6) | static_cast<uint8_t>((t2 >> 4) & mask6);
      arr[boff + 4] = static_cast<uint8_t>(t3 >> 2);
    }
  } else if constexpr (l == 11) {
    constexpr size_t itr_cnt = ml_kem_ntt::N >> 3;
    constexpr uint32_t mask8 = 0b11111111u;
    constexpr uint32_t mask7 = 0b1111111u;
    constexpr uint32_t mask6 = 0b111111u;
    constexpr uint32_t mask5 = 0b11111u;
    constexpr uint32_t mask4 = 0b1111u;
    constexpr uint32_t mask3 = 0b111u;
    constexpr uint32_t mask2 = 0b11u;
    constexpr uint32_t mask1 = 0b1u;

    for (size_t i = 0; i < itr_cnt; i++) {
      const size_t poff = i << 3;
      const size_t boff = i * 11;

      const auto t0 = poly[poff + 0].raw();
      const auto t1 = poly[poff + 1].raw();
      const auto t2 = poly[poff + 2].raw();
      const auto t3 = poly[poff + 3].raw();
      const auto t4 = poly[poff + 4].raw();
      const auto t5 = poly[poff + 5].raw();
      const auto t6 = poly[poff + 6].raw();
      const auto t7 = poly[poff + 7].raw();

      arr[boff + 0] = static_cast<uint8_t>(t0 & mask8);
      arr[boff + 1] = static_cast<uint8_t>((t1 & mask5) << 3) | static_cast<uint8_t>((t0 >> 8) & mask3);
      arr[boff + 2] = static_cast<uint8_t>((t2 & mask2) << 6) | static_cast<uint8_t>((t1 >> 5) & mask6);
      arr[boff + 3] = static_cast<uint8_t>((t2 >> 2) & mask8);
      arr[boff + 4] = static_cast<uint8_t>((t3 & mask7) << 1) | static_cast<uint8_t>((t2 >> 10) & mask1);
      arr[boff + 5] = static_cast<uint8_t>((t4 & mask4) << 4) | static_cast<uint8_t>((t3 >> 7) & mask4);
      arr[boff + 6] = static_cast<uint8_t>((t5 & mask1) << 7) | static_cast<uint8_t>((t4 >> 4) & mask7);
      arr[boff + 7] = static_cast<uint8_t>((t5 >> 1) & mask8);
      arr[boff + 8] = static_cast<uint8_t>((t6 & mask6) << 2) | static_cast<uint8_t>((t5 >> 9) & mask2);
      arr[boff + 9] = static_cast<uint8_t>((t7 & mask3) << 5) | static_cast<uint8_t>((t6 >> 6) & mask5);
      arr[boff + 10] = static_cast<uint8_t>((t7 >> 3) & mask8);
    }
  } else {
    static_assert(l == 12, "l must be equal to 12 !");

    constexpr size_t itr_cnt = ml_kem_ntt::N >> 1;
    constexpr uint32_t mask4 = 0b1111u;

    for (size_t i = 0; i < itr_cnt; i++) {
      const size_t poff = i << 1;
      const size_t boff = i * 3;

      const auto t0 = poly[poff + 0].raw();
      const auto t1 = poly[poff + 1].raw();

      arr[boff + 0] = static_cast<uint8_t>(t0);
      arr[boff + 1] = static_cast<uint8_t>((t1 & mask4) << 4) | static_cast<uint8_t>((t0 >> 8) & mask4);
      arr[boff + 2] = static_cast<uint8_t>(t1 >> 4);
    }
  }
}

// Given a byte array of length 32 * l -bytes this routine deserializes it to a polynomial of degree 255 s.t. significant
// portion of each ( total 256 of them ) coefficient ∈ [0, 2^l).
//
// See algorithm 6 of ML-KEM specification https://doi.org/10.6028/NIST.FIPS.203.
template<size_t l>
constexpr void
decode(std::span<const uint8_t, 32 * l> arr, std::span<ml_kem_field::zq_t, ml_kem_ntt::N> poly)
  requires(ml_kem_params::check_l(l))
{
  if constexpr (l == 1) {
    constexpr size_t itr_cnt = ml_kem_ntt::N >> 3;
    constexpr uint8_t one = 0b1;

    for (size_t i = 0; i < itr_cnt; i++) {
      const size_t off = i << 3;
      const uint8_t byte = arr[i];

      poly[off + 0] = ml_kem_field::zq_t((byte >> 0) & one);
      poly[off + 1] = ml_kem_field::zq_t((byte >> 1) & one);
      poly[off + 2] = ml_kem_field::zq_t((byte >> 2) & one);
      poly[off + 3] = ml_kem_field::zq_t((byte >> 3) & one);
      poly[off + 4] = ml_kem_field::zq_t((byte >> 4) & one);
      poly[off + 5] = ml_kem_field::zq_t((byte >> 5) & one);
      poly[off + 6] = ml_kem_field::zq_t((byte >> 6) & one);
      poly[off + 7] = ml_kem_field::zq_t((byte >> 7) & one);
    }
  } else if constexpr (l == 4) {
    constexpr size_t itr_cnt = ml_kem_ntt::N >> 1;
    constexpr uint8_t mask = 0b1111;

    for (size_t i = 0; i < itr_cnt; i++) {
      const size_t off = i << 1;
      const uint8_t byte = arr[i];

      poly[off + 0] = ml_kem_field::zq_t((byte >> 0) & mask);
      poly[off + 1] = ml_kem_field::zq_t((byte >> 4) & mask);
    }
  } else if constexpr (l == 5) {
    constexpr size_t itr_cnt = ml_kem_ntt::N >> 3;
    constexpr uint8_t mask5 = 0b11111;
    constexpr uint8_t mask4 = 0b1111;
    constexpr uint8_t mask3 = 0b111;
    constexpr uint8_t mask2 = 0b11;
    constexpr uint8_t mask1 = 0b1;

    for (size_t i = 0; i < itr_cnt; i++) {
      const size_t poff = i << 3;
      const size_t boff = i * 5;

      const auto t0 = static_cast<uint16_t>(arr[boff + 0] & mask5);
      const auto t1 = static_cast<uint16_t>((arr[boff + 1] & mask2) << 3) | static_cast<uint16_t>((arr[boff + 0] >> 5) & mask3);
      const auto t2 = static_cast<uint16_t>((arr[boff + 1] >> 2) & mask5);
      const auto t3 = static_cast<uint16_t>((arr[boff + 2] & mask4) << 1) | static_cast<uint16_t>((arr[boff + 1] >> 7) & mask1);
      const auto t4 = static_cast<uint16_t>((arr[boff + 3] & mask1) << 4) | static_cast<uint16_t>((arr[boff + 2] >> 4) & mask4);
      const auto t5 = static_cast<uint16_t>((arr[boff + 3] >> 1) & mask5);
      const auto t6 = static_cast<uint16_t>((arr[boff + 4] & mask3) << 2) | static_cast<uint16_t>((arr[boff + 3] >> 6) & mask2);
      const auto t7 = static_cast<uint16_t>((arr[boff + 4] >> 3) & mask5);

      poly[poff + 0] = ml_kem_field::zq_t(t0);
      poly[poff + 1] = ml_kem_field::zq_t(t1);
      poly[poff + 2] = ml_kem_field::zq_t(t2);
      poly[poff + 3] = ml_kem_field::zq_t(t3);
      poly[poff + 4] = ml_kem_field::zq_t(t4);
      poly[poff + 5] = ml_kem_field::zq_t(t5);
      poly[poff + 6] = ml_kem_field::zq_t(t6);
      poly[poff + 7] = ml_kem_field::zq_t(t7);
    }
  } else if constexpr (l == 10) {
    constexpr size_t itr_cnt = ml_kem_ntt::N >> 2;
    constexpr uint8_t mask6 = 0b111111;
    constexpr uint8_t mask4 = 0b1111;
    constexpr uint8_t mask2 = 0b11;

    for (size_t i = 0; i < itr_cnt; i++) {
      const size_t poff = i << 2;
      const size_t boff = i * 5;

      const auto t0 = (static_cast<uint16_t>(arr[boff + 1] & mask2) << 8) | static_cast<uint16_t>(arr[boff + 0]);
      const auto t1 = (static_cast<uint16_t>(arr[boff + 2] & mask4) << 6) | static_cast<uint16_t>(arr[boff + 1] >> 2);
      const auto t2 = (static_cast<uint16_t>(arr[boff + 3] & mask6) << 4) | static_cast<uint16_t>(arr[boff + 2] >> 4);
      const auto t3 = (static_cast<uint16_t>(arr[boff + 4]) << 2) | static_cast<uint16_t>(arr[boff + 3] >> 6);

      poly[poff + 0] = ml_kem_field::zq_t(t0);
      poly[poff + 1] = ml_kem_field::zq_t(t1);
      poly[poff + 2] = ml_kem_field::zq_t(t2);
      poly[poff + 3] = ml_kem_field::zq_t(t3);
    }
  } else if constexpr (l == 11) {
    constexpr size_t itr_cnt = ml_kem_ntt::N >> 3;
    constexpr uint8_t mask7 = 0b1111111;
    constexpr uint8_t mask6 = 0b111111;
    constexpr uint8_t mask5 = 0b11111;
    constexpr uint8_t mask4 = 0b1111;
    constexpr uint8_t mask3 = 0b111;
    constexpr uint8_t mask2 = 0b11;
    constexpr uint8_t mask1 = 0b1;

    for (size_t i = 0; i < itr_cnt; i++) {
      const size_t poff = i << 3;
      const size_t boff = i * 11;

      const auto t0 = (static_cast<uint16_t>(arr[boff + 1] & mask3) << 8) | static_cast<uint16_t>(arr[boff + 0]);
      const auto t1 = (static_cast<uint16_t>(arr[boff + 2] & mask6) << 5) | static_cast<uint16_t>(arr[boff + 1] >> 3);
      const auto t2 = (static_cast<uint16_t>(arr[boff + 4] & mask1) << 10) | (static_cast<uint16_t>(arr[boff + 3]) << 2) | static_cast<uint16_t>(arr[boff + 2] >> 6);
      const auto t3 = (static_cast<uint16_t>(arr[boff + 5] & mask4) << 7) | static_cast<uint16_t>(arr[boff + 4] >> 1);
      const auto t4 = (static_cast<uint16_t>(arr[boff + 6] & mask7) << 4) | static_cast<uint16_t>(arr[boff + 5] >> 4);
      const auto t5 = (static_cast<uint16_t>(arr[boff + 8] & mask2) << 9) | (static_cast<uint16_t>(arr[boff + 7]) << 1) | static_cast<uint16_t>(arr[boff + 6] >> 7);
      const auto t6 = (static_cast<uint16_t>(arr[boff + 9] & mask5) << 6) | static_cast<uint16_t>(arr[boff + 8] >> 2);
      const auto t7 = (static_cast<uint16_t>(arr[boff + 10]) << 3) | static_cast<uint16_t>(arr[boff + 9] >> 5);

      poly[poff + 0] = ml_kem_field::zq_t(t0);
      poly[poff + 1] = ml_kem_field::zq_t(t1);
      poly[poff + 2] = ml_kem_field::zq_t(t2);
      poly[poff + 3] = ml_kem_field::zq_t(t3);
      poly[poff + 4] = ml_kem_field::zq_t(t4);
      poly[poff + 5] = ml_kem_field::zq_t(t5);
      poly[poff + 6] = ml_kem_field::zq_t(t6);
      poly[poff + 7] = ml_kem_field::zq_t(t7);
    }
  } else {
    static_assert(l == 12, "l must be equal to 12 !");

    constexpr size_t itr_cnt = ml_kem_ntt::N >> 1;
    constexpr uint8_t mask4 = 0b1111;

    for (size_t i = 0; i < itr_cnt; i++) {
      const size_t poff = i << 1;
      const size_t boff = i * 3;

      const auto t0 = (static_cast<uint16_t>(arr[boff + 1] & mask4) << 8) | static_cast<uint16_t>(arr[boff + 0]);
      const auto t1 = (static_cast<uint16_t>(arr[boff + 2]) << 4) | static_cast<uint16_t>(arr[boff + 1] >> 4);

      // Read line (786-792) of ML-KEM specification https://doi.org/10.6028/NIST.FIPS.203.
      poly[poff + 0] = ml_kem_field::zq_t::from_non_reduced(t0);
      poly[poff + 1] = ml_kem_field::zq_t::from_non_reduced(t1);
    }
  }
}

}

namespace ml_kem_utils {

// Given two matrices ( in NTT domain ) of compatible dimension, where each matrix element is a degree-255 polynomial over Z_q | q = 3329,
// this routine multiplies them, computing a resulting matrix.
template<size_t a_rows, size_t a_cols, size_t b_rows, size_t b_cols>
constexpr void
matrix_multiply(std::span<const ml_kem_field::zq_t, a_rows * a_cols * ml_kem_ntt::N> a,
                std::span<const ml_kem_field::zq_t, b_rows * b_cols * ml_kem_ntt::N> b,
                std::span<ml_kem_field::zq_t, a_rows * b_cols * ml_kem_ntt::N> c)
  requires(ml_kem_params::check_matrix_dim(a_cols, b_rows))
{
  using poly_t = std::span<const ml_kem_field::zq_t, ml_kem_ntt::N>;

  std::array<ml_kem_field::zq_t, ml_kem_ntt::N> tmp{};
  auto tmp_span = std::span(tmp);

  for (size_t i = 0; i < a_rows; i++) {
    for (size_t j = 0; j < b_cols; j++) {
      const size_t coff = (i * b_cols + j) * ml_kem_ntt::N;

      for (size_t k = 0; k < a_cols; k++) {
        const size_t aoff = (i * a_cols + k) * ml_kem_ntt::N;
        const size_t boff = (k * b_cols + j) * ml_kem_ntt::N;

        ml_kem_ntt::polymul(poly_t(a.subspan(aoff, ml_kem_ntt::N)), poly_t(b.subspan(boff, ml_kem_ntt::N)), tmp_span);

        for (size_t l = 0; l < ml_kem_ntt::N; l++) {
          c[coff + l] += tmp[l];
        }
      }
    }
  }
}

// Given a vector ( of dimension `k x 1` ) of degree-255 polynomials ( where polynomial coefficients are in non-NTT form ),
// this routine applies in-place polynomial NTT over `k` polynomials.
template<size_t k>
constexpr void
poly_vec_ntt(std::span<ml_kem_field::zq_t, k * ml_kem_ntt::N> vec)
  requires((k == 1) || ml_kem_params::check_k(k))
{
  using poly_t = std::span<ml_kem_field::zq_t, ml_kem_ntt::N>;

  for (size_t i = 0; i < k; i++) {
    const size_t off = i * ml_kem_ntt::N;
    ml_kem_ntt::ntt(poly_t(vec.subspan(off, ml_kem_ntt::N)));
  }
}

// Given a vector ( of dimension `k x 1` ) of degree-255 polynomials ( where polynomial coefficients are in NTT form i.e.
// they are placed in bit-reversed order ), this routine applies in-place polynomial iNTT over those `k` polynomials.
template<size_t k>
constexpr void
poly_vec_intt(std::span<ml_kem_field::zq_t, k * ml_kem_ntt::N> vec)
  requires((k == 1) || ml_kem_params::check_k(k))
{
  using poly_t = std::span<ml_kem_field::zq_t, ml_kem_ntt::N>;

  for (size_t i = 0; i < k; i++) {
    const size_t off = i * ml_kem_ntt::N;
    ml_kem_ntt::intt(poly_t(vec.subspan(off, ml_kem_ntt::N)));
  }
}

// Given a vector ( of dimension `k x 1` ) of degree-255 polynomials, this routine adds it to another polynomial vector of same dimension.
template<size_t k>
constexpr void
poly_vec_add_to(std::span<const ml_kem_field::zq_t, k * ml_kem_ntt::N> src, std::span<ml_kem_field::zq_t, k * ml_kem_ntt::N> dst)
  requires((k == 1) || ml_kem_params::check_k(k))
{
  constexpr size_t cnt = k * ml_kem_ntt::N;

  for (size_t i = 0; i < cnt; i++) {
    dst[i] += src[i];
  }
}

// Given a vector ( of dimension `k x 1` ) of degree-255 polynomials, this routine subtracts it to another polynomial vector of same dimension.
template<size_t k>
constexpr void
poly_vec_sub_from(std::span<const ml_kem_field::zq_t, k * ml_kem_ntt::N> src, std::span<ml_kem_field::zq_t, k * ml_kem_ntt::N> dst)
  requires((k == 1) || ml_kem_params::check_k(k))
{
  constexpr size_t cnt = k * ml_kem_ntt::N;

  for (size_t i = 0; i < cnt; i++) {
    dst[i] -= src[i];
  }
}

// Given a vector ( of dimension `k x 1` ) of degree-255 polynomials, this routine encodes each of those polynomials into 32 x l -bytes,
// writing to a (k x 32 x l) -bytes destination array.
template<size_t k, size_t l>
constexpr void
poly_vec_encode(std::span<const ml_kem_field::zq_t, k * ml_kem_ntt::N> src, std::span<uint8_t, k * 32 * l> dst)
  requires(ml_kem_params::check_k(k))
{
  using poly_t = std::span<const ml_kem_field::zq_t, src.size() / k>;
  using serialized_t = std::span<uint8_t, dst.size() / k>;

  for (size_t i = 0; i < k; i++) {
    const size_t off0 = i * ml_kem_ntt::N;
    const size_t off1 = i * l * 32;

    ml_kem_utils::encode<l>(poly_t(src.subspan(off0, ml_kem_ntt::N)), serialized_t(dst.subspan(off1, 32 * l)));
  }
}

// Given a byte array of length (k x 32 x l) -bytes, this routine decodes them into k degree-255 polynomials, writing them to a
// column vector of dimension `k x 1`.
template<size_t k, size_t l>
constexpr void
poly_vec_decode(std::span<const uint8_t, k * 32 * l> src, std::span<ml_kem_field::zq_t, k * ml_kem_ntt::N> dst)
  requires(ml_kem_params::check_k(k))
{
  using serialized_t = std::span<const uint8_t, src.size() / k>;
  using poly_t = std::span<ml_kem_field::zq_t, dst.size() / k>;

  for (size_t i = 0; i < k; i++) {
    const size_t off0 = i * l * 32;
    const size_t off1 = i * ml_kem_ntt::N;

    ml_kem_utils::decode<l>(serialized_t(src.subspan(off0, 32 * l)), poly_t(dst.subspan(off1, ml_kem_ntt::N)));
  }
}

// Given a vector ( of dimension `k x 1` ) of degree-255 polynomials, each of k * 256 coefficients are compressed, while mutating input.
template<size_t k, size_t d>
constexpr void
poly_vec_compress(std::span<ml_kem_field::zq_t, k * ml_kem_ntt::N> vec)
  requires(ml_kem_params::check_k(k))
{
  using poly_t = std::span<ml_kem_field::zq_t, vec.size() / k>;

  for (size_t i = 0; i < k; i++) {
    const size_t off = i * ml_kem_ntt::N;
    ml_kem_utils::poly_compress<d>(poly_t(vec.subspan(off, ml_kem_ntt::N)));
  }
}

// Given a vector ( of dimension `k x 1` ) of degree-255 polynomials, each of k * 256 coefficients are decompressed, while mutating input.
template<size_t k, size_t d>
constexpr void
poly_vec_decompress(std::span<ml_kem_field::zq_t, k * ml_kem_ntt::N> vec)
  requires(ml_kem_params::check_k(k))
{
  using poly_t = std::span<ml_kem_field::zq_t, vec.size() / k>;

  for (size_t i = 0; i < k; i++) {
    const size_t off = i * ml_kem_ntt::N;
    ml_kem_utils::poly_decompress<d>(poly_t(vec.subspan(off, ml_kem_ntt::N)));
  }
}

}
#pragma once
#pragma once
#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>

// SHAKE128 Extendable Output Function : Keccak[256](M || 1111, d)
namespace shake128 {

// Number of rounds keccak-p[1600] is applied.
static constexpr size_t NUM_KECCAK_ROUNDS = keccak::MAX_NUM_ROUNDS;

// SHAKE128 XOF offers at max 128-bits of security.
static constexpr size_t TARGET_BIT_SECURITY_LEVEL = 128;

// Width of capacity portion of the sponge, in bits.
static constexpr size_t CAPACITY = 2 * TARGET_BIT_SECURITY_LEVEL;

// Width of rate portion of the sponge, in bits.
static constexpr size_t RATE = 1600 - CAPACITY;

// Domain separator bits, used for finalization.
static constexpr uint8_t DOM_SEP = 0b00001111;

// Bit-width of domain separator, starting from least significant bit.
static constexpr size_t DOM_SEP_BW = std::bit_width(DOM_SEP);

/**
 * SHAKE128 eXtendable Output Function (XOF).
 *
 * See SHA3 extendable output function definition in section 6.2 of SHA3 specification https://dx.doi.org/10.6028/NIST.FIPS.202.
 */
struct shake128_t
{
private:
  uint64_t state[keccak::LANE_CNT]{};
  size_t offset = 0;
  alignas(4) bool finalized = false; // All message bytes absorbed?
  size_t squeezable = 0;

public:
  forceinline constexpr shake128_t() = default;
  forceinline constexpr size_t squeezable_num_bytes() const { return squeezable; }

  /**
   * Given N -many bytes input message, this routine consumes those into keccak[256] sponge state.
   *
   * Note, this routine can be called arbitrary number of times, each time with arbitrary bytes of input message, until
   * keccak[256] state is finalized ( by calling routine with the same name ). Once the sponge is finalized, it can't
   * absorb any more message bytes.
   */
  forceinline constexpr void absorb(std::span<const uint8_t> msg)
  {
    if (!finalized) {
      sponge::absorb<RATE, NUM_KECCAK_ROUNDS>(state, offset, msg);
    }
  }

  /**
   * After consuming arbitrary many input bytes, this routine is invoked when no more input bytes remaining to be
   * consumed by keccak[256] state.
   *
   * Note, once this routine is called, calling absorb() or finalize() again, on same SHAKE128 object, doesn't do
   * anything. After finalization, one might intend to read arbitrary many bytes by squeezing sponge, which is done by
   * calling read() function, as many times required.
   */
  forceinline constexpr void finalize()
  {
    if (!finalized) {
      sponge::finalize<DOM_SEP, DOM_SEP_BW, RATE, NUM_KECCAK_ROUNDS>(state, offset);

      finalized = true;
      squeezable = RATE / std::numeric_limits<uint8_t>::digits;
    }
  }

  /**
   * After sponge state is finalized, arbitrary many output bytes can be squeezed by calling this function
   * any number of times required.
   */
  forceinline constexpr void squeeze(std::span<uint8_t> dig)
  {
    if (finalized) {
      sponge::squeeze<RATE, NUM_KECCAK_ROUNDS>(state, squeezable, dig);
    }
  }

  /**
   * Reset the internal state of the SHAKE128 XOF hasher.
   * Now it can again be used for another `absorb() -> finalize() -> squeeze()` cycle.
   */
  forceinline constexpr void reset()
  {
    std::fill(std::begin(state), std::end(state), 0);
    offset = 0;
    finalized = false;
    squeezable = 0;
  }

  /**
   * Given that sponge is already finalized, this routine can be used for zeroizing first n -bytes of
   * permutation state s.t. n <= 200 and applying permutation.
   */
  forceinline void ratchet(const size_t byte_len)
  {
    if (finalized) {
      const auto ratchetable_portion_byte_len = std::min(byte_len, keccak::STATE_BYTE_LEN);

      auto state_as_bytes = reinterpret_cast<uint8_t*>(state);
      std::memset(state_as_bytes, 0, ratchetable_portion_byte_len);

      keccak::permute<NUM_KECCAK_ROUNDS>(state);
    }
  }
};

}
#include <limits>

namespace ml_kem_utils {

// Uniform sampling in R_q | q = 3329.
//
// Given a byte stream, this routine *deterministically* samples a degree 255 polynomial in NTT representation.
// If the byte stream is statistically close to uniform random byte stream, produced polynomial coefficients are also
// statiscally close to randomly sampled elements of R_q.
//
// See algorithm 7 of ML-KEM specification https://doi.org/10.6028/NIST.FIPS.203.
forceinline constexpr void
sample_ntt(shake128::shake128_t& hasher, std::span<ml_kem_field::zq_t, ml_kem_ntt::N> poly)
{
  constexpr size_t n = poly.size();

  size_t coeff_idx = 0;
  std::array<uint8_t, shake128::RATE / std::numeric_limits<uint8_t>::digits> buf{};

  while (coeff_idx < n) {
    hasher.squeeze(buf);

    for (size_t off = 0; (off < buf.size()) && (coeff_idx < n); off += 3) {
      const uint16_t d1 = (static_cast<uint16_t>(buf[off + 1] & 0x0f) << 8) | static_cast<uint16_t>(buf[off + 0]);
      const uint16_t d2 = (static_cast<uint16_t>(buf[off + 2]) << 4) | (static_cast<uint16_t>(buf[off + 1] >> 4));

      if (d1 < ml_kem_field::Q) {
        poly[coeff_idx] = ml_kem_field::zq_t(d1);
        coeff_idx++;
      }

      if ((d2 < ml_kem_field::Q) && (coeff_idx < n)) {
        poly[coeff_idx] = ml_kem_field::zq_t(d2);
        coeff_idx++;
      }
    }
  }
}

// Generate public matrix A ( consists of degree-255 polynomials ) in NTT domain, by sampling from a XOF ( read SHAKE128 ),
// which is seeded with 32 -bytes key and two nonces ( each of 1 -byte ).
//
// See step (3-7) of algorithm 13 of ML-KEM specification https://doi.org/10.6028/NIST.FIPS.203.
template<size_t k, bool transpose>
constexpr void
generate_matrix(std::span<ml_kem_field::zq_t, k * k * ml_kem_ntt::N> mat, std::span<const uint8_t, 32> rho)
  requires(ml_kem_params::check_k(k))
{
  std::array<uint8_t, rho.size() + 2> xof_in{};
  std::copy(rho.begin(), rho.end(), xof_in.begin());

  for (size_t i = 0; i < k; i++) {
    for (size_t j = 0; j < k; j++) {
      const size_t off = (i * k + j) * ml_kem_ntt::N;

      if constexpr (transpose) {
        xof_in[32] = static_cast<uint8_t>(i);
        xof_in[33] = static_cast<uint8_t>(j);
      } else {
        xof_in[32] = static_cast<uint8_t>(j);
        xof_in[33] = static_cast<uint8_t>(i);
      }

      shake128::shake128_t hasher;
      hasher.absorb(xof_in);
      hasher.finalize();

      using poly_t = std::span<ml_kem_field::zq_t, mat.size() / (k * k)>;
      sample_ntt(hasher, poly_t(mat.subspan(off, ml_kem_ntt::N)));
    }
  }
}

// Centered Binomial Distribution.
// A degree 255 polynomial deterministically sampled from `64 * eta` -bytes output of a pseudorandom function ( PRF ).
//
// See algorithm 8 of ML-KEM specification https://doi.org/10.6028/NIST.FIPS.203.
template<size_t eta>
constexpr void
sample_poly_cbd(std::span<const uint8_t, 64 * eta> prf, std::span<ml_kem_field::zq_t, ml_kem_ntt::N> poly)
  requires(ml_kem_params::check_eta(eta))
{
  if constexpr (eta == 2) {
    static_assert(eta == 2, "η must be 2 !");

    constexpr size_t till = 64 * eta;
    constexpr uint8_t mask8 = 0b01010101;
    constexpr uint8_t mask2 = 0b11;

    for (size_t i = 0; i < till; i++) {
      const size_t poff = i << 1;
      const uint8_t word = prf[i];

      const uint8_t t0 = (word >> 0) & mask8;
      const uint8_t t1 = (word >> 1) & mask8;
      const uint8_t t2 = t0 + t1;

      poly[poff + 0] = ml_kem_field::zq_t((t2 >> 0) & mask2) - ml_kem_field::zq_t((t2 >> 2) & mask2);
      poly[poff + 1] = ml_kem_field::zq_t((t2 >> 4) & mask2) - ml_kem_field::zq_t((t2 >> 6) & mask2);
    }
  } else {
    static_assert(eta == 3, "η must be 3 !");

    constexpr size_t till = 64;
    constexpr uint32_t mask24 = 0b001001001001001001001001u;
    constexpr uint32_t mask3 = 0b111u;

    for (size_t i = 0; i < till; i++) {
      const size_t boff = i * 3;
      const size_t poff = i << 2;

      const uint32_t word = (static_cast<uint32_t>(prf[boff + 2]) << 16) | (static_cast<uint32_t>(prf[boff + 1]) << 8) | static_cast<uint32_t>(prf[boff + 0]);

      const uint32_t t0 = (word >> 0) & mask24;
      const uint32_t t1 = (word >> 1) & mask24;
      const uint32_t t2 = (word >> 2) & mask24;
      const uint32_t t3 = t0 + t1 + t2;

      poly[poff + 0] = ml_kem_field::zq_t((t3 >> 0) & mask3) - ml_kem_field::zq_t((t3 >> 3) & mask3);
      poly[poff + 1] = ml_kem_field::zq_t((t3 >> 6) & mask3) - ml_kem_field::zq_t((t3 >> 9) & mask3);
      poly[poff + 2] = ml_kem_field::zq_t((t3 >> 12) & mask3) - ml_kem_field::zq_t((t3 >> 15) & mask3);
      poly[poff + 3] = ml_kem_field::zq_t((t3 >> 18) & mask3) - ml_kem_field::zq_t((t3 >> 21) & mask3);
    }
  }
}

// Sample a polynomial vector from Bη, following step (8-11) of algorithm 13 of ML-KEM specification https://doi.org/10.6028/NIST.FIPS.203.
template<size_t k, size_t eta>
constexpr void
generate_vector(std::span<ml_kem_field::zq_t, k * ml_kem_ntt::N> vec, std::span<const uint8_t, 32> sigma, const uint8_t nonce)
  requires((k == 1) || ml_kem_params::check_k(k))
{
  std::array<uint8_t, 64 * eta> prf_out{};
  std::array<uint8_t, sigma.size() + 1> prf_in{};
  std::copy(sigma.begin(), sigma.end(), prf_in.begin());

  for (size_t i = 0; i < k; i++) {
    const size_t off = i * ml_kem_ntt::N;

    prf_in[32] = nonce + static_cast<uint8_t>(i);

    shake256::shake256_t hasher;
    hasher.absorb(prf_in);
    hasher.finalize();
    hasher.squeeze(prf_out);

    using poly_t = std::span<ml_kem_field::zq_t, vec.size() / k>;
    ml_kem_utils::sample_poly_cbd<eta>(prf_out, poly_t(vec.subspan(off, ml_kem_ntt::N)));
  }
}

}
#pragma once
#pragma once
#include <cstddef>
#include <cstdint>
#include <type_traits>

// Constant-time comparison and selection of unsigned integer values.
namespace subtle {

// Given two unsigned integers x, y of type operandT ( of bitwidth 8, 16, 32 or
// 64 ), this routine returns true ( if x == y ) or false ( in case x != y )
// testing equality of two values.
//
// We represent truth value using maximum number that can be represented using
// returnT i.e. all bits of returnT are set to one. While for false value, we
// set all bits of returnT to zero.
template<typename operandT, typename returnT>
static inline constexpr returnT
ct_eq(const operandT x, const operandT y)
  requires(std::is_unsigned_v<operandT> && std::is_unsigned_v<returnT>)
{
  const operandT a = x ^ y;
  const operandT b = a | (-a);
  const operandT c = b >> ((sizeof(operandT) * 8) - 1); // select only MSB
  const returnT d = static_cast<returnT>(c);
  const returnT e = d - static_cast<returnT>(1);

  return e;
}

// Given two unsigned integers x, y of type operandT ( of bitwidth 8, 16, 32 or
// 64 ), this routine returns true ( if x != y ) or false ( in case x == y )
// testing inequality of two values.
//
// We represent truth value using maximum number that can be represented using
// returnT i.e. all bits of returnT are set to one. While for false value, we
// set all bits of returnT to zero.
template<typename operandT, typename returnT>
static inline constexpr returnT
ct_ne(const operandT x, const operandT y)
  requires(std::is_unsigned_v<operandT> && std::is_unsigned_v<returnT>)
{
  const returnT z = ct_eq<operandT, returnT>(x, y);
  return ~z; // bit-inverted result of equality check
}

// Given a branch value br ( of type branchT ) holding either truth or false
// value and two unsigned integers x, y ( of bitwidth 8, 16, 32 or 64 ), this
// routine selects x if br is truth value or it returns y.
//
// Branch value br can have either of two values
//
// - truth value is represented using all bits of type branchT set to 1
// - false value is represented using all bits of type branchT set to 0
//
// If br takes any other value, this is an undefined behaviour !
template<typename branchT, typename operandT>
static inline constexpr operandT
ct_select(const branchT br, const operandT x, const operandT y)
  requires(std::is_unsigned_v<branchT> && std::is_unsigned_v<operandT>)
{
  const branchT z = br >> ((sizeof(branchT) * 8) - 1); // select MSB
  const operandT w = -static_cast<operandT>(z);        // bw(br) = bw(x) = bw(y)
  const operandT selected = (x & w) | (y & (~w));      // br ? x : y

  return selected;
}

// Given a branch value br ( of type branchT ) holding either truth value (
// represented using value of type branchT s.t. all the bits are set to 1 ) or
// false value ( represented using value of type branchT s.t. all the bits are
// set to 0 ), this function swaps values held by x, y if br is truth value.
// Otherwise both x, y keep their values.
//
// branchT and operandT can be unsigned integers of bit width 8, 16, 32 or 64.
template<typename branchT, typename operandT>
static inline constexpr void
ct_swap(const branchT br, operandT& x, operandT& y)
  requires(std::is_unsigned_v<branchT> && std::is_unsigned_v<operandT>)
{
  const operandT mask = -static_cast<operandT>(br & 1);

  x = x ^ (mask & y);
  y = y ^ (mask & x);
  x = x ^ (mask & y);
}

// Given two unsigned integers x, y of type operandT ( of bitwidth 8, 16, 32 or
// 64 ), this routine returns truth value ( if x <= y ) or false value ( in case
// x > y ) based on result of lesser than equal comparison test.
//
// We represent truth value using maximum number that can be represented using
// type returnT i.e. all bits of returnT are set to one. While for false value,
// we set all bits of returnT to zero.
//
// This implementation collects a lot of inspiration from
// https://github.com/dalek-cryptography/subtle/blob/bd282be01f1c2da8ab03922e03457102a76a0e05/src/lib.rs#L808-L841
//
// I also found
// https://cs.opensource.google/go/go/+/refs/tags/go1.20.1:src/crypto/subtle/constant_time.go;l=56-62
// to be pretty helpful.
template<typename operandT, typename returnT>
static inline constexpr returnT
ct_le(const operandT x, const operandT y)
  requires(std::is_unsigned_v<operandT> && std::is_unsigned_v<returnT>)
{
  const operandT gt_bits = x & ~y;
  operandT lt_bits = ~x & y;

  for (size_t pow = 1; pow < sizeof(operandT) * 8;) {
    lt_bits |= lt_bits >> pow;
    pow += pow;
  }

  operandT bit = gt_bits & ~lt_bits;

  for (size_t pow = 1; pow < sizeof(operandT) * 8;) {
    bit |= bit >> pow;
    pow += pow;
  }

  const returnT z = -static_cast<returnT>((bit & 1) ^ 1);
  return z;
}

// Given two unsigned integers x, y of type operandT ( of bitwidth 8, 16, 32 or
// 64 ), this routine returns truth value ( if x > y ) or false value ( in case
// x <= y ) based on result of greater than comparison test.
//
// We represent truth value using maximum number that can be represented using
// type returnT i.e. all bits of returnT are set to one. While for false value,
// we set all bits of returnT to zero.
template<typename operandT, typename returnT>
static inline constexpr returnT
ct_gt(const operandT x, const operandT y)
  requires(std::is_unsigned_v<operandT> && std::is_unsigned_v<returnT>)
{
  const returnT z = ct_le<operandT, returnT>(x, y);
  return ~z; // bit-inverted result of <= test
}

// Given two unsigned integers x, y of type operandT ( of bitwidth 8, 16, 32 or
// 64 ), this routine returns truth value ( if x >= y ) or false value ( in case
// x < y ) based on result of greater than equal comparison test.
//
// We represent truth value using maximum number that can be represented using
// type returnT i.e. all bits of returnT are set to one. While for false value,
// we set all bits of returnT to zero.
template<typename operandT, typename returnT>
static inline constexpr returnT
ct_ge(const operandT x, const operandT y)
  requires(std::is_unsigned_v<operandT> && std::is_unsigned_v<returnT>)
{
  const returnT z0 = ct_gt<operandT, returnT>(x, y);
  const returnT z1 = ct_eq<operandT, returnT>(x, y);
  return z0 | z1; // (x > y) | (x == y)
}

// Given two unsigned integers x, y of type operandT ( of bitwidth 8, 16, 32 or
// 64 ), this routine returns truth value ( if x < y ) or false value ( in case
// x >= y ) based on result of lesser than comparison test.
//
// We represent truth value using maximum number that can be represented using
// type returnT i.e. all bits of returnT are set to one. While for false value,
// we set all bits of returnT to zero.
template<typename operandT, typename returnT>
static inline constexpr returnT
ct_lt(const operandT x, const operandT y)
  requires(std::is_unsigned_v<operandT> && std::is_unsigned_v<returnT>)
{
  const returnT z = ct_ge<operandT, returnT>(x, y);
  return ~z; // bit-inverted result of >= test
}

}
#include <span>

namespace ml_kem_utils {

// Given two byte arrays of equal length, this routine can be used for comparing them in constant-time,
// producing truth value (0xffffffff) in case of equality, otherwise it returns false value (0x00000000).
template<size_t n>
forceinline constexpr uint32_t
ct_memcmp(std::span<const uint8_t, n> bytes0, std::span<const uint8_t, n> bytes1)
{
  uint32_t flag = -1u;
  for (size_t i = 0; i < n; i++) {
    flag &= subtle::ct_eq<uint8_t, uint32_t>(bytes0[i], bytes1[i]);
  }

  return flag;
}

// Given a branch value, taking either 0x00000000 (false value) or 0xffffffff (truth value), this routine can be used for conditionally
// copying bytes from either `source0` byte array (in case branch holds truth value) or `source1` byte array (if branch holds false value)
// to `sink` byte array, all in constant-time.
//
// In simple words, `sink = cond ? source0 ? source1`
template<size_t n>
forceinline constexpr void
ct_cond_memcpy(const uint32_t cond, std::span<uint8_t, n> sink, std::span<const uint8_t, n> source0, std::span<const uint8_t, n> source1)
{
  for (size_t i = 0; i < n; i++) {
    sink[i] = subtle::ct_select(cond, source0[i], source1[i]);
  }
}

// Returns compile-time computable K-PKE public key byte length.
forceinline constexpr size_t
get_pke_public_key_len(const size_t k)
{
  return k * 12 * 32 + 32;
}

// Returns compile-time computable K-PKE secret key byte length.
forceinline constexpr size_t
get_pke_secret_key_len(const size_t k)
{
  return k * 12 * 32;
}

// Returns compile-time computable K-PKE cipher text byte length.
forceinline constexpr size_t
get_pke_cipher_text_len(size_t k, size_t du, size_t dv)
{
  return 32 * (k * du + dv);
}

// Returns compile-time computable ML-KEM public key byte length.
forceinline constexpr size_t
get_kem_public_key_len(const size_t k)
{
  return get_pke_public_key_len(k);
}

// Returns compile-time computable ML-KEM secret key byte length.
forceinline constexpr size_t
get_kem_secret_key_len(const size_t k)
{
  return get_pke_secret_key_len(k) + get_pke_public_key_len(k) + 32 + 32;
}

// Returns compile-time computable ML-KEM cipher text byte length.
forceinline constexpr size_t
get_kem_cipher_text_len(size_t k, size_t du, size_t dv)
{
  return get_pke_cipher_text_len(k, du, dv);
}

}
#pragma once
#include <bit>
#include <cstdint>
#include <limits>

// SHA3-512 Hash Function : Keccak[1024](M || 01, 512)
namespace sha3_512 {

// Number of rounds keccak-p[1600] is applied.
static constexpr size_t NUM_KECCAK_ROUNDS = keccak::MAX_NUM_ROUNDS;

// Bit length of SHA3-512 message digest.
static constexpr size_t DIGEST_BIT_LEN = 512;

// Byte length of SHA3-512 message digest.
static constexpr size_t DIGEST_LEN = DIGEST_BIT_LEN / std::numeric_limits<uint8_t>::digits;

// Width of capacity portion of the sponge, in bits.
static constexpr size_t CAPACITY = 2 * DIGEST_BIT_LEN;

// Width of rate portion of the sponge, in bits.
static constexpr size_t RATE = 1600 - CAPACITY;

// Domain separator bits, used for finalization.
static constexpr uint8_t DOM_SEP = 0b00000010;

// Bit-width of domain separator, starting from least significant bit.
static constexpr size_t DOM_SEP_BW = std::bit_width(DOM_SEP);

/**
 * Given arbitrary many input message bytes, this routine consumes it into keccak[1024] sponge state and squeezes out 64 -bytes digest.
 *
 * See SHA3 hash function definition in section 6.1 of SHA3 specification https://dx.doi.org/10.6028/NIST.FIPS.202.
 */
struct sha3_512_t
{
private:
  uint64_t state[keccak::LANE_CNT]{};
  size_t offset = 0;
  alignas(4) bool finalized = false;
  alignas(4) bool squeezed = false;

public:
  // Constructor
  forceinline constexpr sha3_512_t() = default;

  /**
   * Given an arbitrary length message, absorbs it all in the SHA3_512 hasher and returns a 64 -bytes message digest.
   * This is the oneshot hashing API. For working with longer message stream, prefer using `absorb() -> finalize() -> digest()`.
   */
  forceinline static constexpr std::array<uint8_t, DIGEST_LEN> hash(std::span<const uint8_t> msg)
  {
    std::array<uint8_t, DIGEST_LEN> md{ 0 };

    sha3_512_t hasher;
    hasher.absorb(msg);
    hasher.finalize();
    hasher.digest(md);

    return md;
  }

  /*
   * Given N (>=0) -bytes message as input, this routine can be invoked arbitrary many times ( until the sponge is
   * finalized ), each time absorbing arbitrary many message bytes into RATE portion of the sponge.
   */
  forceinline constexpr void absorb(std::span<const uint8_t> msg)
  {
    if (!finalized) {
      sponge::absorb<RATE, NUM_KECCAK_ROUNDS>(state, offset, msg);
    }
  }

  /**
   * Finalizes the sponge after all message bytes are absorbed into it, now it should be ready for squeezing message
   * digest bytes. Once finalized, you can't absorb any message bytes into sponge. After finalization, calling this
   * function again and again doesn't mutate anything.
   */
  forceinline constexpr void finalize()
  {
    if (!finalized) {
      sponge::finalize<DOM_SEP, DOM_SEP_BW, RATE, NUM_KECCAK_ROUNDS>(state, offset);
      finalized = true;
    }
  }

  /**
   * After sponge state is finalized, 64 message digest bytes can be squeezed by calling this function.
   * Once digest bytes are squeezed, calling this function again and again returns nothing.
   */
  forceinline constexpr void digest(std::span<uint8_t, DIGEST_LEN> md)
  {
    if (finalized && !squeezed) {
      size_t squeezable = RATE / std::numeric_limits<uint8_t>::digits;
      sponge::squeeze<RATE, NUM_KECCAK_ROUNDS>(state, squeezable, md);

      squeezed = true;
    }
  }

  /**
   * Reset the internal state of the SHA3-512 hasher.
   * Now it can again be used for another `absorb() -> finalize() -> digest()` cycle.
   */
  forceinline constexpr void reset()
  {
    std::fill(std::begin(state), std::end(state), 0);
    offset = 0;
    finalized = false;
    squeezed = false;
  }
};

}

// Public Key Encryption Scheme
namespace k_pke {

// K-PKE key generation algorithm, generating byte serialized public key and secret keym given a 32 -bytes input seed `d`.
// See algorithm 13 of K-PKE specification https://doi.org/10.6028/NIST.FIPS.203.
template<size_t k, size_t eta1>
constexpr void
keygen(std::span<const uint8_t, 32> d,
       std::span<uint8_t, ml_kem_utils::get_pke_public_key_len(k)> pubkey,
       std::span<uint8_t, ml_kem_utils::get_pke_secret_key_len(k)> seckey)
  requires(ml_kem_params::check_keygen_params(k, eta1))
{
  std::array<uint8_t, 64> g_out{};
  auto g_out_span = std::span(g_out);

  // Repurposing `g_out` (i.e. array for holding output of hash function G),
  // for preparing the concatenated input to hash function G.
  std::copy(d.begin(), d.end(), g_out_span.begin());
  g_out_span[d.size()] = k; // Domain seperator to prevent misuse of key

  sha3_512::sha3_512_t h512;
  h512.absorb(g_out_span.template first<d.size() + 1>());
  h512.finalize();
  h512.digest(g_out_span);

  const auto rho = g_out_span.template subspan<0, 32>();
  const auto sigma = g_out_span.template subspan<rho.size(), 32>();

  std::array<ml_kem_field::zq_t, k * k * ml_kem_ntt::N> A_prime{};
  ml_kem_utils::generate_matrix<k, false>(A_prime, rho);

  uint8_t N = 0;

  std::array<ml_kem_field::zq_t, k * ml_kem_ntt::N> s{};
  ml_kem_utils::generate_vector<k, eta1>(s, sigma, N);
  N += k;

  std::array<ml_kem_field::zq_t, k * ml_kem_ntt::N> e{};
  ml_kem_utils::generate_vector<k, eta1>(e, sigma, N);
  N += k;

  ml_kem_utils::poly_vec_ntt<k>(s);
  ml_kem_utils::poly_vec_ntt<k>(e);

  std::array<ml_kem_field::zq_t, k * ml_kem_ntt::N> t_prime{};

  ml_kem_utils::matrix_multiply<k, k, k, 1>(A_prime, s, t_prime);
  ml_kem_utils::poly_vec_add_to<k>(e, t_prime);

  constexpr size_t pubkey_offset = k * 12 * 32;
  auto encoded_t_prime_in_pubkey = pubkey.template subspan<0, pubkey_offset>();
  auto rho_in_pubkey = pubkey.template subspan<pubkey_offset, 32>();

  ml_kem_utils::poly_vec_encode<k, 12>(t_prime, encoded_t_prime_in_pubkey);
  std::copy(rho.begin(), rho.end(), rho_in_pubkey.begin());
  ml_kem_utils::poly_vec_encode<k, 12>(s, seckey);
}

// Given a *valid* K-PKE public key, 32 -bytes message ( to be encrypted ) and 32 -bytes random coin
// ( from where all randomness is deterministically sampled ), this routine encrypts message using
// K-PKE encryption algorithm, computing compressed cipher text.
//
// If modulus check, as described in point (2) of section 7.2 of ML-KEM standard, fails, it returns false.
//
// See algorithm 14 of K-PKE specification https://doi.org/10.6028/NIST.FIPS.203.
template<size_t k, size_t eta1, size_t eta2, size_t du, size_t dv>
[[nodiscard("Use result of modulus check on public key")]] constexpr bool
encrypt(std::span<const uint8_t, ml_kem_utils::get_pke_public_key_len(k)> pubkey,
        std::span<const uint8_t, 32> msg,
        std::span<const uint8_t, 32> rcoin,
        std::span<uint8_t, ml_kem_utils::get_pke_cipher_text_len(k, du, dv)> ctxt)
  requires(ml_kem_params::check_encrypt_params(k, eta1, eta2, du, dv))
{
  constexpr size_t pkoff = k * 12 * 32;
  auto encoded_t_prime_in_pubkey = pubkey.template subspan<0, pkoff>();
  auto rho = pubkey.template subspan<pkoff, 32>();

  std::array<ml_kem_field::zq_t, k * ml_kem_ntt::N> t_prime{};
  std::array<uint8_t, encoded_t_prime_in_pubkey.size()> encoded_tprime{};

  ml_kem_utils::poly_vec_decode<k, 12>(encoded_t_prime_in_pubkey, t_prime);
  ml_kem_utils::poly_vec_encode<k, 12>(t_prime, encoded_tprime);

  using encoded_pkey_t = std::span<const uint8_t, encoded_t_prime_in_pubkey.size()>;
  const auto are_equal = ml_kem_utils::ct_memcmp(encoded_pkey_t(encoded_t_prime_in_pubkey), encoded_pkey_t(encoded_tprime));
  if (are_equal == 0u) {
    // Got an invalid public key
    return false;
  }

  std::array<ml_kem_field::zq_t, k * k * ml_kem_ntt::N> A_prime{};
  ml_kem_utils::generate_matrix<k, true>(A_prime, rho);

  uint8_t N = 0;

  std::array<ml_kem_field::zq_t, k * ml_kem_ntt::N> r{};
  ml_kem_utils::generate_vector<k, eta1>(r, rcoin, N);
  N += k;

  std::array<ml_kem_field::zq_t, k * ml_kem_ntt::N> e1{};
  ml_kem_utils::generate_vector<k, eta2>(e1, rcoin, N);
  N += k;

  std::array<ml_kem_field::zq_t, ml_kem_ntt::N> e2{};
  ml_kem_utils::generate_vector<1, eta2>(e2, rcoin, N);

  ml_kem_utils::poly_vec_ntt<k>(r);

  std::array<ml_kem_field::zq_t, k * ml_kem_ntt::N> u{};

  ml_kem_utils::matrix_multiply<k, k, k, 1>(A_prime, r, u);
  ml_kem_utils::poly_vec_intt<k>(u);
  ml_kem_utils::poly_vec_add_to<k>(e1, u);

  std::array<ml_kem_field::zq_t, ml_kem_ntt::N> v{};

  ml_kem_utils::matrix_multiply<1, k, k, 1>(t_prime, r, v);
  ml_kem_utils::poly_vec_intt<1>(v);
  ml_kem_utils::poly_vec_add_to<1>(e2, v);

  std::array<ml_kem_field::zq_t, ml_kem_ntt::N> m{};
  ml_kem_utils::decode<1>(msg, m);
  ml_kem_utils::poly_decompress<1>(m);
  ml_kem_utils::poly_vec_add_to<1>(m, v);

  constexpr size_t ctxt_offset = k * du * 32;
  auto polyvec_u_in_ctxt = ctxt.template first<ctxt_offset>();
  auto poly_v_in_ctxt = ctxt.template last<dv * 32>();

  ml_kem_utils::poly_vec_compress<k, du>(u);
  ml_kem_utils::poly_vec_encode<k, du>(u, polyvec_u_in_ctxt);

  ml_kem_utils::poly_compress<dv>(v);
  ml_kem_utils::encode<dv>(v, poly_v_in_ctxt);

  return true;
}

// Given K-PKE secret key and cipher text, this routine recovers 32 -bytes plain text which
// was encrypted using K-PKE public key i.e. associated with this secret key.
//
// See algorithm 15 defined in K-PKE specification https://doi.org/10.6028/NIST.FIPS.203.
template<size_t k, size_t du, size_t dv>
constexpr void
decrypt(std::span<const uint8_t, ml_kem_utils::get_pke_secret_key_len(k)> seckey,
        std::span<const uint8_t, ml_kem_utils::get_pke_cipher_text_len(k, du, dv)> ctxt,
        std::span<uint8_t, 32> ptxt)
  requires(ml_kem_params::check_decrypt_params(k, du, dv))
{
  constexpr size_t ctxt_offset = k * du * 32;
  auto polyvec_u_in_ctxt = ctxt.template subspan<0, ctxt_offset>();
  auto poly_v_in_ctxt = ctxt.template subspan<ctxt_offset, dv * 32>();

  std::array<ml_kem_field::zq_t, k * ml_kem_ntt::N> u{};
  std::array<ml_kem_field::zq_t, ml_kem_ntt::N> v{};

  ml_kem_utils::poly_vec_decode<k, du>(polyvec_u_in_ctxt, u);
  ml_kem_utils::poly_vec_decompress<k, du>(u);

  ml_kem_utils::decode<dv>(poly_v_in_ctxt, v);
  ml_kem_utils::poly_decompress<dv>(v);

  std::array<ml_kem_field::zq_t, k * ml_kem_ntt::N> s_prime{};
  ml_kem_utils::poly_vec_decode<k, 12>(seckey, s_prime);

  ml_kem_utils::poly_vec_ntt<k>(u);

  std::array<ml_kem_field::zq_t, ml_kem_ntt::N> t{};

  ml_kem_utils::matrix_multiply<1, k, k, 1>(s_prime, u, t);
  ml_kem_utils::poly_vec_intt<1>(t);
  ml_kem_utils::poly_vec_sub_from<1>(t, v);

  ml_kem_utils::poly_compress<1>(v);
  ml_kem_utils::encode<1>(v, ptxt);
}

}
#pragma once
#include <bit>
#include <cstdint>
#include <limits>

// SHA3-256 Hash Function : Keccak[512](M || 01, 256)
namespace sha3_256 {

// Number of rounds keccak-p[1600] is applied.
static constexpr size_t NUM_KECCAK_ROUNDS = keccak::MAX_NUM_ROUNDS;

// Bit length of SHA3-256 message digest.
static constexpr size_t DIGEST_BIT_LEN = 256;

// Byte length of SHA3-256 message digest.
static constexpr size_t DIGEST_LEN = DIGEST_BIT_LEN / std::numeric_limits<uint8_t>::digits;

// Width of capacity portion of the sponge, in bits.
static constexpr size_t CAPACITY = 2 * DIGEST_BIT_LEN;

// Width of rate portion of the sponge, in bits.
static constexpr size_t RATE = 1600 - CAPACITY;

// Domain separator bits, used for finalization.
static constexpr uint8_t DOM_SEP = 0b00000010;

// Bit-width of domain separator, starting from least significant bit.
static constexpr size_t DOM_SEP_BW = std::bit_width(DOM_SEP);

/**
 * Given arbitrary many input message bytes, this routine consumes it into keccak[512] sponge state and squeezes out 32 -bytes digest.
 *
 * See SHA3 hash function definition in section 6.1 of SHA3 specification https://dx.doi.org/10.6028/NIST.FIPS.202.
 */
struct sha3_256_t
{
private:
  uint64_t state[keccak::LANE_CNT]{};
  size_t offset = 0;
  alignas(4) bool finalized = false;
  alignas(4) bool squeezed = false;

public:
  // Constructor
  forceinline constexpr sha3_256_t() = default;

  /**
   * Given an arbitrary length message, absorbs it all in the SHA3_256 hasher and returns a 32 -bytes message digest.
   * This is the oneshot hashing API. For working with longer message stream, prefer using `absorb() -> finalize() -> digest()`.
   */
  forceinline static constexpr std::array<uint8_t, DIGEST_LEN> hash(std::span<const uint8_t> msg)
  {
    std::array<uint8_t, DIGEST_LEN> md{ 0 };

    sha3_256_t hasher;
    hasher.absorb(msg);
    hasher.finalize();
    hasher.digest(md);

    return md;
  }

  /**
   * Given N (>=0) -bytes message as input, this routine can be invoked arbitrary many times ( until the sponge is
   * finalized ), each time absorbing arbitrary many message bytes into RATE portion of the sponge.
   */
  forceinline constexpr void absorb(std::span<const uint8_t> msg)
  {
    if (!finalized) {
      sponge::absorb<RATE, NUM_KECCAK_ROUNDS>(state, offset, msg);
    }
  }

  /**
   * Finalizes the sponge after all message bytes are absorbed into it, now it should be ready for squeezing message
   * digest bytes. Once finalized, you can't absorb any message bytes into sponge. After finalization, calling this
   * function again and again doesn't mutate anything.
   */
  forceinline constexpr void finalize()
  {
    if (!finalized) {
      sponge::finalize<DOM_SEP, DOM_SEP_BW, RATE, NUM_KECCAK_ROUNDS>(state, offset);
      finalized = true;
    }
  }

  /**
   * After sponge state is finalized, 32 message digest (MD) bytes can be squeezed by calling this function.
   * Once digest bytes are squeezed, calling this function again and again returns nothing.
   */
  forceinline constexpr void digest(std::span<uint8_t, DIGEST_LEN> md)
  {
    if (finalized && !squeezed) {
      size_t squeezable = RATE / std::numeric_limits<uint8_t>::digits;
      sponge::squeeze<RATE, NUM_KECCAK_ROUNDS>(state, squeezable, md);

      squeezed = true;
    }
  }

  /**
   * Reset the internal state of the SHA3-256 hasher.
   * Now it can again be used for another `absorb() -> finalize() -> digest()` cycle.
   */
  forceinline constexpr void reset()
  {
    std::fill(std::begin(state), std::end(state), 0);
    offset = 0;
    finalized = false;
    squeezed = false;
  }
};

}
#include <algorithm>

// Key Encapsulation Mechanism
namespace ml_kem {

// ML-KEM key generation algorithm, generating byte serialized public key and secret key, given 32 -bytes seed `d` and `z`.
// See algorithm 16 defined in ML-KEM specification https://doi.org/10.6028/NIST.FIPS.203.
template<size_t k, size_t eta1>
constexpr void
keygen(std::span<const uint8_t, 32> d, // used in CPA-PKE
       std::span<const uint8_t, 32> z, // used in CCA-KEM
       std::span<uint8_t, ml_kem_utils::get_kem_public_key_len(k)> pubkey,
       std::span<uint8_t, ml_kem_utils::get_kem_secret_key_len(k)> seckey)
  requires(ml_kem_params::check_keygen_params(k, eta1))
{
  constexpr size_t seckey_offset_kpke_skey = k * 12 * 32;
  constexpr size_t seckey_offset_kpke_pkey = seckey_offset_kpke_skey + pubkey.size();
  constexpr size_t seckey_offset_z = seckey_offset_kpke_pkey + 32;

  auto kpke_skey_in_seckey = seckey.template subspan<0, seckey_offset_kpke_skey>();
  auto kpke_pkey_in_seckey = seckey.template subspan<seckey_offset_kpke_skey, seckey_offset_kpke_pkey - seckey_offset_kpke_skey>();
  auto kpke_pkey_digest_in_seckey = seckey.template subspan<seckey_offset_kpke_pkey, seckey_offset_z - seckey_offset_kpke_pkey>();
  auto z_in_seckey = seckey.template subspan<seckey_offset_z, seckey.size() - seckey_offset_z>();

  k_pke::keygen<k, eta1>(d, kpke_pkey_in_seckey, kpke_skey_in_seckey);
  std::copy(kpke_pkey_in_seckey.begin(), kpke_pkey_in_seckey.end(), pubkey.begin());
  std::copy(z.begin(), z.end(), z_in_seckey.begin());

  sha3_256::sha3_256_t hasher{};
  hasher.absorb(pubkey);
  hasher.finalize();
  hasher.digest(kpke_pkey_digest_in_seckey);
  hasher.reset();
}

// Given ML-KEM public key and 32 -bytes seed ( used for deriving 32 -bytes message & 32 -bytes random coin ), this routine computes
// ML-KEM cipher text which can be shared with recipient party ( owning corresponding secret key ) over insecure channel.
//
// It also computes a fixed length 32 -bytes shared secret, which can be used for fast symmetric key encryption between these
// two participating entities. Alternatively they might choose to derive longer keys from this shared secret. Other side of
// communication should also be able to generate same 32 -byte shared secret, after successful decryption of cipher text.
//
// If invalid ML-KEM public key is input, this function execution will fail, returning false.
//
// See algorithm 17 defined in ML-KEM specification https://doi.org/10.6028/NIST.FIPS.203.
template<size_t k, size_t eta1, size_t eta2, size_t du, size_t dv>
[[nodiscard("Use result, it might fail because of malformed input public key")]] constexpr bool
encapsulate(std::span<const uint8_t, 32> m,
            std::span<const uint8_t, ml_kem_utils::get_kem_public_key_len(k)> pubkey,
            std::span<uint8_t, ml_kem_utils::get_kem_cipher_text_len(k, du, dv)> cipher,
            std::span<uint8_t, 32> shared_secret)
  requires(ml_kem_params::check_encap_params(k, eta1, eta2, du, dv))
{
  std::array<uint8_t, m.size() + sha3_256::DIGEST_LEN> g_in{};
  std::array<uint8_t, sha3_512::DIGEST_LEN> g_out{};

  auto g_in_span = std::span(g_in);
  auto g_in_span0 = g_in_span.template first<m.size()>();
  auto g_in_span1 = g_in_span.template last<sha3_256::DIGEST_LEN>();

  auto g_out_span = std::span(g_out);
  auto g_out_span0 = g_out_span.template first<shared_secret.size()>();
  auto g_out_span1 = g_out_span.template last<g_out_span.size() - g_out_span0.size()>();

  std::copy(m.begin(), m.end(), g_in_span0.begin());

  sha3_256::sha3_256_t h256{};
  h256.absorb(pubkey);
  h256.finalize();
  h256.digest(g_in_span1);

  sha3_512::sha3_512_t h512{};
  h512.absorb(g_in_span);
  h512.finalize();
  h512.digest(g_out_span);

  const auto has_mod_check_passed = k_pke::encrypt<k, eta1, eta2, du, dv>(pubkey, m, g_out_span1, cipher);
  if (!has_mod_check_passed) {
    // Got an invalid public key
    return has_mod_check_passed;
  }

  std::copy(g_out_span0.begin(), g_out_span0.end(), shared_secret.begin());
  return true;
}

// Given ML-KEM secret key and cipher text, this routine recovers 32 -bytes plain text which was encrypted by sender,
// using ML-KEM public key, associated with this secret key.
//
// Recovered 32 -bytes plain text is used for deriving a 32 -bytes shared secret key, which can now be
// used for encrypting communication between two participating parties, using fast symmetric key algorithms.
//
// See algorithm 18 defined in ML-KEM specification https://doi.org/10.6028/NIST.FIPS.203.
template<size_t k, size_t eta1, size_t eta2, size_t du, size_t dv>
constexpr void
decapsulate(std::span<const uint8_t, ml_kem_utils::get_kem_secret_key_len(k)> seckey,
            std::span<const uint8_t, ml_kem_utils::get_kem_cipher_text_len(k, du, dv)> cipher,
            std::span<uint8_t, 32> shared_secret)
  requires(ml_kem_params::check_decap_params(k, eta1, eta2, du, dv))
{
  constexpr size_t sklen = k * 12 * 32;
  constexpr size_t pklen = k * 12 * 32 + 32;
  constexpr size_t ctlen = cipher.size();

  constexpr size_t skoff0 = sklen;
  constexpr size_t skoff1 = skoff0 + pklen;
  constexpr size_t skoff2 = skoff1 + 32;

  auto pke_sk = seckey.template subspan<0, skoff0>();
  auto pubkey = seckey.template subspan<skoff0, skoff1 - skoff0>();
  auto h = seckey.template subspan<skoff1, skoff2 - skoff1>();
  auto z = seckey.template subspan<skoff2, seckey.size() - skoff2>();

  std::array<uint8_t, 32 + h.size()> g_in{};
  std::array<uint8_t, shared_secret.size() + 32> g_out{};
  std::array<uint8_t, shared_secret.size()> j_out{};
  std::array<uint8_t, cipher.size()> c_prime{};

  auto g_in_span = std::span(g_in);
  auto g_in_span0 = g_in_span.template first<32>();
  auto g_in_span1 = g_in_span.template last<h.size()>();

  auto g_out_span = std::span(g_out);
  auto g_out_span0 = g_out_span.template first<shared_secret.size()>();
  auto g_out_span1 = g_out_span.template last<32>();

  k_pke::decrypt<k, du, dv>(pke_sk, cipher, g_in_span0);
  std::copy(h.begin(), h.end(), g_in_span1.begin());

  sha3_512::sha3_512_t h512{};
  h512.absorb(g_in_span);
  h512.finalize();
  h512.digest(g_out_span);

  shake256::shake256_t xof256{};
  xof256.absorb(z);
  xof256.absorb(cipher);
  xof256.finalize();
  xof256.squeeze(j_out);

  // Explicitly ignore return value, because public key, held as part of secret key is *assumed* to be valid.
  (void)k_pke::encrypt<k, eta1, eta2, du, dv>(pubkey, g_in_span0, g_out_span1, c_prime);

  // line 9-12 of algorithm 17, in constant-time
  using kdf_t = std::span<const uint8_t, shared_secret.size()>;
  const uint32_t cond = ml_kem_utils::ct_memcmp(cipher, std::span<const uint8_t, ctlen>(c_prime));
  ml_kem_utils::ct_cond_memcpy(cond, shared_secret, kdf_t(g_out_span0), kdf_t(z));
}

}

namespace ml_kem_512 {

// ML-KEM Key Encapsulation Mechanism instantiated with ML-KEM-512 parameters
// See row 1 of table 2 of ML-KEM specification @ https://doi.org/10.6028/NIST.FIPS.203.

inline constexpr size_t k = 2;
inline constexpr size_t η1 = 3;
inline constexpr size_t η2 = 2;
inline constexpr size_t du = 10;
inline constexpr size_t dv = 4;

// 32 -bytes seed `d`, used in underlying K-PKE key generation
inline constexpr size_t SEED_D_BYTE_LEN = 32;

// 32 -bytes seed `z`, used in ML-KEM key generation
inline constexpr size_t SEED_Z_BYTE_LEN = 32;

// 800 -bytes ML-KEM-512 public key
inline constexpr size_t PKEY_BYTE_LEN = ml_kem_utils::get_kem_public_key_len(k);

// 1632 -bytes ML-KEM-512 secret key
inline constexpr size_t SKEY_BYTE_LEN = ml_kem_utils::get_kem_secret_key_len(k);

// 32 -bytes seed `m`, used in ML-KEM encapsulation
inline constexpr size_t SEED_M_BYTE_LEN = 32;

// 768 -bytes ML-KEM-512 cipher text
inline constexpr size_t CIPHER_TEXT_BYTE_LEN = ml_kem_utils::get_kem_cipher_text_len(k, du, dv);

// 32 -bytes ML-KEM-512 shared secret
inline constexpr size_t SHARED_SECRET_BYTE_LEN = 32;

// Computes a new ML-KEM-512 keypair, given seed `d` and `z`.
constexpr void
keygen(std::span<const uint8_t, SEED_D_BYTE_LEN> d,
       std::span<const uint8_t, SEED_Z_BYTE_LEN> z,
       std::span<uint8_t, PKEY_BYTE_LEN> pubkey,
       std::span<uint8_t, SKEY_BYTE_LEN> seckey)
{
  ml_kem::keygen<k, η1>(d, z, pubkey, seckey);
}

// Given seed `m` and a ML-KEM-512 public key, this routine computes a ML-KEM-512 cipher text and a fixed size shared secret.
// If, input ML-KEM-512 public key is malformed, encapsulation will fail, returning false.
[[nodiscard("If public key is malformed, encapsulation fails")]] constexpr bool
encapsulate(std::span<const uint8_t, SEED_M_BYTE_LEN> m,
            std::span<const uint8_t, PKEY_BYTE_LEN> pubkey,
            std::span<uint8_t, CIPHER_TEXT_BYTE_LEN> cipher,
            std::span<uint8_t, SHARED_SECRET_BYTE_LEN> shared_secret)
{
  return ml_kem::encapsulate<k, η1, η2, du, dv>(m, pubkey, cipher, shared_secret);
}

// Given a ML-KEM-512 secret key and a cipher text, this routine computes a fixed size shared secret.
constexpr void
decapsulate(std::span<const uint8_t, SKEY_BYTE_LEN> seckey, std::span<const uint8_t, CIPHER_TEXT_BYTE_LEN> cipher, std::span<uint8_t, SHARED_SECRET_BYTE_LEN> shared_secret)
{
  ml_kem::decapsulate<k, η1, η2, du, dv>(seckey, cipher, shared_secret);
}

}
