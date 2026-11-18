/*

MIT License

Copyright (c) 2026 Christopher J. Cole (ef1500)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

WIFEHASH128x implementation. AVX2, AArch64 NEON and Regular paths.
The fastest member emergent from the Meihash family.
*/

#ifndef WIFEHASH_H
#define WIFEHASH_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if defined(__AVX2__)
  #include <immintrin.h>
#endif

// AArch64 NEON. Little-endian only; big-endian AArch64 falls back to scalar.
#if !defined(WHX_NO_NEON) && !defined(__ARM_BIG_ENDIAN) &&                    \
    ((defined(__aarch64__) && defined(__ARM_NEON)) ||                          \
     defined(_M_ARM64) || defined(_M_ARM64EC))
  #define WHX_NEON 1
  #include <arm_neon.h>
#endif

#if defined(_MSC_VER) && !defined(__clang__)
  #define WHX_INLINE   static __forceinline
  #define WHX_NOINLINE __declspec(noinline)
  #define WHX_ALIGN(n) __declspec(align(n))
#elif defined(__GNUC__) || defined(__clang__)
  #define WHX_INLINE   static inline __attribute__((always_inline, unused))
  #define WHX_NOINLINE __attribute__((noinline))
  #define WHX_ALIGN(n) __attribute__((aligned(n)))
#else
  #define WHX_INLINE   static
  #define WHX_NOINLINE
  #define WHX_ALIGN(n)
#endif

#if defined(__GNUC__) || defined(__clang__)
  #define WHX_FN static __attribute__((unused))
#else
  #define WHX_FN static
#endif

#ifndef WHX_BULK_MIN
  #define WHX_BULK_MIN 192
#endif

// Constants
#define WHX_P0 UINT64_C(0x59757a7541696861) // YuzuAiha
#define WHX_P1 UINT64_C(0x4d65694169686172) // MeiAiha
#define WHX_S0 UINT64_C(0x4d495a5553415741) // MIZUSAWA
#define WHX_S1 UINT64_C(0x494c4f5645594f55) // ILOVEYOU

#define WHX_GOLD UINT64_C(0x9E3779B97F4A7C15) // Constant.

// Key table generation. 
#define WHX_X1_(z_) ((z_) ^ ((z_) >> 30))
#define WHX_X2_(z_) ((z_) ^ ((z_) >> 27))
#define WHX_X3_(z_) ((z_) ^ ((z_) >> 31))
#define WHX_KDF(i_) \
    WHX_X3_(WHX_X2_(WHX_X1_((uint64_t)(WHX_P0 + (uint64_t)(i_) * WHX_GOLD)) \
            * WHX_S1) * WHX_P0)

#define WHX_KROW_(s_) { WHX_KDF(8*(s_)+0), WHX_KDF(8*(s_)+1), \
                        WHX_KDF(8*(s_)+2), WHX_KDF(8*(s_)+3), \
                        WHX_KDF(8*(s_)+4), WHX_KDF(8*(s_)+5), \
                        WHX_KDF(8*(s_)+6), WHX_KDF(8*(s_)+7) }

// stripe16
WHX_ALIGN(64) static const uint64_t WHX_KT[16][8] = {
    WHX_KROW_(0),  WHX_KROW_(1),  WHX_KROW_(2),  WHX_KROW_(3),
    WHX_KROW_(4),  WHX_KROW_(5),  WHX_KROW_(6),  WHX_KROW_(7),
    WHX_KROW_(8),  WHX_KROW_(9),  WHX_KROW_(10), WHX_KROW_(11),
    WHX_KROW_(12), WHX_KROW_(13), WHX_KROW_(14), WHX_KROW_(15)
};

// Blockseed
WHX_ALIGN(32) static const uint64_t WHX_SC[8] = {
    WHX_KDF(0x100), WHX_KDF(0x101), WHX_KDF(0x102), WHX_KDF(0x103),
    WHX_KDF(0x104), WHX_KDF(0x105), WHX_KDF(0x106), WHX_KDF(0x107)
};

// Accseeds
WHX_ALIGN(32) static const uint64_t WHX_ODD[8] = {
    WHX_KDF(0x200)|1u, WHX_KDF(0x201)|1u, WHX_KDF(0x202)|1u, WHX_KDF(0x203)|1u,
    WHX_KDF(0x204)|1u, WHX_KDF(0x205)|1u, WHX_KDF(0x206)|1u, WHX_KDF(0x207)|1u
};

#define WHX_SCRMUL UINT64_C(0x57494645) // "WIFE" (Ironic)

WHX_INLINE uint64_t whx_ld64(const void *p_)
{
    uint64_t v_; memcpy(&v_, p_, 8); return v_;
}

WHX_INLINE uint32_t whx_ld32(const void *p_)
{
    uint32_t v_; memcpy(&v_, p_, 4); return v_;
}

WHX_INLINE void whx_st64(void *p_, uint64_t v_) { memcpy(p_, &v_, 8); }

// Meihash's meirotl
WHX_INLINE uint64_t whx_rol64(uint64_t x_, unsigned r_)
{
    return (x_ << (r_ & 63)) | (x_ >> ((64u - r_) & 63));
}

// Multiply
#if defined(__SIZEOF_INT128__) && !defined(WHX_NO_INT128)

WHX_INLINE uint64_t whx_mul128(uint64_t a_, uint64_t b_, uint64_t *hi_)
{
    __uint128_t r_ = (__uint128_t)a_ * (__uint128_t)b_;
    *hi_ = (uint64_t)(r_ >> 64);
    return (uint64_t)r_;
}

#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_ARM64)) && !defined(WHX_NO_INT128)
  #include <intrin.h>
  #pragma intrinsic(_umul128)

WHX_INLINE uint64_t whx_mul128(uint64_t a_, uint64_t b_, uint64_t *hi_)
{
    return _umul128(a_, b_, hi_);
}

#else

WHX_INLINE uint64_t whx_mul128(uint64_t a_, uint64_t b_, uint64_t *hi_)
{
    uint64_t al_ = (uint32_t)a_, ah_ = a_ >> 32;
    uint64_t bl_ = (uint32_t)b_, bh_ = b_ >> 32;
    uint64_t lo_ = al_ * bl_, m1_ = ah_ * bl_, m2_ = al_ * bh_, hh_ = ah_ * bh_;
    uint64_t t_  = (lo_ >> 32) + (uint32_t)m1_ + (uint32_t)m2_;
    *hi_ = hh_ + (m1_ >> 32) + (m2_ >> 32) + (t_ >> 32);
    return (uint64_t)(uint32_t)lo_ | (t_ << 32);
}
#endif

// t0_/t1_ are the words XORed into each 16-byte pair: WHX_P0/WHX_P1 for the
// unseeded hash, per-seed values for the ws_/wsx_ variants.
WHX_INLINE void whx_tail_c(const uint8_t *ptr_, size_t len_,
                           uint64_t a1_, uint64_t a2_,
                           uint64_t s0_, uint64_t s1_, uint64_t p0_,
                           uint64_t t0_, uint64_t t1_, void *out_)
{
    while (len_ >= 16) {
        uint64_t h_, l_ = whx_mul128(whx_ld64(ptr_)     ^ t0_,
                                     whx_ld64(ptr_ + 8) ^ t1_, &h_);
        uint64_t t_;
        a1_ ^= l_; a2_ ^= h_;
        t_ = a1_;
        a1_ = whx_rol64(a1_, 1) + a2_;
        a2_ = whx_rol64(t_,  1) ^ a1_;   // Xor here to pass permutation tests.
        ptr_ += 16; len_ -= 16;
    }

    if (len_) 
    {
        uint64_t m_ = 0, y_ = 0, h_, l_;
        if (len_ >= 8) 
        {
            m_ = whx_ld64(ptr_);
            if (len_ > 8)
                y_ = whx_ld64(ptr_ + len_ - 8) >> (8u * (unsigned)(16 - len_));
        } 
        else if (len_ >= 4) 
        {
            m_ = (uint64_t)whx_ld32(ptr_)
               | (((uint64_t)whx_ld32(ptr_ + len_ - 4)
                   >> (8u * (unsigned)(8 - len_))) << 32);
        } 
        else 
        {
            m_ = ptr_[0];
            if (len_ > 1) m_ |= (uint64_t)ptr_[1] << 8;
            if (len_ > 2) m_ |= (uint64_t)ptr_[2] << 16;
        }
        // Xorstuff here to pass the seedBIC tests.
        l_ = whx_mul128(m_ ^ s0_, y_ ^ s1_, &h_);
        a1_ ^= l_; a2_ ^= h_;
    }

    a1_ ^= len_;

    {   
        // AVALANCHE! WHEEEEEEEEEEE!
        uint64_t h0_, h1_, l0_, l1_, fl_, fh_;
        l0_ = whx_mul128(a1_, s0_, &h0_);
        l1_ = whx_mul128(a2_, p0_, &h1_);
        fl_ = l0_ ^ l1_; fh_ = h0_ ^ h1_;
        a1_ = fl_ ^ fh_;
        a2_ = fh_ + a1_;

        a1_ ^= a1_ >> 33; a1_ *= WHX_P0;
        a1_ ^= a1_ >> 33; a1_ *= WHX_S1;
        a1_ ^= a1_ >> 33;

        a2_ ^= a2_ >> 33; a2_ *= WHX_P1;
        a2_ ^= a2_ >> 33; a2_ *= WHX_S0;
        a2_ ^= a2_ >> 33;

        whx_st64(out_, a1_);
        whx_st64((uint8_t *)out_ + 8, a2_);
    }
}

WHX_INLINE void whx_tail(const uint8_t *ptr_, size_t len_,
                         uint64_t a1_, uint64_t a2_,
                         uint64_t s0_, uint64_t s1_, uint64_t p0_, void *out_)
{
    whx_tail_c(ptr_, len_, a1_, a2_, s0_, s1_, p0_, WHX_P0, WHX_P1, out_);
}

