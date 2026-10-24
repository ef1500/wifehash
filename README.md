<div align="center">

# wifehash

**A 128-bit non-cryptographic hash that passes SMHasher3 extra, built for large inputs.**

</div>

---

- Passes ALL 252 tests in [SMHasher3](https://gitlab.com/fwojcik/smhasher3)
  ([full results](results/smhasher3-wifehash.txt)). Verification code `0xAA7C087B`.
- AVX2 bulk path with a portable version built-in.
- C99 or C++11. Needs `<stddef.h>`, `<stdint.h>`, `<string.h>`, and `<immintrin.h>`
  only when the compiler reports AVX2.

wifehash is a member of the [MeiHash](https://github.com/ef1500/MeiHash) family; its mixer is meihash's dual-MUM
folding mixer, used one path at a time. See [How it works](#how-it-works).

## Install

Copy `wifehash.h` into your project and include it.

With CMake:

```cmake
find_package(wifehash 1.0 REQUIRED)
target_link_libraries(myapp PRIVATE wifehash::wifehash)
```

or, alternatively:

```cmake
add_subdirectory(external/wifehash)
target_link_libraries(myapp PRIVATE wifehash::wifehash)
```

For AVX2, build with `-mavx2`, `-march=native`, or `/arch:AVX2`.

## Usage

```c
#include "wifehash.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    const char *msg = "Hello!";
    uint64_t h[2]; // Result (128-bits)
    // h[0] = lohalf
    // h[1] = highhalf

    wifehash128x(msg, strlen(msg), 0, h);
    printf("%016llx%016llx\n", (unsigned long long)h[1],
                               (unsigned long long)h[0]);

    // 64-bit result
    uint64_t h64 = wifehash64x(msg, strlen(msg), 0);
    printf("%016llx\n", (unsigned long long)h64);
    return 0;
}
```

C++

```cpp
#include "wifehash.h"
#include <cstdint>
#include <string>

std::uint64_t hash_of(const std::string &s) {
    return wifehash64x(s.data(), s.size(), 0);
}
```

Hashing a file

```c
uint64_t digest[2];
wifehash128x(mapped_file, file_size, 0, digest);
```

Print the backend which is being used

```c
printf("wifehash backend: %s\n", WHX_BACKEND); // AVX2 or scalar
```

## Functions

| Symbol | Description |
|:---|:---|
| `void wifehash128x(const void *in, size_t len, uint64_t seed, void *out)` | The hash. Writes 16 bytes to `out`. Dispatches to the best backend at compile time. |
| `uint64_t wifehash64x(const void *in, size_t len, uint64_t seed)` | The low 64 bits of the above. |
| `void wifehash128x_small(const void *in, size_t len, uint64_t seed, void *out)` | The original scalar implementation of wifehash128. A different function from `wifehash128x` above 192 bytes. |
| `void wifehash128x_ref(...)` | Portable Hash |
| `void wifehash128x_avx2(...)` | AVX2 hash |
| `WHX_BACKEND` | Backend currently enabled |

Tunable defines

| Macro | Effect |
|:---|:---|
| `WHX_BULK_MIN` | Length at which the AVX2 version takes over. Default 192. **Changing this changes the hash.** |
| `WHX_NO_INT128` | Force the use of a portable 32x32 multiply instead of `__int128` / `_umul128`. |

## Known answers

Digests are printed high word first, as `h[1]` then `h[0]`.

| Input | Seed | `wifehash128x` |
|:---|:---|:---|
| (empty string) | 0 | `112225b27b53457984ca3a03e4d161cc` |
| `hello` | 0 | `99c4ab8dc0fc1e268cd20a19d684087e` |
| `hello` | 1 | `a8d5aa051a7b7cf5fed0c41b84ae479e` |
| `The quick brown fox jumps over the lazy dog` | 0 | `4b8bfb02fcdbfb49a9d363780c5bbbbf` |

## Performance

These numbers were recorded using [SMHasher3](https://gitlab.com/fwojcik/smhasher3)'s speed test on an Intel Core i7-10750H (Comet
Lake, 6 cores, 2.6 GHz base), gcc 14.1, `-O3 -march=native`, Release build. I tested each competing hash 5 times, took their average and reported them here. I am reporting wifehash's number from the results file so you may compare if you'd like. I've reported a ± range as well for XXH3-128 and Wifehash, since it's a pretty tight race.

| Hash | Output | Bulk (bytes/cycle) | Short keys (cycles/hash, 1-31 B) |
|:---|---:|---:|---:|
| **wifehash** | 128 | **21.95** (±0.30) | 23.14 (±0.67) |
| XXH3-128 | 128 | 20.4 (±0.38) | 19.1 (±0.38) |
| MeowHash (AES-NI) | 128 | 19.4 | 38.1 |
| rapidhash | 64 | 14.0 | 15.5 |
| MuseAir-128.bfast | 128 | 13.2 | 15.9 |
| MuseAir-128 | 128 | 11.4 | 16.3 |
| a5hash-128 | 128 | 10.8 | 13.4 |
| komihash | 64 | 10.3 | 16.5 |

~8% Faster than XXH3-128 (bulk), ~21% slower than XXH3-128 on small keys.

~13% Faster than MeowHash (which needs AES-NI) on bulk, ~39% faster than MeowHash on short keys.

It should be noted that wifehash is NOT for small keys. It's for bulk hashing.

Additionally note that XXH3-128 does NOT pass the full SMHasher3 suite. Wifehash does.

And on an Intel Core Ultra (Meteor Lake), same build and same test:

| Hash | Output | Bulk (bytes/cycle) | Short keys (cycles/hash, 1-31 B) |
|:---|---:|---:|---:|
| falkhash2 (AES-NI) | 128 | 28.40 | 45.72 |
| falkhash1 (AES-NI) | 128 | 28.36 | 44.79 |
| AquaHash (AES-NI) | 128 | 26.98 | 22.00 |
| **wifehash** | 128 | **26.23** (±0.01) | 29.22 (±0.19) |
| XXH3-128 | 128 | 22.81 (±0.01) | 26.18 (±0.40) |
| MeowHash (AES-NI) | 128 | 19.78 | 46.14 |
| rapidhash | 64 | 19.10 | 21.35 |

falkhash1, falkhash2 and AquaHash are faster on bulk here, but fail 10, 6 and 55 SMHasher3 tests respectively. Of the hashes above that pass, wifehash is the fastest. Wifehash gains up to 15% efficency over XXH3-128 here (Still 11% deficit for short keys, however). On unaligned input, Wifehash is level with Aquahash, and within 3% of falkhash.

## How it works

Eight 64-bit accumulator lanes are fed 64 bytes at a time with
position-dependent keys and scramble every 1 KiB, then get folded back down 
with the 128-bit mixer and handed to the original finalizer.

Meihash's mixer sends a word pair down two independent xor-mul paths, supplied with
`(P0,P1)` and `(S0,S1)`, and XORs the two 128-bit products. wifehash uses one
path at a time, alternating between them and rotating the constant pair per
lane. This halves the 64x64->128 multiplies issued per 64 bytes, alleviating (some) pressure on the CPU.

The AVX2 portion implements a facsimile of the mixer, accommodated to AVX2's widest multiply.

## Testing

The included [SMHasher3 extra results](results/smhasher3-wifehash.txt) are a full run
against SMHasher3 commit `3b61937`.

## Stability

Please note that wifehash is **not cryptographic**. Do NOT use it for message authentication,
signatures, password storage, or anything where that requires security.

## License

MIT. See the header of `wifehash.h`.
