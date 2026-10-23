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

WIFEHASH128x implementation. Uses both AVX2 and Regular paths.
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

WHX_INLINE void whx_tail(const uint8_t *ptr_, size_t len_,
                         uint64_t a1_, uint64_t a2_,
                         uint64_t s0_, uint64_t s1_, uint64_t p0_, void *out_)
{
    while (len_ >= 16) {
        uint64_t h_, l_ = whx_mul128(whx_ld64(ptr_)     ^ WHX_P0,
                                     whx_ld64(ptr_ + 8) ^ WHX_P1, &h_);
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

WHX_INLINE void whx_small(const void *input, size_t len,
                          uint64_t seed, void *out)
{
    //Bee nice to the stack! :)
    uint64_t h, l, t; // Define here instead to bee nice to the stack.
    const uint8_t *ptr = (const uint8_t *)input;
    uint64_t p0 = WHX_P0 ^ seed;
    uint64_t s0 = WHX_S0 ^ whx_rol64(seed, 32);
    uint64_t s1 = WHX_S1;
    uint64_t a1 = WHX_P1, a2 = WHX_P0;
    uint64_t l1l = 0, l1h = 0, l2l = 0, l2h = 0;
    uint64_t l3l = 0, l3h = 0, l4l = 0, l4h = 0;
    // uint64_t h, l, t;

    while (len >= 64) 
    {
        l = whx_mul128(whx_ld64(ptr     ) ^ WHX_S0,
                       whx_ld64(ptr +  8) ^ WHX_P1, &h); l1l ^= l; l1h ^= h;
        l = whx_mul128(whx_ld64(ptr + 16) ^ WHX_P0,
                       whx_ld64(ptr + 24) ^ WHX_S1, &h); l2l ^= l; l2h ^= h;
        l = whx_mul128(whx_ld64(ptr + 32) ^ WHX_P1,
                       whx_ld64(ptr + 40) ^ WHX_S0, &h); l3l ^= l; l3h ^= h;
        l = whx_mul128(whx_ld64(ptr + 48) ^ WHX_S1,
                       whx_ld64(ptr + 56) ^ WHX_P0, &h); l4l ^= l; l4h ^= h;

        a1 ^= l1l; a2 ^= l1h;
        a1 ^= whx_rol64(a1,1); a2 ^= whx_rol64(a2,1); a1 ^= l2l; a2 ^= l2h;
        a1 ^= whx_rol64(a1,1); a2 ^= whx_rol64(a2,1); a1 ^= l3l; a2 ^= l3h;
        a1 ^= whx_rol64(a1,1); a2 ^= whx_rol64(a2,1); a1 ^= l4l; a2 ^= l4h;

        t  = a1;
        a1 = whx_rol64(a1,1) + a2;
        a2 = whx_rol64(t, 1) + a1;
        ptr += 64; len -= 64;
    }

    whx_tail(ptr, len, a1, a2, s0, s1, p0, out);
}

// wifehash baby version
WHX_FN void wifehash128x_small(const void *input, size_t len,
                               uint64_t seed, void *out)
{
    whx_small(input, len, seed, out);
}

// Laneinit, laneseed, lanemix
WHX_INLINE void whx_init(uint64_t acc[8], uint64_t seed, size_t len)
{
    int j = 0; // bee nice to stack!
    uint64_t z = (seed ^ WHX_S0) * WHX_P0 + (uint64_t)len * WHX_GOLD;
    z ^= z >> 31;
    for (j = 0; j < 8; j++)
        acc[j] = (WHX_KT[0][j] + z) * WHX_ODD[j] ^ whx_rol64(seed, 8u*(unsigned)j + 3u);
}

// Lanefold
WHX_INLINE void whx_merge(const uint64_t acc[8], uint64_t *pa1, uint64_t *pa2)
{
    uint64_t a1 = WHX_P1, a2 = WHX_P0;
    int j = 0;
    for (; j < 4; j++) {
        uint64_t h, t, l = whx_mul128(acc[j] ^ WHX_SC[j],
                                      acc[j + 4] ^ WHX_SC[j + 4], &h);
        a1 ^= l; a2 ^= h;
        t  = a1;
        a1 = whx_rol64(a1, 1) + a2;
        a2 = whx_rol64(t,  1) ^ a1;
    }
    *pa1 = a1; *pa2 = a2;
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

// AVX2 Hasher
#if defined(__AVX2__)

#define WHX_STRIPE256_(va_, vb_, p_, key_) do {                              \
    __m256i d0_ = _mm256_loadu_si256((const __m256i *)(p_));                 \
    __m256i d1_ = _mm256_loadu_si256((const __m256i *)((p_) + 32));          \
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

#if defined(__GNUC__)
  #define WHX_PIN_(a_, b_) __asm__("" : "+x"(a_), "+x"(b_))
#else
  #define WHX_PIN_(a_, b_) ((void)0)
#endif

#define WHX_STRIPES16_(M_) \
    M_(0)  M_(1)  M_(2)  M_(3)  M_(4)  M_(5)  M_(6)  M_(7) \
    M_(8)  M_(9)  M_(10) M_(11) M_(12) M_(13) M_(14) M_(15)

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

#endif // __AVX2__

// Dispatcher
#if defined(__AVX2__)
  #define wifehash128x wifehash128x_avx2
  #define WHX_BACKEND  "avx2"
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

#endif // WIFEHASH_H