// c0..c3 stand in for P0, P1, S0, S1 wherever they are XORed into input.
// The unseeded hash passes the macro constants; ws_/wsx_ pass per-seed values.
WHX_INLINE void whx_small_c(const void *input, size_t len, uint64_t seed, void *out,
                            uint64_t c0, uint64_t c1, uint64_t c2, uint64_t c3)
{
    //Bee nice to the stack! :)
    uint64_t h, l, t; // Define here instead to bee nice to the stack.
    const uint8_t *ptr = (const uint8_t *)input;
    uint64_t p0 = c0 ^ seed;
    uint64_t s0 = c2 ^ whx_rol64(seed, 32);
    uint64_t s1 = c3;
    uint64_t a1 = WHX_P1, a2 = WHX_P0;
    uint64_t l1l = 0, l1h = 0, l2l = 0, l2h = 0;
    uint64_t l3l = 0, l3h = 0, l4l = 0, l4h = 0;
    // uint64_t h, l, t;

    while (len >= 64)
    {
        l = whx_mul128(whx_ld64(ptr     ) ^ c2,
                       whx_ld64(ptr +  8) ^ c1, &h); l1l ^= l; l1h ^= h;
        l = whx_mul128(whx_ld64(ptr + 16) ^ c0,
                       whx_ld64(ptr + 24) ^ c3, &h); l2l ^= l; l2h ^= h;
        l = whx_mul128(whx_ld64(ptr + 32) ^ c1,
                       whx_ld64(ptr + 40) ^ c2, &h); l3l ^= l; l3h ^= h;
        l = whx_mul128(whx_ld64(ptr + 48) ^ c3,
                       whx_ld64(ptr + 56) ^ c0, &h); l4l ^= l; l4h ^= h;

        a1 ^= l1l; a2 ^= l1h;
        a1 ^= whx_rol64(a1,1); a2 ^= whx_rol64(a2,1); a1 ^= l2l; a2 ^= l2h;
        a1 ^= whx_rol64(a1,1); a2 ^= whx_rol64(a2,1); a1 ^= l3l; a2 ^= l3h;
        a1 ^= whx_rol64(a1,1); a2 ^= whx_rol64(a2,1); a1 ^= l4l; a2 ^= l4h;

        t  = a1;
        a1 = whx_rol64(a1,1) + a2;
        a2 = whx_rol64(t, 1) + a1;
        ptr += 64; len -= 64;
    }

    whx_tail_c(ptr, len, a1, a2, s0, s1, p0, c0, c1, out);
}

WHX_INLINE void whx_small(const void *input, size_t len,
                          uint64_t seed, void *out)
{
    whx_small_c(input, len, seed, out, WHX_P0, WHX_P1, WHX_S0, WHX_S1);
}

// wifehash baby version
WHX_FN void wifehash128x_small(const void *input, size_t len,
                               uint64_t seed, void *out)
{
    whx_small(input, len, seed, out);
}

// Laneinit, laneseed, lanemix
// len is 64-bit so a stream longer than 4 GiB hashes the same on 32-bit targets.
WHX_INLINE void whx_init(uint64_t acc[8], uint64_t seed, uint64_t len)
{
    int j = 0; // bee nice to stack!
    uint64_t z = (seed ^ WHX_S0) * WHX_P0 + len * WHX_GOLD;
    z ^= z >> 31;
    for (j = 0; j < 8; j++)
        acc[j] = (WHX_KT[0][j] + z) * WHX_ODD[j] ^ whx_rol64(seed, 8u*(unsigned)j + 3u);
}

// Lanefold
WHX_INLINE void whx_merge_c(const uint64_t acc[8], const uint64_t sc[8],
                            uint64_t *pa1, uint64_t *pa2)
{
    uint64_t a1 = WHX_P1, a2 = WHX_P0;
    int j = 0;
    for (; j < 4; j++) {
        uint64_t h, t, l = whx_mul128(acc[j] ^ sc[j],
                                      acc[j + 4] ^ sc[j + 4], &h);
        a1 ^= l; a2 ^= h;
        t  = a1;
        a1 = whx_rol64(a1, 1) + a2;
        a2 = whx_rol64(t,  1) ^ a1;
    }
    *pa1 = a1; *pa2 = a2;
}

WHX_INLINE void whx_merge(const uint64_t acc[8], uint64_t *pa1, uint64_t *pa2)
{
    whx_merge_c(acc, WHX_SC, pa1, pa2);
}

// Scalar
WHX_INLINE void whx_stripe_ref(uint64_t acc[8], const uint8_t *p,
                               const uint64_t *key)
{
    int j = 0; // Bee nice to the stack.
    for (; j < 4; j++) 
    {
        uint64_t d0 = whx_ld64(p + 8*j);
        uint64_t d1 = whx_ld64(p + 32 + 8*j);
        uint64_t x0 = d0 ^ key[j];
        uint64_t x1 = d1 ^ key[j + 4];
        acc[j] += d1 + (uint64_t)(uint32_t)x0 * (uint32_t)(x0 >> 32);
        acc[j + 4] += d0 + (uint64_t)(uint32_t)x1 * (uint32_t)(x1 >> 32);
    }
}

WHX_INLINE void whx_scramble_ref(uint64_t acc[8])
{
    int j = 0;
    for (; j < 8; j++)
        acc[j] = (acc[j] ^ (acc[j] >> 47) ^ WHX_SC[j]) * WHX_SCRMUL;
}

// Bulk hash
WHX_FN WHX_NOINLINE
void whx_bulk_ref(const void *input, size_t len, uint64_t seed, void *out)
{
    const uint8_t *ptr = (const uint8_t *)input;
    uint64_t acc[8], a1, a2, p0, s0;
    size_t nblk, nstr, b, s; // bee nice to stack.

    whx_init(acc, seed, len);
    nblk = len >> 10;
    nstr = (len & 1023u) >> 6;

    for (b = 0; b < nblk; b++) 
    {
        for (s = 0; s < 16; s++) whx_stripe_ref(acc, ptr + 64*s, WHX_KT[s]);
        
        ptr += 1024;
        whx_scramble_ref(acc);
    }

    for (s = 0; s < nstr; s++) whx_stripe_ref(acc, ptr + 64*s, WHX_KT[s]);
    ptr += 64 * nstr;

    whx_merge(acc, &a1, &a2);
    p0 = WHX_P0 ^ seed;
    s0 = WHX_S0 ^ whx_rol64(seed, 32);
    whx_tail(ptr, len & 63u, a1, a2, s0, WHX_S1, p0, out);
}

WHX_FN void wifehash128x_ref(const void *input, size_t len,
                             uint64_t seed, void *out)
{
    if (len < WHX_BULK_MIN) whx_small(input, len, seed, out);
    else whx_bulk_ref(input, len, seed, out);
}

// Seeded (with-state) bulk path, scalar. Same structure as whx_bulk_ref, but:
//  - keys come from a per-seed table of nkb 1 KiB blocks; 1 KiB block b uses
//    key block (b mod nkb), and the trailing stripes use the next key block;
//  - the scramble/fold constants (sc) and the tail XOR constants (c) are per-seed.
WHX_INLINE void whx_scramble_c(uint64_t acc[8], const uint64_t *sc)
{
    int j = 0;
    for (; j < 8; j++)
        acc[j] = (acc[j] ^ (acc[j] >> 47) ^ sc[j]) * WHX_SCRMUL;
}

WHX_FN WHX_NOINLINE
void whx_ws_bulk_ref(const void *input, size_t len, uint64_t seed, void *out,
                     const uint64_t *kt, size_t nkb,
                     const uint64_t *sc, const uint64_t *c)
{
    const uint8_t *ptr = (const uint8_t *)input;
    const uint64_t *kb = kt, *kend = kt + 128 * nkb;
    uint64_t acc[8], a1, a2;
    size_t nblk, nstr, b, s; // bee nice to stack.

    whx_init(acc, seed, len);
    nblk = len >> 10;
    nstr = (len & 1023u) >> 6;

    for (b = 0; b < nblk; b++)
    {
        for (s = 0; s < 16; s++) whx_stripe_ref(acc, ptr + 64*s, kb + 8*s);
        ptr += 1024;
        whx_scramble_c(acc, sc);
        kb += 128;
        if (kb == kend) kb = kt;
    }

    for (s = 0; s < nstr; s++) whx_stripe_ref(acc, ptr + 64*s, kb + 8*s);
    ptr += 64 * nstr;

    whx_merge_c(acc, sc, &a1, &a2);
    whx_tail_c(ptr, len & 63u, a1, a2, c[2] ^ whx_rol64(seed, 32), c[3],
               c[0] ^ seed, c[0], c[1], out);
}

#define WHX_STRIPES16_(M_) \
    M_(0)  M_(1)  M_(2)  M_(3)  M_(4)  M_(5)  M_(6)  M_(7) \
    M_(8)  M_(9)  M_(10) M_(11) M_(12) M_(13) M_(14) M_(15)

// AVX2 Hasher
#if defined(__AVX2__)

#if defined(__GNUC__)
  // Register trick used by nyahash and friends.
  #define WHX_PIN_(a_, b_) __asm__("" : "+x"(a_), "+x"(b_))
#else
  #define WHX_PIN_(a_, b_) ((void)0)
#endif

// Uses a modded xxhmul with meihash-style mixer.
// The data is pinned to registers after the load: otherwise gcc folds d0/d1 into both the
// xor and the add, loading each twice (twice the cache-line splits on unaligned input).
#define WHX_STRIPE256_(va_, vb_, p_, key_) do {                              \
    __m256i d0_ = _mm256_loadu_si256((const __m256i *)(p_));                 \
    __m256i d1_ = _mm256_loadu_si256((const __m256i *)((p_) + 32));          \
    WHX_PIN_(d0_, d1_);                                                      \
    __m256i x0_ = _mm256_xor_si256(d0_,                                      \
                      _mm256_load_si256((const __m256i *)(key_)));           \
    __m256i x1_ = _mm256_xor_si256(d1_,                                      \
                      _mm256_load_si256((const __m256i *)((key_) + 4)));     \
    __m256i q0_ = _mm256_mul_epu32(x0_, _mm256_srli_epi64(x0_, 32));         \
    __m256i q1_ = _mm256_mul_epu32(x1_, _mm256_srli_epi64(x1_, 32));         \
    (va_) = _mm256_add_epi64(va_, _mm256_add_epi64(q0_, d1_));               \
    (vb_) = _mm256_add_epi64(vb_, _mm256_add_epi64(q1_, d0_));               \
} while (0)

WHX_INLINE __m256i whx_scr256(__m256i a, __m256i sc)
{
    __m256i m = _mm256_set1_epi64x((long long)WHX_SCRMUL);
    __m256i lo, hi;
    a  = _mm256_xor_si256(_mm256_xor_si256(a, _mm256_srli_epi64(a, 47)), sc);
    lo = _mm256_mul_epu32(a, m);
    hi = _mm256_mul_epu32(_mm256_srli_epi64(a, 32), m);
    return _mm256_add_epi64(lo, _mm256_slli_epi64(hi, 32));
}

