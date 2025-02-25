# `inferb` - Cache Timing Side-Channel Demo

[![Operating Systems](https://img.shields.io/badge/os-macOS-yellow?style=for-the-badge)](https://github.com/ArchAdept/inferb)
[![Architectures](https://img.shields.io/badge/arch-arm64-purple?style=for-the-badge)](https://github.com/ArchAdept/inferb)
[![License](https://img.shields.io/github/license/archadept/inferb?label=license&style=for-the-badge)](https://github.com/ArchAdept/inferb/blob/main/LICENSE.md)

Small example program demonstrating how cache timing side-channels can be used to infer
the contents of memory.

## Prerequisites

Currently only supports Apple Silicon Macs.

Porting to other platforms will require reimplementing `startTimer()` and `timeElapsed()`
using that operating system's high resolution timer API.

Porting to other architectures will require reimplementing `flush_cache_relaxed()` using
that architecture's cache maintenance instructions.

Pull requests welcome 🙂

## Usage

Simply compile using `clang` or `gcc` and run:

```shell
❯ clang -O2 main.c -o inferb && ./inferb
✅ Inferred value >>>  42 <<< in 0.0129 seconds (spacing=0x4000, 20 iterations, 50 retries)
```

## Tuning

The following `#define`'s are available to tune the behaviour of the program:

### `VALUE`

The value to place in memory.

### `SPACING`

How far apart to space each cache line in the probe array; default 16 KiB to match the
macOS page size.

Increasing this value results in better accuracy, at the cost of a slower runtime.

### `ITERATIONS`

How many times to probe each cache line when generating an average access time; default
20.

Increasing this value significantly improves accuracy, at the cost of a significantly
slower runtime.

### `RETRIES_IF_ZERO`

As outlined in [the original Meltdown paper](https://meltdownattack.com/meltdown.pdf),
CPUs tend to have an inherent bias towards using value zero as a placeholder value. With
this in mind, we retry results of zero up to this many times (default 50) to be
confident that the byte in memory really does have value zero.

