[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://www.apache.org/licenses/LICENSE-2.0)
[![GitHub stars](https://img.shields.io/github/stars/LaurieWired/tailslayer)](https://github.com/LaurieWired/tailslayer/stargazers)
[![GitHub forks](https://img.shields.io/github/forks/LaurieWired/tailslayer)](https://github.com/LaurieWired/tailslayer/network/members)
[![GitHub contributors](https://img.shields.io/github/contributors/LaurieWired/tailslayer)](https://github.com/LaurieWired/tailslayer/graphs/contributors)
[![Follow @lauriewired](https://img.shields.io/twitter/follow/lauriewired?style=social)](https://twitter.com/lauriewired)

<img width="2490" height="1148" alt="tailslayer3" src="https://github.com/user-attachments/assets/35d6cd98-ab9d-4ea6-8804-dff3a1b8698b" />

# Tailslayer

Tailslayer is a C++ library that reduces tail latency in RAM reads caused by DRAM refresh stalls. 

It replicates data across multiple, independent DRAM channels with uncorrelated refresh schedules, using (undocumented!) channel scrambling offsets that works on AMD, Intel, and Graviton. Once the request comes in, Tailslayer issues hedged reads across all replicas, allowing the work to be performed on whichever result responds first.


<img width="4986" height="2796" alt="cross_platform_nway" src="https://github.com/user-attachments/assets/4b4a5614-00e4-4845-8a4b-f4adecef5b4d" />

## Usage

The library code is available in [hedged_reader.cpp](https://github.com/LaurieWired/tailslayer/blob/main/include/tailslayer/hedged_reader.hpp) and the example using the library can be found in [tailslayer_example.cpp](https://github.com/LaurieWired/tailslayer/blob/main/tailslayer_example.cpp). To use it, copy `include/tailslayer` into your project and `#include <tailslayer/hedged_reader.hpp>`. The library currently works with two channels (updates to come!), but full N-way usage is available in the [benchmark](https://github.com/LaurieWired/tailslayer/tree/main/discovery/benchmark).

You provide the value type and two functions as template parameters:

1. **Signal function**: Add the loop that waits for the external signal. This determines when to read. Return the desired index to read, and the read immediately fires.
2. **Final work function**: This receives the value immediately after it is read. Add the desired value processing code here.

```cpp
#include <tailslayer/hedged_reader.hpp>

[[gnu::always_inline]] inline std::size_t my_signal() {
    // Wait for your event, then return the index to read
    return index_to_read;
}

template <typename T>
[[gnu::always_inline]] inline void my_work(T val) {
    // Use the value
}

int main() {
    using T = uint8_t;
    tailslayer::pin_to_core(tailslayer::CORE_MAIN);

    tailslayer::HedgedReader<T, my_signal, my_work<T>> reader{};
    reader.insert(0x43);
    reader.insert(0x44);
    reader.start_workers();
}
```

Arguments can be passed to either function via `ArgList`:

```cpp
tailslayer::HedgedReader<T, my_signal, my_work<T>,
    tailslayer::ArgList<1, 2>,   // args to signal function
    tailslayer::ArgList<2>       // args to final work function
> reader{};
```

You can also optionally pass in a different channel offset, channel bit, and number of replicas to the constructor. *Note:* Each insert copies the element N times where N is the number of replicas. It does the address calculation work on the backend, allowing tailslayer to act as a hedged vector that uses logical indices. Additionally, each replica is pinned to a separate core, and will spin on that core according to the signal function until the read happens.

## Extreme Strictness Verification & Artifact Retention

Tailslayer includes an extreme compilation mode (`tailslayer_extreme.c`) enforcing zero-tolerance compiler flags (`-Wall -Wextra -Werror -Wpedantic -Wconversion -Wshadow -Wcast-align -Wpointer-arith -Wwrite-strings -O3 -fno-omit-frame-pointer -fverbose-asm -save-temps=obj`).

All compilation intermediates (`.s`, `.i`, `.o`), disassembly (`objdump -d`), symbols (`nm -C`), ELF sections (`readelf -S`), CPU info (`lscpu`), memory info (`/proc/meminfo`), raw spikes (`raw_spikes.csv`), and JSON telemetry (`telemetry.json`) are preserved.

```bash
make tailslayer-extreme
```

## ESP32-S3 Embedded Dual-Core Probe

The `esp32/` directory provides hardware-ready firmware for Xtensa LX7 dual-core microcontrollers:

- `esp32/esp32_tailslayer_probe.ino`: Arduino/ESP-IDF dual-core firmware issuing hedged reads across internal SRAM and Octal SPI PSRAM (APMemory APS6408).
- `esp32/esp32_tailslayer_sim.py`: Cycle-accurate hardware simulator modeling tREFI refresh bursts and tail-latency reduction.

```bash
make tailslayer-esp32-sim
```

## Rust Implementation (`tailslayer-rs`)

A pure, memory-safe Rust port of Tailslayer is located in [`rust/`](rust/):

- `allocator.rs`: Dual-channel memory allocator with 2MB Hugepages (`MAP_HUGETLB`) and `mlock` page pinning.
- `arch.rs`: Architecture-specific cycle counters (`_rdtsc` on x86_64, `cntvct_el0` on aarch64) and cache flush (`clflush` / `dc civac`).
- `probe.rs`: DRAM refresh jitter spike detector and percentiles ledger.

```bash
make tailslayer-rust-build
make tailslayer-rust-probe
```