WHX_FN WHX_NOINLINE
void whx_bulk_avx2(const void *input, size_t len, uint64_t seed, void *out)
{
    const uint8_t *ptr = (const uint8_t *)input;
    WHX_ALIGN(32) uint64_t acc[8];
    uint64_t a1, a2, p0, s0;
    __m256i va, vb, sca, scb;
    size_t nblk, nstr, s;

    whx_init(acc, seed, len);
    va  = _mm256_load_si256((const __m256i *)acc);
    vb  = _mm256_load_si256((const __m256i *)(acc + 4));
    sca = _mm256_load_si256((const __m256i *)WHX_SC);
    scb = _mm256_load_si256((const __m256i *)(WHX_SC + 4));

    nblk = len >> 10;
    nstr = (len & 1023u) >> 6;

    while (nblk--) {
        #define WHX_DO_(s_) WHX_STRIPE256_(va, vb, ptr + 64*(s_), WHX_KT[s_]); \
                            WHX_PIN_(va, vb);
        WHX_STRIPES16_(WHX_DO_)
        #undef WHX_DO_
        ptr += 1024;
        va = whx_scr256(va, sca);
        vb = whx_scr256(vb, scb);
    }
    for (s = 0; s < nstr; s++) {
        WHX_STRIPE256_(va, vb, ptr + 64*s, WHX_KT[s]);
        WHX_PIN_(va, vb);
    }
    ptr += 64 * nstr;

    _mm256_store_si256((__m256i *)acc, va);
    _mm256_store_si256((__m256i *)(acc + 4), vb);

    whx_merge(acc, &a1, &a2);
    p0 = WHX_P0 ^ seed;
    s0 = WHX_S0 ^ whx_rol64(seed, 32);
    whx_tail(ptr, len & 63u, a1, a2, s0, WHX_S1, p0, out);
}

WHX_FN void wifehash128x_avx2(const void *input, size_t len,
                              uint64_t seed, void *out)
{
    if (len < WHX_BULK_MIN) whx_small(input, len, seed, out);
    else                    whx_bulk_avx2(input, len, seed, out);
}

// Seeded (with-state) AVX2 bulk path. Same stripe as WHX_STRIPE256_, except the
// keys are loaded unaligned: a state from malloc is not guaranteed 32-byte aligned.
#define WHX_STRIPE256U_(va_, vb_, p_, key_) do {                             \
    __m256i d0_ = _mm256_loadu_si256((const __m256i *)(p_));                 \
    __m256i d1_ = _mm256_loadu_si256((const __m256i *)((p_) + 32));          \
    WHX_PIN_(d0_, d1_);                                                      \
    __m256i x0_ = _mm256_xor_si256(d0_,                                      \
                      _mm256_loadu_si256((const __m256i *)(key_)));          \
    __m256i x1_ = _mm256_xor_si256(d1_,                                      \
                      _mm256_loadu_si256((const __m256i *)((key_) + 4)));    \
    __m256i q0_ = _mm256_mul_epu32(x0_, _mm256_srli_epi64(x0_, 32));         \
    __m256i q1_ = _mm256_mul_epu32(x1_, _mm256_srli_epi64(x1_, 32));         \
    (va_) = _mm256_add_epi64(va_, _mm256_add_epi64(q0_, d1_));               \
    (vb_) = _mm256_add_epi64(vb_, _mm256_add_epi64(q1_, d0_));               \
} while (0)

WHX_FN WHX_NOINLINE
void whx_ws_bulk_avx2(const void *input, size_t len, uint64_t seed, void *out,
                      const uint64_t *kt, size_t nkb,
                      const uint64_t *sc, const uint64_t *c)
{
    const uint8_t *ptr = (const uint8_t *)input;
    const uint64_t *kb = kt, *kend = kt + 128 * nkb;
    WHX_ALIGN(32) uint64_t acc[8];
    uint64_t a1, a2;
    __m256i va, vb, sca, scb;
    size_t nblk, nstr, s;

    whx_init(acc, seed, len);
    va  = _mm256_load_si256((const __m256i *)acc);
    vb  = _mm256_load_si256((const __m256i *)(acc + 4));
    sca = _mm256_loadu_si256((const __m256i *)sc);
    scb = _mm256_loadu_si256((const __m256i *)(sc + 4));

    nblk = len >> 10;
    nstr = (len & 1023u) >> 6;

    while (nblk--) {
        #define WHX_DO_(s_) WHX_STRIPE256U_(va, vb, ptr + 64*(s_), kb + 8*(s_)); \
                            WHX_PIN_(va, vb);
        WHX_STRIPES16_(WHX_DO_)
        #undef WHX_DO_
        ptr += 1024;
        va = whx_scr256(va, sca);
        vb = whx_scr256(vb, scb);
        kb += 128;
        if (kb == kend) kb = kt;
    }
    for (s = 0; s < nstr; s++) {
        WHX_STRIPE256U_(va, vb, ptr + 64*s, kb + 8*s);
        WHX_PIN_(va, vb);
    }
    ptr += 64 * nstr;

    _mm256_store_si256((__m256i *)acc, va);
    _mm256_store_si256((__m256i *)(acc + 4), vb);

    whx_merge_c(acc, sc, &a1, &a2);
    whx_tail_c(ptr, len & 63u, a1, a2, c[2] ^ whx_rol64(seed, 32), c[3],
               c[0] ^ seed, c[0], c[1], out);
}

#endif // __AVX2__

// NEON Hasher
#if defined(WHX_NEON)

// One 64-byte stripe over four 2x64 accumulators (lanes 0-1, 2-3, 4-5, 6-7):
//   x = d ^ key
//   acc[0..3] += d1 + lo(x0) * hi(x0)
//   acc[4..7] += d0 + lo(x1) * hi(x1)
// NEON has no even*odd 32-bit multiply like mul_epu32, so uzp1/uzp2 over
// two xored vectors pull the lo and hi words of four lanes into two
// registers. umlal/umlal2 then multiply them directly, one uzp per vector.
// The product goes into the d_other vector, not the accumulator, so the
// accumulator dependency chain is one add per stripe.
// The cross-half add (d1 into the low lanes, d0 into the high) lines up
// with the natural register order, so no swap is needed.
#define WHX_STRIPE128_(a0_, a1_, a2_, a3_, p_, k_) do {                       \
    uint64x2_t d0a_ = vld1q_u64((const uint64_t *)(p_));                      \
    uint64x2_t d0b_ = vld1q_u64((const uint64_t *)((p_) + 16));               \
    uint64x2_t d1a_ = vld1q_u64((const uint64_t *)((p_) + 32));               \
    uint64x2_t d1b_ = vld1q_u64((const uint64_t *)((p_) + 48));               \
    uint32x4_t x0a_ = vreinterpretq_u32_u64(veorq_u64(d0a_, vld1q_u64((k_))));     \
    uint32x4_t x0b_ = vreinterpretq_u32_u64(veorq_u64(d0b_, vld1q_u64((k_) + 2))); \
    uint32x4_t x1a_ = vreinterpretq_u32_u64(veorq_u64(d1a_, vld1q_u64((k_) + 4))); \
    uint32x4_t x1b_ = vreinterpretq_u32_u64(veorq_u64(d1b_, vld1q_u64((k_) + 6))); \
    uint32x4_t l0_ = vuzp1q_u32(x0a_, x0b_), h0_ = vuzp2q_u32(x0a_, x0b_);    \
    uint32x4_t l1_ = vuzp1q_u32(x1a_, x1b_), h1_ = vuzp2q_u32(x1a_, x1b_);    \
    (a0_) = vaddq_u64(a0_, vmlal_u32(d1a_, vget_low_u32(l0_), vget_low_u32(h0_))); \
    (a1_) = vaddq_u64(a1_, vmlal_high_u32(d1b_, l0_, h0_));                   \
    (a2_) = vaddq_u64(a2_, vmlal_u32(d0a_, vget_low_u32(l1_), vget_low_u32(h1_))); \
    (a3_) = vaddq_u64(a3_, vmlal_high_u32(d0b_, l1_, h1_));                   \
} while (0)

// (a ^ a>>47 ^ sc) * WIFE, lane-wise, mod 2^64.
// With a 32-bit multiplier: a*m = lo(a)*m + (hi(a)*m << 32). A 32-bit mul
// against {0, m} per lane leaves hi(a)*m in the top word and 0 in the
// bottom, which is the shifted term; umlal adds lo(a)*m on top.
WHX_INLINE uint64x2_t whx_scr128(uint64x2_t a, uint64x2_t sc)
{
    const uint32x4_t mh = vreinterpretq_u32_u64(vdupq_n_u64(WHX_SCRMUL << 32));
    const uint32x2_t ml = vdup_n_u32((uint32_t)WHX_SCRMUL);
#if defined(__ARM_FEATURE_SHA3)
    a = veor3q_u64(a, vshrq_n_u64(a, 47), sc);  // 3-way xor in one op
#else
    a = veorq_u64(veorq_u64(a, vshrq_n_u64(a, 47)), sc);
#endif
    return vmlal_u32(vreinterpretq_u64_u32(vmulq_u32(vreinterpretq_u32_u64(a), mh)),
                     vmovn_u64(a), ml);
}

#if defined(__GNUC__) || defined(__clang__)
  // Per-stripe fence. It makes the pointers and accumulators opaque so the
  // compiler cannot hoist later stripes' loads above this one. Without it
  // GCC runs out of the 32 V registers and spills accumulators to the stack
  // inside the hot loop (about 30% slower on Apple M1). It emits nothing,
  // and the out-of-order core still overlaps stripes.
  #define WHX_NFENCE_(p_, k_, a0_, a1_, a2_, a3_) \
      __asm__("" : "+r"(p_), "+r"(k_), "+w"(a0_), "+w"(a1_), "+w"(a2_), "+w"(a3_))
#else
  #define WHX_NFENCE_(p_, k_, a0_, a1_, a2_, a3_) ((void)0)
#endif

