<div align="center">

# wifehash

**A 128-bit non-cryptographic hash that passes SMHasher3 extra, built for large inputs.**

</div>

---
- **11-31% faster than XXH3-128 on bulk input** on every machine tested: x86 (AVX2) ***and***
  ARM (NEON, Apple M1 and Raspberry Pi 5), and ahead of it at every alignment.
- Passes ***ALL*** 252 tests in [SMHasher3](https://gitlab.com/fwojcik/smhasher3)
  ([full results](results/old/smhasher3-wifehash.txt)).
- AVX2, AArch64 NEON and portable scalar backends, picked at compile time, all bit-identical.
- Seeded, stateful variants (`ws_` / `wsx_`) for hash tables that take untrusted keys.
- C99 or C++11. Needs `<stddef.h>`, `<stdint.h>`, `<string.h>`, and `<immintrin.h>` (AVX2) or `<arm_neon.h>` (AArch64).
- Supports streaming

wifehash is a member of the [MeiHash](https://github.com/ef1500/MeiHash) family; its mixer is meihash's dual-MUM
folding mixer, used one path at a time. See [How it works](#how-it-works).

## Install

Copy `wifehash.h` into your project and include it.

With CMake:

```cmake
find_package(wifehash 1.1 REQUIRED)
target_link_libraries(myapp PRIVATE wifehash::wifehash)
```

or, alternatively:

```cmake
add_subdirectory(external/wifehash)
target_link_libraries(myapp PRIVATE wifehash::wifehash)
```

For AVX2, build with `-mavx2`, `-march=native`, or `/arch:AVX2`.

NEON is on by default on little-endian AArch64. Building with `-mcpu=native` (or any
`-march=armv8.2-a+sha3` target) also lets the scramble use the 3-way `EOR3`.

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
printf("wifehash backend: %s\n", WHX_BACKEND); // avx2, neon, or scalar
```

Seeded hashing with state (for hash tables that take untrusted keys)

```c
static whx_ws_state st;              // 1 KiB table; whx_wsx_state for 256 KiB
ws_wifehash128x_init(&st, seed);     // once per seed (~0.2 us)
ws_wifehash128x(&st, msg, len, h);   // then hash to your delight!
```

## Functions

| Symbol | Description |
|:---|:---|
| `void wifehash128x(const void *in, size_t len, uint64_t seed, void *out)` | The hash. Writes 16 bytes to `out`. Dispatches to the best backend at compile time. |
| `uint64_t wifehash64x(const void *in, size_t len, uint64_t seed)` | The low 64 bits of the above. |
| `void wifehash128x_small(const void *in, size_t len, uint64_t seed, void *out)` | The original scalar implementation of wifehash128. A different function from `wifehash128x` above 192 bytes. |
| `void wifehash128x_ref(...)` | Portable Hash |
| `void wifehash128x_avx2(...)` | AVX2 hash |
| `void wifehash128x_neon(...)` | AArch64 NEON hash |
| `WHX_BACKEND` | Backend currently enabled |
| `void ws_wifehash128x_init(whx_ws_state *st, uint64_t seed)` | Builds a seeded state with a 1 KiB key table. |
| `void ws_wifehash128x(const whx_ws_state *st, const void *in, size_t len, void *out)` | Seeded hash using that state. Writes 16 bytes. |
| `uint64_t ws_wifehash64x(const whx_ws_state *st, const void *in, size_t len)` | The low 64 bits of the above. |
| `void wsx_wifehash128x_init(whx_wsx_state *st, uint64_t seed)` | Builds a seeded state with a 256 KiB key table. |
| `void wsx_wifehash128x(const whx_wsx_state *st, const void *in, size_t len, void *out)` | Seeded hash using that state. Writes 16 bytes. |
| `uint64_t wsx_wifehash64x(const whx_wsx_state *st, const void *in, size_t len)` | The low 64 bits of the above. |
| `void wifehash128x_stream_init(whx_stream *s, uint64_t seed, uint64_t len)` | Starts a stream for `wifehash128x` over `len` bytes in total. |
| `void ws_wifehash128x_stream_init(whx_stream *s, const whx_ws_state *st, uint64_t len)` | The same for `ws_wifehash128x`. The state must outlive the stream. |
| `void wsx_wifehash128x_stream_init(whx_stream *s, const whx_wsx_state *st, uint64_t len)` | The same for `wsx_wifehash128x`. |
| `int wifehash128x_update(whx_stream *s, const void *in, size_t n)` | Feeds `n` bytes in any chunking. Returns -1 (and hashes nothing) if that would exceed `len`. |
| `int wifehash128x_final(const whx_stream *s, void *out)` | Writes the 16-byte digest, identical to the one-shot function over the whole input. Returns -1 if fewer than `len` bytes were fed. |

The streaming API needs the total length up front because wifehash mixes it into its state
before the first 1 KiB block (a file size from `stat`, a `Content-Length`, an mmap length).
`whx_stream` is 384 bytes on 64-bit targets and can live on the stack.

The `ws_`/`wsx_` state is plain read-only data after `_init`, so one state can be shared
across threads. Allocate it 64-byte aligned for best speed (static storage, `aligned_alloc`,
`_aligned_malloc`); `whx_wsx_state` is 256 KiB, so keep it off the stack. Their outputs differ
from `wifehash128x` and from each other above 1 KiB.

Tunable defines

| Macro | Effect |
|:---|:---|
| `WHX_BULK_MIN` | Length at which the bulk (AVX2/NEON/scalar lane) path takes over. Default 192. **Changing this changes the hash.** |
| `WHX_NO_INT128` | Force the use of a portable 32x32 multiply instead of `__int128` / `_umul128`. |
| `WHX_NO_NEON` | Disable the NEON backend on AArch64 and use the scalar one. |
| `WHX_WSX_BLOCKS` | Size of the `wsx_` key table in 1 KiB blocks. Default 256 (256 KiB). **Changing this changes the `wsx_` hash.** |

SMHasher3 verification codes: `wifehash` `0xAA7C087B`, `wifehash.ws` `0xBA1D00B7`,
`wifehash.wsx` `0x32FBEF4A` (identical on AVX2, NEON and scalar).

## Known answers

### `wifehash128x` Test Answers
Digests are printed high word first, as `h[1]` then `h[0]`.

| Input | Seed | `wifehash128x` |
|:---|:---|:---|
| (empty string) | 0 | `112225b27b53457984ca3a03e4d161cc` |
| `hello` | 0 | `99c4ab8dc0fc1e268cd20a19d684087e` |
| `hello` | 1 | `a8d5aa051a7b7cf5fed0c41b84ae479e` |
| `The quick brown fox jumps over the lazy dog` | 0 | `4b8bfb02fcdbfb49a9d363780c5bbbbf` |

---



## Performance

These numbers were recorded with [SMHasher3](https://gitlab.com/fwojcik/smhasher3)'s speed test,
Release builds (`-march=native`), pinned to one core. Bulk is bytes per cycle on 256 KiB inputs;
short keys are cycles per hash for 1-31-byte inputs. The run files are linked above each table.

wifehash is for bulk hashing, not small keys. Note also that XXH3-128 does not pass the full
SMHasher3 suite; wifehash does.

> **Version note:** Older runs are kept in [`results/old`](results/old).

> **Extension-dependent hashes:** hashes marked (AES-NI), (ARM AES) or (VPCLMULQDQ) need an
> instruction-set extension on top of AVX2 or NEON. wifehash uses **only** AVX2 integer
> instructions, so it runs at full speed on every AVX2 CPU (Intel Haswell and later, AMD Zen 1
> and later). VPCLMULQDQ is the 256-bit carryless multiply, found on Intel Ice Lake and Alder
> Lake and later and AMD Zen 3 and later. Without it, PolyXOR-128 and ChainHash-128 fall back to
> 128-bit PCLMUL.
>
> [PolyXOR-128](https://github.com/orlp/polyxor/tree/3123eb6c0ac95b879a46b490ecda1de0184b14bd)
> is Rust only. I ported it to C in order to run it properly.
> [ChainHash-128](https://github.com/thomasahle/chainhash) uses the author's source, I just wired it into SMHasher3.
> PolyXOR128 and ChainHash128 were both run once on each architecture.
> as wifehash. ChainHash-128's author reports that it passes SMHasher3; PolyXOR-128 has not been
> run through the suite here (I presume it passes too).

---

### Comet Lake Results

> **Note:** my laptop's fan is broken on this machine.

Intel Core i7-10750H (Comet Lake, 6 cores, 2.6 GHz base), pinned to one logical CPU at high
priority (`start /affinity`), Windows 11, gcc 14.1, SMHasher3 `3b61937`, Release build
(`-march=native`). Mean of 3 interleaved runs each, with the min-max range in brackets.
([wifehash run](results/old/wifehash_avx2_comet_lake_speed.txt))

| Hash | Output | Bulk (bytes/cycle) | Short keys (cycles/hash, 1-31 B) |
|:---|---:|---:|---:|
| **wifehash** | 128 | **19.24** (18.96-19.40) | 39.07 (38.31-39.82) |
| `ws_wifehash128x` | 128 | 18.90 (18.18-19.41) | 40.05 (38.25-41.89) |
| XXH3-128 | 128 | 17.36 (16.95-17.68) | 21.55 (21.19-22.12) |
| `wsx_wifehash128x` | 128 | 12.60 (12.40-12.91) | 38.77 (37.94-39.70) |

~11% faster than XXH3-128 on bulk, and 10-12% ahead in every round. `wsx_wifehash128x` drops
~35% here: Comet Lake has 256 KiB of L2 per core, so its 256 KiB key table and the input can't
both stay in L2. On CPUs with a small L2, use `ws_wifehash128x`.

---

### Alder Lake Results

Intel Core i7-12700H (Alder Lake), P-core pinned with `taskset`, gcc 15.2, SMHasher3 `3b61937`,
Release build (`-march=native`). Mean of 3 interleaved runs each, with the min-max range in brackets.
The pinned P-core held 4.6-4.7 GHz throughout the wifehash runs.
(wifehash runs: [1](results/wifehash_avx2_alder_lake_run1.txt), [2](results/wifehash_avx2_alder_lake_run2.txt),
[3](results/wifehash_avx2_alder_lake_run3.txt); `ws_` and `wsx_` runs alongside them in `results/`;
[PolyXOR-128](results/polyxor128_avx2_alder_lake_run1.txt), [ChainHash-128](results/chainhash128_avx2_alder_lake_run1.txt))

| Hash | Output | Bulk (bytes/cycle) | Short keys (cycles/hash, 1-31 B) |
|:---|---:|---:|---:|
| falkhash1 (AES-NI) | 128 | 31.68 (31.67-31.69) | 42.15 |
| falkhash2 (AES-NI) | 128 | 31.46 (31.44-31.49) | 42.33 |
| AquaHash (AES-NI) | 128 | 30.05 (30.05-30.06) | 20.37 |
| PolyXOR-128 (VPCLMULQDQ) | 128 | 29.62 | 108.67 |
| **wifehash** | 128 | **28.14** (27.90-28.54) | 40.54 (40.54-40.54) |
| `ws_wifehash128x` | 128 | 27.97 (27.87-28.10) | 39.31 (39.30-39.31) |
| `wsx_wifehash128x` | 128 | 25.86 (25.67-25.96) | 39.26 (39.25-39.26) |
| XXH3-128 | 128 | 23.77 (23.75-23.80) | 21.17 (21.15-21.18) |
| ChainHash-128 (VPCLMULQDQ) | 128 | 23.28 | 74.81 |
| MeowHash (AES-NI) | 128 | 21.08 (21.08-21.08) | 42.06 |

~18% faster than XXH3-128 on bulk and ~33% faster than MeowHash, without AES-NI. Seeding costs
almost nothing here: `ws_wifehash128x` is within 0.6% of the unseeded hash, and
`wsx_wifehash128x` (~8% behind) is still ~9% ahead of XXH3-128. Short keys are still
XXH3-128's ground (~48% fewer cycles). It should be noted that wifehash remains competitive with PolyXOR.

Alder Lake is the best case for the carryless hashes: its 256-bit carry-less multiply issues
about once per cycle, roughly the cost of any other vector op. PolyXOR-128 leads wifehash by ~5%
on 256 KiB inputs. On SMHasher3's second bulk test (262017-262144-byte inputs) it drops to 25.28,
while wifehash holds 28.00, and on short keys it takes ~2.7x wifehash's cycles. ChainHash-128 is
~21% behind wifehash (19.85 on the second bulk test) and takes ~1.8x its cycles on short keys.

The AES-NI hashes lead here on 16-byte-aligned input, and lose 16-26% of their speed when the
data is unaligned. wifehash loses ~2%:

| Hash | Aligned (offset 0) | Unaligned (offsets 1-7) | Drop |
|:---|---:|---:|---:|
| falkhash1 (AES-NI) | 41.14 | 30.33 | -26% |
| falkhash2 (AES-NI) | 36.45 | 30.75 | -16% |
| AquaHash (AES-NI) | 34.99 | 29.35 | -16% |
| PolyXOR-128 (VPCLMULQDQ) | 30.94 | 29.43 | -4.9% |
| **wifehash** | 28.56 | **28.08** | **-1.7%** |
| XXH3-128 | 23.93 | 23.74 | -0.8% |
| ChainHash-128 (VPCLMULQDQ) | 23.96 | 23.18 | -3.3% |
| MeowHash (AES-NI) | 21.41 | 21.03 | -1.8% |

On unaligned input wifehash is within 5% of AquaHash and 7-9% of falkhash, all of which fail
SMHasher3 tests, and ~5% behind PolyXOR-128, which needs VPCLMULQDQ. Of the hashes that need
nothing beyond AVX2, wifehash is the fastest.

---

### Zen 3 Results

AMD EPYC 7763 (Zen 3), a KVM virtual machine on a shared university server, running on one vCPU
with `taskset`, gcc 13.3, SMHasher3 `3b61937`, Release build (`-march=native`). Mean of 3
interleaved runs each, with the min-max range in brackets.
(wifehash runs: [1](results/wifehash_avx2_zen3_epyc_run1.txt), [2](results/wifehash_avx2_zen3_epyc_run2.txt),
[3](results/wifehash_avx2_zen3_epyc_run3.txt); [PolyXOR-128](results/polyxor128_avx2_zen3_epyc_run1.txt),
[ChainHash-128](results/chainhash128_avx2_zen3_epyc_run1.txt))

| Hash | Output | Bulk (bytes/cycle) | Short keys (cycles/hash, 1-31 B) |
|:---|---:|---:|---:|
| falkhash1 (AES-NI) | 128 | 28.62 (28.56-28.66) | 61.53 |
| falkhash2 (AES-NI) | 128 | 27.61 (27.52-27.71) | 62.31 |
| **wifehash** | 128 | **26.23** (26.06-26.43) | 48.02 (47.92-48.21) |
| `ws_wifehash128x` | 128 | 25.51 (25.18-25.85) | 48.03 (47.94-48.20) |
| AquaHash (AES-NI) | 128 | 23.01 (23.01-23.01) | 27.39 (27.32-27.52) |
| XXH3-128 | 128 | 20.07 (20.01-20.11) | 24.43 (23.89-25.23) |
| `wsx_wifehash128x` | 128 | 19.99 (19.92-20.13) | 47.98 (47.96-47.99) |
| MeowHash (AES-NI) | 128 | 18.38 (18.31-18.42) | 45.98 |
| PolyXOR-128 (VPCLMULQDQ) | 128 | 16.03 | 152.57 |
| ChainHash-128 (VPCLMULQDQ) | 128 | 13.26 | 91.15 |

~31% faster than XXH3-128 on bulk, and ~14% faster than AquaHash. Only the two
falkhash versions are faster, and both fail SMHasher3 tests. `ws_wifehash128x` is ~3% behind the unseeded hash;
`wsx_wifehash128x` (~24% behind) is level with XXH3-128.

The carryless hashes fall to the bottom on Zen 3, which issues one 256-bit VPCLMULQDQ every 2
cycles, costing 4x more than `vpmuludq`. Because of that, wifehash is ~64% 
faster than PolyXOR-128 and ~98% faster than ChainHash-128 here.

In throughput terms, wifehash's 26.23 bytes/cycle is ~64 GB/s and XXH3-128's
is ~49 GB/s.

On alignment with Zen 3, XXH3-128 loses 17%, and wifehash loses 5%. wifehash leads XXH3-128 by 16% aligned and
33% unaligned:

| Hash | Aligned (offset 0) | Unaligned (offsets 1-7) | Drop |
|:---|---:|---:|---:|
| falkhash1 (AES-NI) | 28.68 | 28.61 | -0.2% |
| falkhash2 (AES-NI) | 28.50 | 27.49 | -3.5% |
| **wifehash** | 27.36 | **26.07** | -4.7% |
| AquaHash (AES-NI) | 23.01 | 23.01 | -0.0% |
| XXH3-128 | 23.58 | 19.57 | -17.0% |
| MeowHash (AES-NI) | 20.47 | 18.08 | -11.7% |
| PolyXOR-128 (VPCLMULQDQ) | 15.98 | 16.03 | +0.3% |
| ChainHash-128 (VPCLMULQDQ) | 13.46 | 13.23 | -1.7% |

**Zen 3 (again):** AMD Ryzen Threadripper PRO 5955WX, core pinned with `taskset` (held
4.54 GHz throughout), Gentoo, gcc 16.2, same SMHasher3 build. Only the wifehash variants and the
two carry-less hashes were run here. (wifehash runs: [1](results/wifehash_avx2_zen3_threadripper_run1.txt),
[2](results/wifehash_avx2_zen3_threadripper_run2.txt), [3](results/wifehash_avx2_zen3_threadripper_run3.txt);
[PolyXOR-128](results/polyxor128_avx2_zen3_threadripper_run1.txt),
[ChainHash-128](results/chainhash128_avx2_zen3_threadripper_run1.txt))

| Hash | Output | Bulk (bytes/cycle) | Short keys (cycles/hash, 1-31 B) | Bulk (GB/s) |
|:---|---:|---:|---:|---:|
| **wifehash** | 128 | **21.05** (21.03-21.07) | 60.27 (60.24-60.29) | **~84** |
| `ws_wifehash128x` | 128 | 20.48 (20.35-20.61) | 60.50 (60.48-60.51) | ~82 |
| `wsx_wifehash128x` | 128 | 16.46 (15.54-16.99) | 60.37 (60.37-60.37) | ~66 |
| PolyXOR-128 (VPCLMULQDQ) | 128 | 12.69 | 191.20 | ~51 |
| ChainHash-128 (VPCLMULQDQ) | 128 | 10.54 | 105.68 | ~42 |

This chip's timestamp counter runs at ~4.0 GHz, so its bytes/cycle reads lower than the EPYC's
(2.45 GHz counter) even though it is the faster machine: ~84 GB/s against ~64 GB/s. wifehash is
~66% faster than PolyXOR-128 and 2x ChainHash-128 here.

---

### Apple M1 (NEON) Results

Four identical Apple M1 machines, Firestorm core pinned with `taskset`, Asahi Ubuntu 26.04,
gcc 15.2, SMHasher3 `3b61937`, Release build (`-march=native`). Three interleaved rounds per
machine, so each number is the mean of 12 runs, with the min-max range in brackets.
(wifehash runs: [1](results/wifehash_neon_m1_run1.txt) to [12](results/wifehash_neon_m1_run12.txt),
three per machine)

| Hash | Output | Bulk (bytes/cycle) | Short keys (cycles/hash, 1-31 B) |
|:---|---:|---:|---:|
| gxhash (ARM AES) | 128 | 26.65 (26.21-26.88) | 39.04 (38.80-39.35) |
| **wifehash** (NEON) | 128 | **15.50** (15.37-15.59) | 41.38 (41.17-41.47) |
| `ws_wifehash128x` | 128 | 15.33 (15.26-15.40) | 41.27 (41.20-41.34) |
| `wsx_wifehash128x` | 128 | 12.66 (12.61-12.73) | 41.28 (41.15-41.40) |
| XXH3-128 (NEON) | 128 | 12.56 (12.52-12.59) | 30.11 (30.00-30.19) |

~23% faster than XXH3-128 on bulk, wifehash's slowest run
(15.37) beats XXH3-128's fastest (12.59). That is ~46 GB/s against ~37.5 GB/s at the M1's
2.98 GHz. 

Throughput is the same at every alignment (0-7). `ws_wifehash128x` is ~1% behind the
unseeded hash; `wsx_wifehash128x` is ~18% behind and level with XXH3-128. gxhash is faster on bulk,
but needs AES and fails 25 SMHasher3 tests. Short keys use the scalar small path, which takes
~37% more cycles than XXH3-128 on M1.

wifehash also passes SMHasher3's PerlinNoise test (`--extra`) on M1
([results](results/old/wifehash_neon_m1_perlin.txt)), which XXH3-128 fails.

---

### Raspberry Pi 5 (Cortex-A76) Results

Four Raspberry Pi 5 boards (Cortex-A76, 2.4 GHz), core pinned with `taskset`, Ubuntu 26.04,
gcc 15.2, SMHasher3 `3b61937`, Release build (`-march=native`). Three interleaved rounds per
board, so each number is the mean of 12 runs, with the min-max range in brackets.
(wifehash runs: [1](results/wifehash_neon_a76_run1.txt) to [12](results/wifehash_neon_a76_run12.txt),
three per board)

| Hash | Output | Bulk (bytes/cycle) | Short keys (cycles/hash, 1-31 B) |
|:---|---:|---:|---:|
| gxhash (ARM AES) | 128 | 9.52 (9.40-9.72) | 47.23 (45.91-55.64) |
| **wifehash** (NEON) | 128 | **6.61** (6.52-6.68) | 59.28 (56.09-59.66) |
| `ws_wifehash128x` | 128 | 6.53 (6.49-6.62) | 59.33 (59.22-59.39) |
| XXH3-128 (NEON) | 128 | 5.85 (5.80-5.90) | 39.91 (39.84-40.01) |
| `wsx_wifehash128x` | 128 | 5.60 (5.49-5.68) | 59.30 (59.14-59.37) |

~13% faster than XXH3-128 on bulk, in every run (wifehash's slowest, 6.52, beats XXH3-128's
fastest, 5.90).On 16-byte-aligned input wifehash reaches 7.72 bytes/cycle (6.45 at offsets 1-7).
`ws_wifehash128x` is ~1% behind the unseeded hash, `wsx_wifehash128x` ~15%. gxhash is faster, but
needs AES and fails 25 SMHasher3 tests.

---

### Streaming API vs XXH3-128 streaming

`wifehash128x_stream_init` / `_update` / `_final` against XXH3-128's streaming API
(`XXH3_128bits_reset_withSeed` / `_update` / `_digest`, the official `xxhash.h` v0.8.3 with
`XXH_INLINE_ALL`). Both hashes were compiled into the same binary with the same flags
(`-O2 -march=native`, or `-mcpu=native` on ARM) and fed identically, pinned to one core on the
machines above. Throughput is in GB/s from the wall clock, so unlike the cycle tables above,
these numbers can be compared across machines. Each figure is the best of 5 interleaved runs.

- **In cache:** 256 MiB fed as fixed-size updates, cycling over a 256 KiB buffer.
- **DRAM:** 256 MiB of fresh addresses from a 512 MiB buffer, so the data comes from memory.
- **File:** a 512 MiB file in the page cache, either `read()` into a 256 KiB buffer and fed
  per read, or `mmap`'d with `MADV_SEQUENTIAL` and fed in 1 MiB updates.

wifehash's lead over XXH3-128:

| Machine | Data | 16 B | 64 B | 256 B | 1 KiB | 4 KiB | 64 KiB | 1 MiB |
|:---|:---|---:|---:|---:|---:|---:|---:|---:|
| Alder Lake | in cache | +27% | +31% | +129% | +60% | +28% | +18% | +17% |
| | DRAM | +30% | +12% | +41% | +35% | +24% | +16% | +15% |
| Zen 3 EPYC | in cache | +58% | +52% | +61% | +41% | +28% | +22% | +11% |
| | DRAM | +57% | +44% | +17% | +6% | +9% | +4% | +4% |
| Zen 3 Threadripper | in cache | +44% | +39% | +72% | +36% | +19% | +13% | +5% |
| | DRAM | +43% | +34% | +22% | +12% | +6% | +5% | +4% |
| Raspberry Pi 5 | in cache | +59% | +42% | +52% | +67% | +53% | +47% | +47% |
| | DRAM | +61% | +42% | +51% | +63% | +30% | +19% | +16% |
| Apple M1 | in cache | +58% | +78% | +91% | +49% | +37% | +33% | +33% |
| | DRAM | +58% | +77% | +90% | +48% | +37% | +32% | +31% |

Page-cached file, GB/s:

| Machine | `read()`: wifehash | `read()`: XXH3-128 | `mmap`: wifehash | `mmap`: XXH3-128 |
|:---|---:|---:|---:|---:|
| Alder Lake | **11.20** | 10.84 | **14.16** | 13.23 |
| Zen 3 EPYC | **9.52** | 9.45 | 6.74 | **6.85** |
| Zen 3 Threadripper | **11.38** | 11.21 | 9.28 | **9.72** |
| Raspberry Pi 5 | **4.75** | 4.24 | **5.78** | 5.37 |
| Apple M1 | **16.97** | 15.38 | **26.79** | 22.28 |

wifehash's streaming API beats XXH3-128's at every update size, in cache and from DRAM, on all
five machines. Through a file, the kernel's copy (`read()`) or the page mapping (`mmap`) sets the
pace and the two hashes are close. The one case where XXH3-128 leads is `mmap` on Zen 3 (2-5%).
That gap shows up with wifehash's prefetch on or off, and its cause is unknown. A file that has
to come off the disk is limited by the disk for either hash; that case was not measured, because
emptying the page cache needs root.

Throughput, GB/s, by update size (runs:
[Alder Lake](results/stream_vs_xxh3_avx2_alder_lake_run1.txt),
[Zen 3 EPYC](results/stream_vs_xxh3_avx2_zen3_epyc_run1.txt),
[Zen 3 Threadripper](results/stream_vs_xxh3_avx2_zen3_threadripper_run1.txt),
[Raspberry Pi 5](results/stream_vs_xxh3_neon_a76_run1.txt),
[Apple M1](results/stream_vs_xxh3_neon_m1_run1.txt)):

<details>
<summary>Alder Lake (i7-12700H, gcc 15.2, prefetch 2048)</summary>

| | 16 B | 64 B | 256 B | 1 KiB | 4 KiB | 64 KiB | 1 MiB |
|:---|---:|---:|---:|---:|---:|---:|---:|
| wifehash, in cache | **4.99** | **18.44** | **41.00** | **56.22** | **70.81** | **76.89** | **76.78** |
| XXH3-128, in cache | 3.93 | 14.04 | 17.87 | 35.17 | 55.14 | 65.28 | 65.56 |
| wifehash, DRAM | **4.97** | **13.96** | **19.72** | **27.56** | **27.63** | **27.84** | **27.96** |
| XXH3-128, DRAM | 3.82 | 12.42 | 13.99 | 20.39 | 22.28 | 24.04 | 24.23 |

</details>

<details>
<summary>Zen 3 EPYC (EPYC 7763 VM, gcc 13.3, prefetch 512)</summary>

| | 16 B | 64 B | 256 B | 1 KiB | 4 KiB | 64 KiB | 1 MiB |
|:---|---:|---:|---:|---:|---:|---:|---:|
| wifehash, in cache | **3.97** | **13.84** | **30.81** | **50.62** | **60.15** | **64.35** | **59.24** |
| XXH3-128, in cache | 2.52 | 9.10 | 19.08 | 35.85 | 47.10 | 52.95 | 53.54 |
| wifehash, DRAM | **3.92** | **12.14** | **16.28** | **17.63** | **18.62** | **19.37** | **19.95** |
| XXH3-128, DRAM | 2.50 | 8.41 | 13.88 | 16.56 | 17.13 | 18.67 | 19.20 |

</details>

<details>
<summary>Zen 3 Threadripper (Threadripper PRO 5955WX, gcc 16.2, prefetch 512)</summary>

| | 16 B | 64 B | 256 B | 1 KiB | 4 KiB | 64 KiB | 1 MiB |
|:---|---:|---:|---:|---:|---:|---:|---:|
| wifehash, in cache | **5.38** | **17.93** | **41.93** | **64.64** | **77.92** | **83.61** | **76.68** |
| XXH3-128, in cache | 3.75 | 12.87 | 24.31 | 47.49 | 65.45 | 74.09 | 73.05 |
| wifehash, DRAM | **5.34** | **15.90** | **22.46** | **24.95** | **25.98** | **25.99** | **25.96** |
| XXH3-128, DRAM | 3.74 | 11.86 | 18.43 | 22.36 | 24.59 | 24.85 | 24.90 |

</details>

<details>
<summary>Raspberry Pi 5 (Cortex-A76, gcc 15.2, no prefetch)</summary>

| | 16 B | 64 B | 256 B | 1 KiB | 4 KiB | 64 KiB | 1 MiB |
|:---|---:|---:|---:|---:|---:|---:|---:|
| wifehash, in cache | **2.28** | **5.59** | **8.42** | **14.75** | **17.01** | **17.85** | **18.02** |
| XXH3-128, in cache | 1.43 | 3.94 | 5.54 | 8.83 | 11.11 | 12.14 | 12.24 |
| wifehash, DRAM | **2.28** | **5.61** | **8.45** | **14.13** | **14.14** | **14.14** | **14.14** |
| XXH3-128, DRAM | 1.42 | 3.96 | 5.59 | 8.67 | 10.85 | 11.84 | 12.15 |

</details>

<details>
<summary>Apple M1 (Firestorm, gcc 15.2, no prefetch)</summary>

| | 16 B | 64 B | 256 B | 1 KiB | 4 KiB | 64 KiB | 1 MiB |
|:---|---:|---:|---:|---:|---:|---:|---:|
| wifehash, in cache | **5.32** | **16.30** | **29.63** | **36.87** | **43.12** | **45.60** | **45.96** |
| XXH3-128, in cache | 3.36 | 9.14 | 15.54 | 24.78 | 31.49 | 34.31 | 34.59 |
| wifehash, DRAM | **5.31** | **16.15** | **29.21** | **36.37** | **42.44** | **44.40** | **44.41** |
| XXH3-128, DRAM | 3.36 | 9.12 | 15.41 | 24.50 | 30.88 | 33.65 | 33.92 |

</details>

The "prefetch" figure is the `WHX_STREAM_PREFETCH` distance the build picked (see
[How it works](#how-it-works)). XXH3-128 does not need the total length up front, and
wifehash's streaming API does; see [Functions](#functions).

## How it works

Eight 64-bit accumulator lanes are fed 64 bytes at a time with
position-dependent keys and scramble every 1 KiB, then get folded back down 
with the 128-bit mixer and handed to the original finalizer.

Meihash's mixer sends a word pair down two independent xor-mul paths, supplied with
`(P0,P1)` and `(S0,S1)`, and XORs the two 128-bit products. wifehash uses one
path at a time, alternating between them and rotating the constant pair per
lane. This halves the 64x64 (128-bit result) multiplies issued per 64 bytes, alleviating (some) pressure on the CPU.

The AVX2 portion implements a facsimile of the mixer, accommodated to AVX2's widest multiply.

NEON has no even-by-odd 32-bit multiply like
`mul_epu32`, so `UZP1`/`UZP2` is used to feed `UMLAL`/`UMLAL2` directly. The product is accumulated into the cross-half data vector, so each accumulator is basically a single add per stripe, so no laneswapping is needed. Still beats XXH3-128 :-)

Since 1.1.1 the NEON bulk loop is hand-scheduled inline asm (GCC/Clang). Each 1 KiB block runs
as a 3-stage software pipeline (load stripe s+2, xor/unzip stripe s+1, multiply/add stripe s),
which narrow cores like the Cortex-A76 can't find on their own. On 4-byte-aligned input (any
input on Apple cores, see `WHX_NEON_WIDE`), a hybrid block puts lanes 0 and 1 on the otherwise
idle scalar integer units (`UMADDL`), and NEON takes lanes 2-7, unzipping lanes 2-3 of two
stripes at once. Misaligned 16-byte loads cost the A76 double, so misaligned input stays on the
all-NEON block there. Both blocks compute the same function. MSVC keeps the intrinsics loop.

The streaming API hashes each 64-byte stripe in place, straight from the caller's buffer, as
soon as it arrives. A stripe's work depends only on its position (its key is its index in the
1 KiB block, and the block is scrambled after its 16th stripe), never on what comes after it, so
only a partial stripe (under 64 bytes) is ever copied. Stripes that don't finish a block are
hashed inline in `update()` with no function call, and partial stripes are copied with two
overlapping fixed-size moves instead of a `memcpy` call. Bigger updates make one call that runs
whole blocks through the same kernels as the one-shot hash, including the NEON assembly. On x86
the stream also prefetches one cache line per stripe, `WHX_STREAM_PREFETCH` bytes ahead,
because the hardware prefetcher stops at 4 KiB page boundaries and a software prefetch doesn't.
The best distance depends on the core: Golden Cove (Alder / Raptor Lake) builds use 2048 bytes,
and every other AVX2 build uses 512. ARM builds don't prefetch, because there it cost 8-66%.

## Testing

The included [SMHasher3 extra results](results/old/smhasher3-wifehash.txt) are a full run
against SMHasher3 commit `3b61937`.

The seeded variants have their own `--extra` runs on Alder Lake:
[`ws_wifehash128x`](results/old/wifehash_ws_alder_lake_extra.txt) passes all 252 tests.
[`wsx_wifehash128x`](results/old/wifehash_wsx_alder_lake_extra.txt) passed every test it ran, with
zero failures, but the run was stopped in the last section (Seed Bitflip): every reseed rebuilds
its 256 KiB table, and on a shared server that section alone would have taken ~11 more hours.

These full runs were made before 1.1.1. 1.1.1 produces the same output for every input, so
the pass/fail results carry over unchanged.

> **Note on speed numbers in `./results`:** some of these runs were made on shared university
> servers, and the full-suite runs were spread across all cores to finish faster. The Speed
> sections inside those files can therefore read lower than the hash's real throughput (the
> Alder Lake seeded runs show ~20 bytes/cycle bulk). The speed tables in this README come from
> dedicated runs pinned to one idle core, and those are the numbers to use. The pass/fail
> results in the files are unaffected.

### SMHasher3 inclusion

wifehash is not part of SMHasher3's official hash list. As of November 17, 2026:

- I have reached out to SMHasher3's maintainer, Frank Wojcik, multiple times, including
  personally by email, with no response.
- I am barred from submitting merge requests to the SMHasher3 repository. No reason was given:

  ![Merge request submission barred](https://files.catbox.moe/hc1wdb.png)

- I then submitted the merge request by email, with the patch attached, following every rule in
  SMHasher3's `CONTRIBUTING.md`. I sent it four times, and each attempt was met with the same
  message:

  ![Four "Add Wifehash" merge request emails to SMHasher3, each with the patch attached](https://files.catbox.moe/3r5pg3.png)

- As a last resort I opened an [issue](https://gitlab.com/fwojcik/smhasher3/-/work_items/93)
  in early November 2026. It has gone unanswered.

Until wifehash is added upstream, every result in `./results` comes from SMHasher3 commit
`3b61937` with wifehash registered locally, so anyone can reproduce them the same way.

To make up for the missing upstream listing, I tested on every device at my disposal:

| CPU | Cores | wifehash backend | OS / compiler |
|:---|:---|:---|:---|
| Intel Core i7-10750H (Comet Lake), 2.6 GHz base (broken fan) | 6 cores / 12 threads | AVX2 | Windows 11, gcc 14.1 |
| Intel Core Ultra 7 155H (Meteor Lake), up to 4.8 GHz | 6 P-cores + 8 E-cores + 2 LP E-cores / 22 threads | AVX2 | Windows 11, gcc 14.1 |
| Intel Core i7-12700H (Alder Lake), up to 4.7 GHz | 6 P-cores + 8 E-cores / 20 threads | AVX2 | Ubuntu, gcc 15.2 |
| AMD EPYC 7763 (Zen 3), KVM virtual machine (university server) | 16 vCPUs | AVX2 | Ubuntu 24.04, gcc 13.3 |
| AMD Ryzen Threadripper PRO 5955WX (Zen 3), up to 4.5 GHz (university workstation) | 16 cores / 32 threads | AVX2 | Gentoo, gcc 16.2 |
| 4x Raspberry Pi 5 (university machines) | 4 Cortex-A76 (2.4 GHz) | NEON | Ubuntu 26.04, gcc 15.2 |
| 4x Apple M1 (university machines) | 4 Firestorm (2.98 GHz) + 4 Icestorm | NEON | Asahi Ubuntu 26.04, gcc 15.2 |

## Stability

Please note that wifehash is **not cryptographic**. Do NOT use it for message authentication,
signatures, password storage, or anything where that requires security.

## License

MIT. See the header of `wifehash.h`.

## HashDoS warning

`wifehash128x` is **not HashDoS-resistant**. Collisions can be constructed that hold for
**every seed**, so seeding does not protect a hash table that takes keys from untrusted
input (network requests, user-supplied strings, file contents from others).

Use `wifehash128x` where inputs are not chosen by an attacker (checksums, deduplication,
caches of trusted data).

Use seeded `ws_wifehash128x` / `wsx_wifehash128x` otherwise. `ws_wifehash128x` uses a 1KB table, and `wsx_wifehash128x` uses a 256KB table. The former is as fast as `wifehash128x` (within 3% on every machine tested). The latter costs
8-24% on large inputs, and up to 35% on CPUs with only 256 KiB of L2 per core, so prefer
`ws_wifehash128x` unless you want the bigger table.