#if defined(__GNUC__) || defined(__clang__)
// One 1 KiB block (16 stripes) as a 3-stage software pipeline in inline asm:
// load stripe s+2, eor/uzp stripe s+1, umlal/add stripe s. Same instructions
// as WHX_STRIPE128_, only scheduled. Narrow cores need this: the Cortex-A76
// has two NEON pipes and only V0 multiplies, and with one stripe in flight it
// stalls at ~12 cycles/stripe (vs. 8 by throughput). GCC can't produce the
// schedule from intrinsics without spilling. ~25% faster on A76, neutral on M1.
// Stripes rotate through three register sets {D0-3 data, K0-3 keys, T}:
// set 0 = v4-v11 + v28, set 1 = v12-v19 + v29, set 2 = v20-v27 + v30.
#define WHX_NSET0_ 4, 5, 6, 7, 8, 9, 10, 11, 28
#define WHX_NSET1_ 12, 13, 14, 15, 16, 17, 18, 19, 29
#define WHX_NSET2_ 20, 21, 22, 23, 24, 25, 26, 27, 30
#define WHX_NLDD_(a, b, o) "ldp q" #a ", q" #b ", [%[p], #" #o "]\n"
#define WHX_NLDK_(a, b, o) "ldp q" #a ", q" #b ", [%[k], #" #o "]\n"
#define WHX_NEOR_(k, d)    "eor v" #k ".16b, v" #d ".16b, v" #k ".16b\n"
#define WHX_NUZ1_(t, a, b) "uzp1 v" #t ".4s, v" #a ".4s, v" #b ".4s\n"
#define WHX_NUZ2_(t, a, b) "uzp2 v" #t ".4s, v" #a ".4s, v" #b ".4s\n"
#define WHX_NMLL_(d, a, b) "umlal v" #d ".2d, v" #a ".2s, v" #b ".2s\n"
#define WHX_NMLH_(d, a, b) "umlal2 v" #d ".2d, v" #a ".4s, v" #b ".4s\n"
#define WHX_NACC_(n, d)    "add %[a" #n "].2d, %[a" #n "].2d, v" #d ".2d\n"
// Stage L: load data and keys of one stripe (byte offsets o, o+32).
#define WHX_NL__(o, o32, d0, d1, d2, d3, k0, k1, k2, k3, t) \
    WHX_NLDD_(d0, d1, o) WHX_NLDD_(d2, d3, o32) WHX_NLDK_(k0, k1, o) WHX_NLDK_(k2, k3, o32)
#define WHX_NL_(...) WHX_NL__(__VA_ARGS__)
// Stage X: x = d ^ k, then lo/hi words: L0 -> T, H0 -> K1, L1 -> K0, H1 -> K3.
#define WHX_NX__(d0, d1, d2, d3, k0, k1, k2, k3, t) \
    WHX_NEOR_(k0, d0) WHX_NEOR_(k1, d1) WHX_NEOR_(k2, d2) WHX_NEOR_(k3, d3) \
    WHX_NUZ1_(t, k0, k1) WHX_NUZ2_(k1, k0, k1) WHX_NUZ1_(k0, k2, k3) WHX_NUZ2_(k3, k2, k3)
#define WHX_NX_(...) WHX_NX__(__VA_ARGS__)
// Stage M: products into the cross-half data, then into the accumulators.
#define WHX_NMUL__(d0, d1, d2, d3, k0, k1, k2, k3, t) \
    WHX_NMLL_(d2, t, k1) WHX_NMLH_(d3, t, k1) WHX_NMLL_(d0, k0, k3) WHX_NMLH_(d1, k0, k3)
#define WHX_NMUL_(...) WHX_NMUL__(__VA_ARGS__)
#define WHX_NADD__(d0, d1, d2, d3, k0, k1, k2, k3, t) \
    WHX_NACC_(0, d2) WHX_NACC_(1, d3) WHX_NACC_(2, d0) WHX_NACC_(3, d1)
#define WHX_NADD_(...) WHX_NADD__(__VA_ARGS__)
// Stage L of one stripe interleaved with stage X of another.
#define WHX_NLX__(o, o32, d0, d1, d2, d3, k0, k1, k2, k3, t, \
                  e0, e1, e2, e3, j0, j1, j2, j3, u) \
    WHX_NLDD_(d0, d1, o)  WHX_NEOR_(j0, e0) WHX_NLDD_(d2, d3, o32) WHX_NEOR_(j1, e1) \
    WHX_NLDK_(k0, k1, o)  WHX_NEOR_(j2, e2) WHX_NLDK_(k2, k3, o32) WHX_NEOR_(j3, e3) \
    WHX_NUZ1_(u, j0, j1) WHX_NUZ2_(j1, j0, j1) WHX_NUZ1_(j0, j2, j3) WHX_NUZ2_(j3, j2, j3)
#define WHX_NLX_(...) WHX_NLX__(__VA_ARGS__)
// Step s: M(s) multiplies, L(s+2) with X(s+1), M(s) adds.
#define WHX_NSTEP_(o, o32, SM, SL, SX) \
    WHX_NMUL_(SM) WHX_NLX_(o, o32, SL, SX) WHX_NADD_(SM)
#define WHX_NBLOCK_(p_, k_, a0_, a1_, a2_, a3_) __asm__ volatile(          \
    WHX_NL_(0, 32, WHX_NSET0_) WHX_NL_(64, 96, WHX_NSET1_) WHX_NX_(WHX_NSET0_) \
    WHX_NSTEP_(128, 160, WHX_NSET0_, WHX_NSET2_, WHX_NSET1_)                  \
    WHX_NSTEP_(192, 224, WHX_NSET1_, WHX_NSET0_, WHX_NSET2_)                  \
    WHX_NSTEP_(256, 288, WHX_NSET2_, WHX_NSET1_, WHX_NSET0_)                  \
    WHX_NSTEP_(320, 352, WHX_NSET0_, WHX_NSET2_, WHX_NSET1_)                  \
    WHX_NSTEP_(384, 416, WHX_NSET1_, WHX_NSET0_, WHX_NSET2_)                  \
    WHX_NSTEP_(448, 480, WHX_NSET2_, WHX_NSET1_, WHX_NSET0_)                  \
    WHX_NSTEP_(512, 544, WHX_NSET0_, WHX_NSET2_, WHX_NSET1_)                  \
    WHX_NSTEP_(576, 608, WHX_NSET1_, WHX_NSET0_, WHX_NSET2_)                  \
    WHX_NSTEP_(640, 672, WHX_NSET2_, WHX_NSET1_, WHX_NSET0_)                  \
    WHX_NSTEP_(704, 736, WHX_NSET0_, WHX_NSET2_, WHX_NSET1_)                  \
    WHX_NSTEP_(768, 800, WHX_NSET1_, WHX_NSET0_, WHX_NSET2_)                  \
    WHX_NSTEP_(832, 864, WHX_NSET2_, WHX_NSET1_, WHX_NSET0_)                  \
    WHX_NSTEP_(896, 928, WHX_NSET0_, WHX_NSET2_, WHX_NSET1_)                  \
    WHX_NSTEP_(960, 992, WHX_NSET1_, WHX_NSET0_, WHX_NSET2_)                  \
    WHX_NMUL_(WHX_NSET2_) WHX_NX_(WHX_NSET0_) WHX_NADD_(WHX_NSET2_)           \
    WHX_NMUL_(WHX_NSET0_) WHX_NADD_(WHX_NSET0_)                               \
    : [a0] "+w"(a0_), [a1] "+w"(a1_), [a2] "+w"(a2_), [a3] "+w"(a3_)          \
    : [p] "r"(p_), [k] "r"(k_)                                                \
    : "memory", "v4", "v5", "v6", "v7", "v8", "v9", "v10", "v11", "v12",     \
      "v13", "v14", "v15", "v16", "v17", "v18", "v19", "v20", "v21", "v22",   \
      "v23", "v24", "v25", "v26", "v27", "v28", "v29", "v30")

// Hybrid NEON + scalar block, used when the input is 4-byte aligned (and at any
// alignment on wide cores, see WHX_NEON_WIDE). Same arithmetic per lane, split
// across units:
//   scalar lanes 0,1:  products (eor, lsr #32, umaddl -> g0/g1) and the cross-half
//                      data of lanes 4,5 (s0/s1). Scalar loads take no misalignment
//                      penalty, and the integer pipes are otherwise idle.
//   NEON lanes 2-7:    data d23/d45/d67 only. Lanes 4-7 via uzp(x45, x67), lanes 2,3
//                      via uzp of x23 across the two stripes of a pair (one uzp1/uzp2
//                      per two stripes). Lane 4,5 products go straight into acc2.
// Per stripe: 12 NEON ops (3 umlal), 8 scalar, 8 loads. Processed as 8 stripe pairs,
// loading pair q+1 while computing pair q. Measured vs the all-NEON block (bytes/cycle,
// 256 KiB): Cortex-A76 7.61 vs 6.54 (16-byte aligned), 7.00 vs 6.45 (4-byte aligned);
// Apple M1 15.4 vs 14.7 at every alignment. When the input is not 4-byte aligned the
// A76 splits its q loads and the all-NEON block is faster, so it is used there.
#ifndef WHX_NEON_WIDE
  #if defined(__APPLE__) || defined(__ARM_FEATURE_SHA3)
    #define WHX_NEON_WIDE 1
  #else
    #define WHX_NEON_WIDE 0
  #endif
#endif
#define WHX_HLDQ_(b, v, o)    "ldr q" #v ", [%[" #b "], #" #o "]\n"
#define WHX_HLDX_(b, x, y, o) "ldp x" #x ", x" #y ", [%[" #b "], #" #o "]\n"
#define WHX_HMLA_(n, a, b)    "umlal %[a" #n "].2d, v" #a ".2s, v" #b ".2s\n"
#define WHX_HADD_(s, x)       "add %[" #s "], %[" #s "], x" #x "\n"
#define WHX_HEOR_(x, y)       "eor x" #x ", x" #x ", x" #y "\n"
#define WHX_HLSR_(y, x)       "lsr x" #y ", x" #x ", #32\n"
#define WHX_HMAD_(g, x, y)    "umaddl %[" #g "], w" #x ", w" #y ", %[" #g "]\n"
// Register sets {d23 d45 d67 k23 k45 k67 (v), d0 d1 k0 k1 (x)} for the two stripes
// of an even pair and of an odd pair.
#define WHX_HSETE_ 4, 5, 6, 7, 8, 9, 2, 3, 4, 5, 10, 11, 12, 13, 14, 15, 6, 7, 8, 9
#define WHX_HSETO_ 16, 17, 18, 19, 20, 21, 10, 11, 12, 13, \
                   22, 23, 24, 25, 26, 27, 14, 15, 16, 17
// Generated schedule (tools/gen_neon_hybrid.py): load pair, compute pair while
// loading the next, compute the last pair.
#define WHX_HLOAD__(o16a, o32a, o48a, soa, o16b, o32b, o48b, sob, sp, sk, d23a, d45a, d67a, k23a, k45a, k67a, x0a, x1a, y0a, y1a, d23b, d45b, d67b, k23b, k45b, k67b, x0b, x1b, y0b, y1b) \
    WHX_HLDQ_(p, d23a, o16a) WHX_HLDQ_(p, d45a, o32a) WHX_HLDQ_(p, d67a, o48a) \
    WHX_HLDQ_(k, k23a, o16a) WHX_HLDQ_(k, k45a, o32a) WHX_HLDQ_(k, k67a, o48a) \
    WHX_HLDX_(sp, x0a, x1a, soa) WHX_HLDX_(sk, y0a, y1a, soa) WHX_HLDQ_(p, d23b, o16b) \
    WHX_HLDQ_(p, d45b, o32b) WHX_HLDQ_(p, d67b, o48b) WHX_HLDQ_(k, k23b, o16b) \
    WHX_HLDQ_(k, k45b, o32b) WHX_HLDQ_(k, k67b, o48b) WHX_HLDX_(sp, x0b, x1b, sob) \
    WHX_HLDX_(sk, y0b, y1b, sob)
#define WHX_HSTEP__(oL16a, oL32a, oL48a, soLa, oL16b, oL32b, oL48b, soLb, sp, sk, d23a, d45a, d67a, k23a, k45a, k67a, x0a, x1a, y0a, y1a, d23b, d45b, d67b, k23b, k45b, k67b, x0b, x1b, y0b, y1b, Ld23a, Ld45a, Ld67a, Lk23a, Lk45a, Lk67a, Lx0a, Lx1a, Ly0a, Ly1a, Ld23b, Ld45b, Ld67b, Lk23b, Lk45b, Lk67b, Lx0b, Lx1b, Ly0b, Ly1b) \
    WHX_NEOR_(k23a, d23a) WHX_HADD_(s0, x0a) WHX_HLDQ_(p, Ld23a, oL16a) \
    WHX_NEOR_(k45a, d45a) WHX_HADD_(s1, x1a) WHX_HLDQ_(p, Ld45a, oL32a) \
    WHX_NEOR_(k67a, d67a) WHX_NACC_(0, d45a) WHX_HEOR_(x0a, y0a) WHX_HLDQ_(p, Ld67a, oL48a) \
    WHX_NEOR_(k23b, d23b) WHX_HEOR_(x1a, y1a) WHX_HLDQ_(k, Lk23a, oL16a) \
    WHX_NEOR_(k45b, d45b) WHX_NEOR_(k67b, d67b) WHX_HLSR_(y0a, x0a) \
    WHX_HLDQ_(k, Lk45a, oL32a) WHX_NACC_(0, d45b) WHX_HLSR_(y1a, x1a) \
    WHX_HLDQ_(k, Lk67a, oL48a) WHX_NUZ1_(d45a, k45a, k67a) WHX_NUZ2_(k67a, k45a, k67a) \
    WHX_HMAD_(g0, x0a, y0a) WHX_HLDX_(sp, Lx0a, Lx1a, soLa) WHX_HMLA_(2, d45a, k67a) \
    WHX_HMAD_(g1, x1a, y1a) WHX_HLDX_(sk, Ly0a, Ly1a, soLa) WHX_NMLH_(d23a, d45a, k67a) \
    WHX_NACC_(3, d23a) WHX_HADD_(s0, x0b) WHX_HLDQ_(p, Ld23b, oL16b) \
    WHX_NUZ1_(d45b, k45b, k67b) WHX_HADD_(s1, x1b) WHX_HLDQ_(p, Ld45b, oL32b) \
    WHX_NUZ2_(k67b, k45b, k67b) WHX_HMLA_(2, d45b, k67b) WHX_HEOR_(x0b, y0b) \
    WHX_HLDQ_(p, Ld67b, oL48b) WHX_NMLH_(d23b, d45b, k67b) WHX_HEOR_(x1b, y1b) \
    WHX_HLDQ_(k, Lk23b, oL16b) WHX_NACC_(3, d23b) WHX_NUZ1_(k45a, k23a, k23b) \
    WHX_HLSR_(y0b, x0b) WHX_HLDQ_(k, Lk45b, oL32b) WHX_NUZ2_(k23b, k23a, k23b) \
    WHX_HLSR_(y1b, x1b) WHX_HLDQ_(k, Lk67b, oL48b) WHX_NMLL_(d67a, k45a, k23b) \
    WHX_NMLH_(d67b, k45a, k23b) WHX_HMAD_(g0, x0b, y0b) WHX_HLDX_(sp, Lx0b, Lx1b, soLb) \
    WHX_NACC_(1, d67a) WHX_HMAD_(g1, x1b, y1b) WHX_HLDX_(sk, Ly0b, Ly1b, soLb) \
    WHX_NACC_(1, d67b)
#define WHX_HLAST__(d23a, d45a, d67a, k23a, k45a, k67a, x0a, x1a, y0a, y1a, d23b, d45b, d67b, k23b, k45b, k67b, x0b, x1b, y0b, y1b) \
    WHX_NEOR_(k23a, d23a) WHX_HADD_(s0, x0a) WHX_NEOR_(k45a, d45a) WHX_HADD_(s1, x1a) \
    WHX_NEOR_(k67a, d67a) WHX_NACC_(0, d45a) WHX_HEOR_(x0a, y0a) WHX_NEOR_(k23b, d23b) \
    WHX_HEOR_(x1a, y1a) WHX_NEOR_(k45b, d45b) WHX_NEOR_(k67b, d67b) WHX_HLSR_(y0a, x0a) \
    WHX_NACC_(0, d45b) WHX_HLSR_(y1a, x1a) WHX_NUZ1_(d45a, k45a, k67a) \
    WHX_NUZ2_(k67a, k45a, k67a) WHX_HMAD_(g0, x0a, y0a) WHX_HMLA_(2, d45a, k67a) \
    WHX_HMAD_(g1, x1a, y1a) WHX_NMLH_(d23a, d45a, k67a) WHX_NACC_(3, d23a) \
    WHX_HADD_(s0, x0b) WHX_NUZ1_(d45b, k45b, k67b) WHX_HADD_(s1, x1b) \
    WHX_NUZ2_(k67b, k45b, k67b) WHX_HMLA_(2, d45b, k67b) WHX_HEOR_(x0b, y0b) \
    WHX_NMLH_(d23b, d45b, k67b) WHX_HEOR_(x1b, y1b) WHX_NACC_(3, d23b) \
    WHX_NUZ1_(k45a, k23a, k23b) WHX_HLSR_(y0b, x0b) WHX_NUZ2_(k23b, k23a, k23b) \
    WHX_HLSR_(y1b, x1b) WHX_NMLL_(d67a, k45a, k23b) WHX_NMLH_(d67b, k45a, k23b) \
    WHX_HMAD_(g0, x0b, y0b) WHX_NACC_(1, d67a) WHX_HMAD_(g1, x1b, y1b) WHX_NACC_(1, d67b)
#define WHX_HLOAD_(...) WHX_HLOAD__(__VA_ARGS__)
#define WHX_HSTEP_(...) WHX_HSTEP__(__VA_ARGS__)
#define WHX_HLAST_(...) WHX_HLAST__(__VA_ARGS__)
// Scalar ldp reaches +/-512 bytes, so pairs 4-7 address their scalar loads from
// p2 = p + 512 / k2 = k + 512.
#define WHX_NHYB_(p_, k_, p2_, k2_, a0_, a1_, a2_, a3_, g0_, g1_, s0_, s1_) __asm__ volatile( \
    WHX_HLOAD_(16, 32, 48, 0, 80, 96, 112, 64, p, k, WHX_HSETE_)                              \
    WHX_HSTEP_(144, 160, 176, 128, 208, 224, 240, 192, p, k, WHX_HSETE_, WHX_HSETO_)         \
    WHX_HSTEP_(272, 288, 304, 256, 336, 352, 368, 320, p, k, WHX_HSETO_, WHX_HSETE_)         \
    WHX_HSTEP_(400, 416, 432, 384, 464, 480, 496, 448, p, k, WHX_HSETE_, WHX_HSETO_)         \
    WHX_HSTEP_(528, 544, 560, 0, 592, 608, 624, 64, p2, k2, WHX_HSETO_, WHX_HSETE_)          \
    WHX_HSTEP_(656, 672, 688, 128, 720, 736, 752, 192, p2, k2, WHX_HSETE_, WHX_HSETO_)       \
    WHX_HSTEP_(784, 800, 816, 256, 848, 864, 880, 320, p2, k2, WHX_HSETO_, WHX_HSETE_)       \
    WHX_HSTEP_(912, 928, 944, 384, 976, 992, 1008, 448, p2, k2, WHX_HSETE_, WHX_HSETO_)      \
    WHX_HLAST_(WHX_HSETO_)                                                                    \
    : [a0] "+w"(a0_), [a1] "+w"(a1_), [a2] "+w"(a2_), [a3] "+w"(a3_),                         \
      [g0] "+r"(g0_), [g1] "+r"(g1_), [s0] "+r"(s0_), [s1] "+r"(s1_)                          \
    : [p] "r"(p_), [k] "r"(k_), [p2] "r"(p2_), [k2] "r"(k2_)                                  \
    : "memory", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12",         \
      "x13", "x14", "x15", "x16", "x17", "v4", "v5", "v6", "v7", "v8", "v9", "v10", "v11",    \
      "v12", "v13", "v14", "v15", "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23",    \
      "v24", "v25", "v26", "v27")
// One hybrid 1 KiB block. g/s: scalar lane 0,1 products and lane 4,5 cross-half
// data, folded into the vector accumulators once per block.
#define WHX_NHBLK_(p_, k_, a0_, a1_, a2_, a3_) do {                                           \
    uint64_t g0_ = 0, g1_ = 0, s0_ = 0, s1_ = 0;                                              \
    WHX_NHYB_(p_, k_, (p_) + 512, (k_) + 64, a0_, a1_, a2_, a3_, g0_, g1_, s0_, s1_);         \
    (a0_) = vaddq_u64(a0_, vcombine_u64(vcreate_u64(g0_), vcreate_u64(g1_)));                 \
    (a2_) = vaddq_u64(a2_, vcombine_u64(vcreate_u64(s0_), vcreate_u64(s1_)));                 \
} while (0)
// Alignment dispatch, decided once per call: hybrid for 4-byte aligned input (or any
// input on wide cores), else the all-NEON block. Each gets its own loop.
#define WHX_NHYB_OK_(p_) (WHX_NEON_WIDE || ((uintptr_t)(p_) & 3u) == 0)
#endif

WHX_FN WHX_NOINLINE
void whx_bulk_neon(const void *input, size_t len, uint64_t seed, void *out)
{
    const uint8_t *ptr = (const uint8_t *)input;
    const uint64_t *key;
    WHX_ALIGN(16) uint64_t acc[8];
    uint64_t a1, a2, p0, s0;
    uint64x2_t v0, v1, v2, v3;
    size_t nblk, nstr;

    whx_init(acc, seed, len);
    v0 = vld1q_u64(acc);     v1 = vld1q_u64(acc + 2);
    v2 = vld1q_u64(acc + 4); v3 = vld1q_u64(acc + 6);

    nblk = len >> 10;
    nstr = (len & 1023u) >> 6;

#if defined(WHX_NHBLK_)
    #define WHX_DO_(BLOCK_) while (nblk--) {                                    \
        BLOCK_(ptr, WHX_KT[0], v0, v1, v2, v3);                                 \
        ptr += 1024;                                                            \
        v0 = whx_scr128(v0, vld1q_u64(WHX_SC));                                 \
        v1 = whx_scr128(v1, vld1q_u64(WHX_SC + 2));                             \
        v2 = whx_scr128(v2, vld1q_u64(WHX_SC + 4));                             \
        v3 = whx_scr128(v3, vld1q_u64(WHX_SC + 6));                             \
    }
    if (WHX_NHYB_OK_(ptr)) { WHX_DO_(WHX_NHBLK_) } else { WHX_DO_(WHX_NBLOCK_) }
    #undef WHX_DO_
#else
    while (nblk--) {
        key = WHX_KT[0];
        #define WHX_DO_(s_) WHX_STRIPE128_(v0, v1, v2, v3, ptr, key);           \
                            ptr += 64; key += 8;                                \
                            WHX_NFENCE_(ptr, key, v0, v1, v2, v3);
        WHX_STRIPES16_(WHX_DO_)
        #undef WHX_DO_
        v0 = whx_scr128(v0, vld1q_u64(WHX_SC));
        v1 = whx_scr128(v1, vld1q_u64(WHX_SC + 2));
        v2 = whx_scr128(v2, vld1q_u64(WHX_SC + 4));
        v3 = whx_scr128(v3, vld1q_u64(WHX_SC + 6));
    }
#endif
    key = WHX_KT[0];
    while (nstr--) {
        WHX_STRIPE128_(v0, v1, v2, v3, ptr, key);
        ptr += 64; key += 8;
        WHX_NFENCE_(ptr, key, v0, v1, v2, v3);
    }

    vst1q_u64(acc, v0);     vst1q_u64(acc + 2, v1);
    vst1q_u64(acc + 4, v2); vst1q_u64(acc + 6, v3);

    whx_merge(acc, &a1, &a2);
    p0 = WHX_P0 ^ seed;
    s0 = WHX_S0 ^ whx_rol64(seed, 32);
    whx_tail(ptr, len & 63u, a1, a2, s0, WHX_S1, p0, out);
}

WHX_FN void wifehash128x_neon(const void *input, size_t len,
                              uint64_t seed, void *out)
{
    if (len < WHX_BULK_MIN) whx_small(input, len, seed, out);
    else                    whx_bulk_neon(input, len, seed, out);
}

// Seeded (with-state) NEON bulk path. Same stripe and fence as whx_bulk_neon;
// keys walk the per-seed table one 1 KiB block at a time and wrap after nkb.
WHX_FN WHX_NOINLINE
void whx_ws_bulk_neon(const void *input, size_t len, uint64_t seed, void *out,
                      const uint64_t *kt, size_t nkb,
                      const uint64_t *sc, const uint64_t *c)
{
    const uint8_t *ptr = (const uint8_t *)input;
    const uint64_t *key, *kb = kt, *kend = kt + 128 * nkb;
    WHX_ALIGN(16) uint64_t acc[8];
    uint64_t a1, a2;
    uint64x2_t v0, v1, v2, v3;
    size_t nblk, nstr;

    whx_init(acc, seed, len);
    v0 = vld1q_u64(acc);     v1 = vld1q_u64(acc + 2);
    v2 = vld1q_u64(acc + 4); v3 = vld1q_u64(acc + 6);

    nblk = len >> 10;
    nstr = (len & 1023u) >> 6;

#if defined(WHX_NHBLK_)
    #define WHX_DO_(BLOCK_) while (nblk--) {                                    \
        BLOCK_(ptr, kb, v0, v1, v2, v3);                                        \
        ptr += 1024;                                                            \
        v0 = whx_scr128(v0, vld1q_u64(sc));                                     \
        v1 = whx_scr128(v1, vld1q_u64(sc + 2));                                 \
        v2 = whx_scr128(v2, vld1q_u64(sc + 4));                                 \
        v3 = whx_scr128(v3, vld1q_u64(sc + 6));                                 \
        kb += 128;                                                              \
        if (kb == kend) kb = kt;                                                \
    }
    if (WHX_NHYB_OK_(ptr)) { WHX_DO_(WHX_NHBLK_) } else { WHX_DO_(WHX_NBLOCK_) }
    #undef WHX_DO_
#else
    while (nblk--) {
        key = kb;
        #define WHX_DO_(s_) WHX_STRIPE128_(v0, v1, v2, v3, ptr, key);           \
                            ptr += 64; key += 8;                                \
                            WHX_NFENCE_(ptr, key, v0, v1, v2, v3);
        WHX_STRIPES16_(WHX_DO_)
        #undef WHX_DO_
        v0 = whx_scr128(v0, vld1q_u64(sc));
        v1 = whx_scr128(v1, vld1q_u64(sc + 2));
        v2 = whx_scr128(v2, vld1q_u64(sc + 4));
        v3 = whx_scr128(v3, vld1q_u64(sc + 6));
        kb += 128;
        if (kb == kend) kb = kt;
    }
#endif
    key = kb;
    while (nstr--) {
        WHX_STRIPE128_(v0, v1, v2, v3, ptr, key);
        ptr += 64; key += 8;
        WHX_NFENCE_(ptr, key, v0, v1, v2, v3);
    }

    vst1q_u64(acc, v0);     vst1q_u64(acc + 2, v1);
    vst1q_u64(acc + 4, v2); vst1q_u64(acc + 6, v3);

    whx_merge_c(acc, sc, &a1, &a2);
    whx_tail_c(ptr, len & 63u, a1, a2, c[2] ^ whx_rol64(seed, 32), c[3],
               c[0] ^ seed, c[0], c[1], out);
}

#endif // WHX_NEON

// Dispatcher
#if defined(__AVX2__)
  #define wifehash128x wifehash128x_avx2
  #define WHX_BACKEND  "avx2"
#elif defined(WHX_NEON)
  #define wifehash128x wifehash128x_neon
  #define WHX_BACKEND  "neon"
#else
  #define wifehash128x wifehash128x_ref
  #define WHX_BACKEND  "scalar"
#endif

// Truncate for 64 bits. We already know from smhasher's tests that we can do this
// Rather Freely.
WHX_FN uint64_t wifehash64x(const void *input, size_t len, uint64_t seed)
{
    uint64_t o[2];
    wifehash128x(input, len, seed, o);
    return o[0];
}

/*
 * Seeded, with-state variants.
 *
 *   ws_wifehash128x   1 KiB per-seed key table
 *   wsx_wifehash128x  256 KiB per-seed key table (WHX_WSX_BLOCKS x 1 KiB)
 *
 * The unseeded hash XORs input with fixed public constants (WHX_KT, P0/P1/S0/S1),
 * which lets anyone build collisions that hold for every seed. Here every
 * constant that touches input is derived from the seed, once, by *_init().
 *
 * The derivation expands the names of the Citrus heroines with the seed through a
 * meimix-style mixer, so the table is unpredictable without the seed.
 *
 * The state is plain data: build it once per seed, then share it read-only
 * across threads. For best speed allocate it 64-byte aligned (static,
 * aligned_alloc, _aligned_malloc); unaligned states still hash correctly.
 */

#ifndef WHX_WSX_BLOCKS
  #define WHX_WSX_BLOCKS 256   // 1 KiB key blocks in the wsx table (256 = 256 KiB)
#endif

// Yuzu Aihara, Mei Aihara, Harumi Taniguchi, Matsuri Mizusawa, Himeko Momokino.
static const char WHX_NAMES[136] =
    "YuzuAihara" "MeiAihara" "HarumiTaniguchi" "MatsuriMizusawa" "HimekoMomokino"
    "\xe8\x97\x8d\xe5\x8e\x9f\xe6\x9f\x9a\xe5\xad\x90"                  // Aihara Yuzu
    "\xe8\x97\x8d\xe5\x8e\x9f\xe8\x8a\xbd\xe8\xa1\xa3"                  // Aihara Mei
    "\xe8\xb0\xb7\xe5\x8f\xa3\xe3\x81\xaf\xe3\x82\x8b\xe3\x81\xbf"      // Taniguchi Harumi
    "\xe6\xb0\xb4\xe6\xb2\xa2\xe3\x81\xbe\xe3\x81\xa4\xe3\x82\x8a"      // Mizusawa Matsuri
    "\xe6\xa1\x83\xe6\x9c\xa8\xe9\x87\x8e\xe5\xa7\xab\xe5\xad\x90";     // Momokino Himeko
#define WHX_NAME_WORDS 17 // 132 name bytes, zero padded to 136

#define WHX_WS_SC_IDX UINT64_C(0x5343000000000000) // "SC": scramble/fold constants
#define WHX_WS_C_IDX  UINT64_C(0x4300000000000000) // "C":  small-path/tail XOR constants

// One table word: (seed ^ name word) + counter, through a meimix-style (meihalf) mixer.
// Note: the other half of meimix, yuzuhalf, uses a multiplication instead of an add.
WHX_INLINE uint64_t whx_ws_word(uint64_t seed, uint64_t idx)
{
    uint64_t z = (seed ^ whx_ld64(WHX_NAMES + 8 * (size_t)(idx % WHX_NAME_WORDS)))
               + (idx + 1) * WHX_GOLD;
    z = (z ^ (z >> 30)) * WHX_S1;
    z = (z ^ (z >> 27)) * WHX_P0;
    return z ^ (z >> 31);
}

WHX_INLINE void whx_ws_fill(uint64_t *kt, size_t nwords, uint64_t sc[8],
                            uint64_t c[4], uint64_t seed)
{
    size_t i;
    for (i = 0; i < nwords; i++) kt[i] = whx_ws_word(seed, i);
    for (i = 0; i < 8; i++)      sc[i] = whx_ws_word(seed, WHX_WS_SC_IDX + i);
    for (i = 0; i < 4; i++)      c[i]  = whx_ws_word(seed, WHX_WS_C_IDX + i);
}

typedef struct whx_ws_state {
    WHX_ALIGN(64) uint64_t kt[128];
    uint64_t sc[8];
    uint64_t c[4];
    uint64_t seed;
} whx_ws_state;

typedef struct whx_wsx_state {
    WHX_ALIGN(64) uint64_t kt[128 * WHX_WSX_BLOCKS];
    uint64_t sc[8];
    uint64_t c[4];
    uint64_t seed;
} whx_wsx_state;

#if defined(__AVX2__)
  #define WHX_WS_BULK whx_ws_bulk_avx2
#elif defined(WHX_NEON)
  #define WHX_WS_BULK whx_ws_bulk_neon
#else
  #define WHX_WS_BULK whx_ws_bulk_ref
#endif

WHX_FN void ws_wifehash128x_init(whx_ws_state *st, uint64_t seed)
{
    st->seed = seed;
    whx_ws_fill(st->kt, 128, st->sc, st->c, seed);
}

WHX_FN void ws_wifehash128x(const whx_ws_state *st, const void *input,
                            size_t len, void *out)
{
    if (len < WHX_BULK_MIN)
        whx_small_c(input, len, st->seed, out, st->c[0], st->c[1], st->c[2], st->c[3]);
    else
        WHX_WS_BULK(input, len, st->seed, out, st->kt, 1, st->sc, st->c);
}

WHX_FN uint64_t ws_wifehash64x(const whx_ws_state *st, const void *input, size_t len)
{
    uint64_t o[2];
    ws_wifehash128x(st, input, len, o);
    return o[0];
}

WHX_FN void wsx_wifehash128x_init(whx_wsx_state *st, uint64_t seed)
{
    st->seed = seed;
    whx_ws_fill(st->kt, 128 * (size_t)WHX_WSX_BLOCKS, st->sc, st->c, seed);
}

WHX_FN void wsx_wifehash128x(const whx_wsx_state *st, const void *input,
                             size_t len, void *out)
{
    if (len < WHX_BULK_MIN)
        whx_small_c(input, len, st->seed, out, st->c[0], st->c[1], st->c[2], st->c[3]);
    else
        WHX_WS_BULK(input, len, st->seed, out, st->kt, WHX_WSX_BLOCKS, st->sc, st->c);
}

WHX_FN uint64_t wsx_wifehash64x(const whx_wsx_state *st, const void *input, size_t len)
{
    uint64_t o[2];
    wsx_wifehash128x(st, input, len, o);
    return o[0];
}

/*
 * Streaming: *_stream_init / wifehash128x_update / wifehash128x_final.
 * The digest is identical to the one-shot function over the concatenated input.
 *
 * The total length must be declared at init: wifehash mixes it into the lane state
 * before the first 1 KiB scramble. update() returns -1 (and hashes nothing) if it
 * would exceed the declared length; final() returns -1 if fewer bytes were fed.
 *
 * How update() stays fast:
 *  - Stripe-granular: every complete 64-byte stripe is hashed in place, straight from
 *    the caller's memory, the moment it arrives. A stripe's work depends only on its
 *    position (key = stripe index, scramble after the 16th), never on what follows,
 *    so only a partial stripe (< 64 bytes) is ever staged. At most 126 bytes are
 *    copied per call; whole-stripe input is never copied.
 *  - Small updates never leave update(): stripes that stay inside the open 1 KiB
 *    block are hashed inline (no call, no scramble constants, no key-block step), and
 *    partial stripes are staged with two overlapping fixed-size moves per size class
 *    instead of a libc memcpy. A stream of equal-size updates always takes the same
 *    branches, so they predict perfectly.
 *  - Larger updates make one feed call: the accumulators are loaded into registers
 *    once, the leading stripes finish the open block, whole blocks run as tiles, the
 *    trailing stripes start the next one, and the state is stored once.
 *  - The tiles are the one-shot kernels: AVX2 stripes, and on AArch64 the
 *    hand-scheduled asm blocks with the hybrid / all-NEON alignment dispatch.
 *  - x86 software prefetch, one line per stripe, WHX_STREAM_PREFETCH bytes ahead
 *    (0 = off). The hardware L2 streamer stops at 4 KiB page boundaries; the software
 *    prefetch crosses them. The best distance is a property of the core, measured on
 *    input outside L2: Golden Cove (Alder / Raptor Lake) gains most at 2048 bytes,
 *    Zen 3 at 320-512, where 2048 loses. Builds tuned for Golden Cove default to 2048,
 *    every other AVX2 build to 512 (ahead of no prefetch on all three cores tested).
 *    No prefetch on AArch64: it cost 8-66% on Cortex-A76 and Apple M1, whose hardware
 *    prefetchers already stream DRAM at near full speed.
 *
 * The stream keeps pointers into a ws_/wsx_ state: the state must outlive it.
 * final() does not modify the stream.
 */
#ifndef WHX_STREAM_PREFETCH
  #if defined(__AVX2__) && (defined(__alderlake__) || defined(__raptorlake__) || \
                            defined(__tune_alderlake__) || defined(__tune_raptorlake__))
    #define WHX_STREAM_PREFETCH 2048
  #elif defined(__AVX2__)
    #define WHX_STREAM_PREFETCH 512
  #else
    #define WHX_STREAM_PREFETCH 0
  #endif
#endif

#if defined(__GNUC__) || defined(__clang__)
  #define WHX_LIKELY(x_)   __builtin_expect(!!(x_), 1)
  #define WHX_UNLIKELY(x_) __builtin_expect(!!(x_), 0)
#else
  #define WHX_LIKELY(x_)   (x_)
  #define WHX_UNLIKELY(x_) (x_)
#endif

typedef struct whx_stream {
    WHX_ALIGN(64) uint64_t acc[8];   // lane state
    uint8_t buf[192];                // partial stripe; the whole input below WHX_BULK_MIN
    const uint64_t *kt, *kb, *kend;  // key table, current 1 KiB key block, table end
    const uint64_t *sc;              // scramble / fold constants
    uint64_t c[4];                   // small-path / tail constants
    uint64_t seed, len, fed;         // seed, declared length, bytes fed so far
    uint32_t bufn, si;               // bytes staged in buf, next stripe index (0-15)
} whx_stream;

// One prefetched line per stripe, interleaved with the stripe's loads (x86 only).
#if WHX_STREAM_PREFETCH > 0 && defined(__AVX2__)
  #define WHX_SPF_(q_) _mm_prefetch((const char *)(q_), _MM_HINT_T0)
#else
  #define WHX_SPF_(q_) ((void)0)
#endif

// Next key block, wrapping at the end of the table.
#define WHX_SNEXT_(kb_, kt_, kend_) do { (kb_) += 128; if ((kb_) == (kend_)) (kb_) = (kt_); } while (0)

// Feed nstr complete stripes from p. Each backend: finish the open block stripe by
// stripe, run whole blocks as tiles, then start the next block.
#if defined(__AVX2__)

WHX_FN WHX_NOINLINE
void whx_sfeed_avx2(whx_stream *s, const uint8_t *p, size_t nstr)
{
    const uint64_t *kb = s->kb, *kt = s->kt, *kend = s->kend;
    unsigned si = s->si;
    size_t k, nblk;
    __m256i va, vb, sca, scb;

    va  = _mm256_load_si256((const __m256i *)s->acc);
    vb  = _mm256_load_si256((const __m256i *)(s->acc + 4));
    sca = _mm256_loadu_si256((const __m256i *)s->sc);
    scb = _mm256_loadu_si256((const __m256i *)(s->sc + 4));

    if (si) {
        k = 16u - si;
        if (k > nstr) k = nstr;
        nstr -= k;
        while (k--) {
            WHX_STRIPE256U_(va, vb, p, kb + 8 * si);
            WHX_PIN_(va, vb);
            p += 64; si++;
        }
        if (si == 16) {
            va = whx_scr256(va, sca); vb = whx_scr256(vb, scb);
            si = 0;
            WHX_SNEXT_(kb, kt, kend);
        }
    }
    nblk = nstr >> 4;
    nstr &= 15;
    while (nblk--) {
        #define WHX_DO_(s_) WHX_SPF_(p + 64*(s_) + WHX_STREAM_PREFETCH);        \
                            WHX_STRIPE256U_(va, vb, p + 64*(s_), kb + 8*(s_)); \
                            WHX_PIN_(va, vb);
        WHX_STRIPES16_(WHX_DO_)
        #undef WHX_DO_
        p += 1024;
        va = whx_scr256(va, sca); vb = whx_scr256(vb, scb);
        WHX_SNEXT_(kb, kt, kend);
    }
    while (nstr--) {
        WHX_STRIPE256U_(va, vb, p, kb + 8 * si);
        WHX_PIN_(va, vb);
        p += 64; si++;
    }

    _mm256_store_si256((__m256i *)s->acc, va);
    _mm256_store_si256((__m256i *)(s->acc + 4), vb);
    s->kb = kb; s->si = si;
}
#define WHX_SFEED whx_sfeed_avx2

#elif defined(WHX_NEON)

#define WHX_SSCR_N_ do {                                                        \
    v0 = whx_scr128(v0, vld1q_u64(sc));     v1 = whx_scr128(v1, vld1q_u64(sc + 2)); \
    v2 = whx_scr128(v2, vld1q_u64(sc + 4)); v3 = whx_scr128(v3, vld1q_u64(sc + 6)); \
} while (0)

WHX_FN WHX_NOINLINE
void whx_sfeed_neon(whx_stream *s, const uint8_t *p, size_t nstr)
{
    const uint64_t *kb = s->kb, *kt = s->kt, *kend = s->kend, *sc = s->sc, *key;
    unsigned si = s->si;
    size_t k, nblk;
    uint64x2_t v0, v1, v2, v3;

    v0 = vld1q_u64(s->acc);     v1 = vld1q_u64(s->acc + 2);
    v2 = vld1q_u64(s->acc + 4); v3 = vld1q_u64(s->acc + 6);

    if (si) {
        k = 16u - si;
        if (k > nstr) k = nstr;
        nstr -= k;
        while (k--) {
            key = kb + 8 * si;
            WHX_STRIPE128_(v0, v1, v2, v3, p, key);
            p += 64; si++;
            WHX_NFENCE_(p, key, v0, v1, v2, v3);
        }
        if (si == 16) {
            WHX_SSCR_N_;
            si = 0;
            WHX_SNEXT_(kb, kt, kend);
        }
    }
    nblk = nstr >> 4;
    nstr &= 15;
#if defined(WHX_NHBLK_)
    #define WHX_DO_(BLOCK_) while (nblk--) {                                    \
        BLOCK_(p, kb, v0, v1, v2, v3);                                          \
        p += 1024;                                                              \
        WHX_SSCR_N_;                                                            \
        WHX_SNEXT_(kb, kt, kend);                                               \
    }
    if (nblk) {
        if (WHX_NHYB_OK_(p)) { WHX_DO_(WHX_NHBLK_) } else { WHX_DO_(WHX_NBLOCK_) }
    }
    #undef WHX_DO_
#else
    while (nblk--) {
        key = kb;
        #define WHX_DO_(s_) WHX_STRIPE128_(v0, v1, v2, v3, p, key);             \
                            p += 64; key += 8;                                  \
                            WHX_NFENCE_(p, key, v0, v1, v2, v3);
        WHX_STRIPES16_(WHX_DO_)
        #undef WHX_DO_
        WHX_SSCR_N_;
        WHX_SNEXT_(kb, kt, kend);
    }
#endif
    while (nstr--) {
        key = kb + 8 * si;
        WHX_STRIPE128_(v0, v1, v2, v3, p, key);
        p += 64; si++;
        WHX_NFENCE_(p, key, v0, v1, v2, v3);
    }

    vst1q_u64(s->acc, v0);     vst1q_u64(s->acc + 2, v1);
    vst1q_u64(s->acc + 4, v2); vst1q_u64(s->acc + 6, v3);
    s->kb = kb; s->si = si;
}
#undef WHX_SSCR_N_
#define WHX_SFEED whx_sfeed_neon

#else

WHX_FN WHX_NOINLINE
void whx_sfeed_ref(whx_stream *s, const uint8_t *p, size_t nstr)
{
    const uint64_t *kb = s->kb, *kt = s->kt, *kend = s->kend;
    unsigned si = s->si;
    uint64_t acc[8];

    memcpy(acc, s->acc, sizeof acc);
    while (nstr--) {
        whx_stripe_ref(acc, p, kb + 8 * si);
        p += 64;
        if (++si == 16) {
            whx_scramble_c(acc, s->sc);
            si = 0;
            WHX_SNEXT_(kb, kt, kend);
        }
    }
    memcpy(s->acc, acc, sizeof acc);
    s->kb = kb; s->si = si;
}
#define WHX_SFEED whx_sfeed_ref

#endif

static const uint64_t WHX_CDEF[4] = { WHX_P0, WHX_P1, WHX_S0, WHX_S1 };

WHX_INLINE void whx_stream_setup(whx_stream *s, uint64_t seed, uint64_t len,
                                 const uint64_t *kt, size_t nkb,
                                 const uint64_t *sc, const uint64_t *c)
{
    s->kt = kt; s->kb = kt; s->kend = kt + 128 * nkb;
    s->sc = sc;
    memcpy(s->c, c, sizeof s->c);
    s->seed = seed; s->len = len; s->fed = 0;
    s->bufn = 0; s->si = 0;
    if (len >= WHX_BULK_MIN) whx_init(s->acc, seed, len);
}

// Stream for wifehash128x(input, len, seed).
WHX_FN void wifehash128x_stream_init(whx_stream *s, uint64_t seed, uint64_t len)
{
    whx_stream_setup(s, seed, len, WHX_KT[0], 1, WHX_SC, WHX_CDEF);
}

// Stream for ws_wifehash128x(st, input, len).
WHX_FN void ws_wifehash128x_stream_init(whx_stream *s, const whx_ws_state *st, uint64_t len)
{
    whx_stream_setup(s, st->seed, len, st->kt, 1, st->sc, st->c);
}

// Stream for wsx_wifehash128x(st, input, len).
WHX_FN void wsx_wifehash128x_stream_init(whx_stream *s, const whx_wsx_state *st, uint64_t len)
{
    whx_stream_setup(s, st->seed, len, st->kt, WHX_WSX_BLOCKS, st->sc, st->c);
}

// Stripes that stay inside the open 1 KiB block, hashed inline in update(): no call,
// no scramble constants, no key-block step. Anything that reaches the block end goes
// to the feed kernel. Removing the call took 64-byte updates from 2-15% behind XXH3's
// streaming API to 30-40% ahead on Alder Lake and Zen 3.
#if defined(__AVX2__)
WHX_INLINE void whx_sfeed_in(whx_stream *s, const uint8_t *p, size_t nstr)
{
    __m256i va, vb;
    const uint64_t *key;
    if (WHX_LIKELY(nstr < 16u - s->si)) {
        va  = _mm256_load_si256((const __m256i *)s->acc);
        vb  = _mm256_load_si256((const __m256i *)(s->acc + 4));
        key = s->kb + 8 * s->si;
        s->si += (uint32_t)nstr;
        do {
            WHX_STRIPE256U_(va, vb, p, key);
            WHX_PIN_(va, vb);
            p += 64; key += 8;
        } while (--nstr);
        _mm256_store_si256((__m256i *)s->acc, va);
        _mm256_store_si256((__m256i *)(s->acc + 4), vb);
    } else {
        WHX_SFEED(s, p, nstr);
    }
}
#elif defined(WHX_NEON)
WHX_INLINE void whx_sfeed_in(whx_stream *s, const uint8_t *p, size_t nstr)
{
    uint64x2_t v0, v1, v2, v3;
    const uint64_t *key;
    if (WHX_LIKELY(nstr < 16u - s->si)) {
        v0 = vld1q_u64(s->acc);     v1 = vld1q_u64(s->acc + 2);
        v2 = vld1q_u64(s->acc + 4); v3 = vld1q_u64(s->acc + 6);
        key = s->kb + 8 * s->si;
        s->si += (uint32_t)nstr;
        do {
            WHX_STRIPE128_(v0, v1, v2, v3, p, key);
            p += 64; key += 8;
            WHX_NFENCE_(p, key, v0, v1, v2, v3);
        } while (--nstr);
        vst1q_u64(s->acc, v0);     vst1q_u64(s->acc + 2, v1);
        vst1q_u64(s->acc + 4, v2); vst1q_u64(s->acc + 6, v3);
    } else {
        WHX_SFEED(s, p, nstr);
    }
}
#else
  #define whx_sfeed_in WHX_SFEED
#endif

// Copy 1-63 bytes without a libc call: two overlapping fixed-size moves per size class.
// Took 16-byte updates from up to 17% behind XXH3's streaming API to 25-61% ahead.
WHX_INLINE void whx_copy63(uint8_t *d, const uint8_t *s, size_t n)
{
    if (n >= 32)      { memcpy(d, s, 32); memcpy(d + n - 32, s + n - 32, 32); }
    else if (n >= 16) { memcpy(d, s, 16); memcpy(d + n - 16, s + n - 16, 16); }
    else if (n >= 8)  { memcpy(d, s, 8);  memcpy(d + n - 8,  s + n - 8,  8); }
    else if (n >= 4)  { memcpy(d, s, 4);  memcpy(d + n - 4,  s + n - 4,  4); }
    else              { d[0] = s[0]; d[n >> 1] = s[n >> 1]; d[n - 1] = s[n - 1]; }
}

WHX_FN int wifehash128x_update(whx_stream *s, const void *input, size_t n)
{
    const uint8_t *p = (const uint8_t *)input;
    size_t take, nstr;

    if (WHX_UNLIKELY(n > s->len - s->fed)) return -1;
    if (WHX_UNLIKELY(n == 0)) return 0;
    s->fed += n;

    if (WHX_UNLIKELY(s->len < WHX_BULK_MIN)) {   // small path: keep everything
        memcpy(s->buf + s->bufn, p, n);
        s->bufn += (uint32_t)n;
        return 0;
    }
    if (s->bufn) {                                // complete the staged stripe
        take = 64u - s->bufn;
        if (n < take) {
            whx_copy63(s->buf + s->bufn, p, n);
            s->bufn += (uint32_t)n;
            return 0;
        }
        whx_copy63(s->buf + s->bufn, p, take);
        p += take; n -= take;
        whx_sfeed_in(s, s->buf, 1);
        s->bufn = 0;
    }
    nstr = n >> 6;
    if (WHX_LIKELY(nstr)) {                       // whole stripes, in place
        whx_sfeed_in(s, p, nstr);
        p += nstr << 6;
        n &= 63u;
    }
    if (n) {                                      // stage the partial stripe
        whx_copy63(s->buf, p, n);
        s->bufn = (uint32_t)n;
    }
    return 0;
}

// Writes the 16-byte digest. The stream is unchanged, so final() can be repeated.
WHX_FN int wifehash128x_final(const whx_stream *s, void *out)
{
    uint64_t a1, a2;

    if (s->fed != s->len) return -1;
    if (s->len < WHX_BULK_MIN) {
        whx_small_c(s->buf, (size_t)s->len, s->seed, out, s->c[0], s->c[1], s->c[2], s->c[3]);
        return 0;
    }
    // Every complete stripe is already in acc; buf holds the last len % 64 bytes.
    whx_merge_c(s->acc, s->sc, &a1, &a2);
    whx_tail_c(s->buf, s->bufn, a1, a2, s->c[2] ^ whx_rol64(s->seed, 32), s->c[3],
               s->c[0] ^ s->seed, s->c[0], s->c[1], out);
    return 0;
}

#endif // WIFEHASH_H
