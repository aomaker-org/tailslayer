#!/usr/bin/env python3
# Path: clouds-labs/labs/tailslayer/esp32/esp32_tailslayer_sim.py
# Purpose: Cycle-accurate ESP32-S3 PSRAM DRAM Refresh vs SRAM Hedged Read Sim
# Max Column: 80 Columns
# ==============================================================================

import random
import sys
import math

CPU_FREQ_MHZ = 240.0
CYCLE_NS = 1000.0 / CPU_FREQ_MHZ  # ~4.167 ns per cycle
PROBES = 500000

# Timing parameters (in CPU cycles at 240MHz)
# Internal SRAM: 2 cycles deterministic
SRAM_BASE_CYCLES = 2

# External PSRAM (Octal SPI @ 80MHz with DCACHE miss)
PSRAM_BASE_CYCLES = 28  # ~116 ns base read access
PSRAM_REFRESH_STALL = 45  # ~187 ns refresh collision latency penalty
PSRAM_REFRESH_INTERVAL_US = 15.6  # APMemory tREFI refresh period (~3,744 cycles)
PSRAM_REFRESH_DURATION_CYCLES = 36  # burst refresh window


def simulate_esp32_memory():
    print("========================================================================")
    print("🔬 ESP32-S3 TAILSLAYER DUAL-CORE HARDWARE SIMULATION")
    print(f"   CPU Clock    : {CPU_FREQ_MHZ:.1f} MHz (1 cycle = {CYCLE_NS:.3f} ns)")
    print(f"   Sim Probes   : {PROBES:,}")
    print(f"   Memory Types : Internal SRAM vs Octal PSRAM (APMemory APS6408)")
    print("========================================================================")

    # 1. Simulate SRAM Baseline
    sram_latencies = [SRAM_BASE_CYCLES + (1 if random.random() < 0.001 else 0) for _ in range(PROBES)]

    # 2. Simulate Single-Core External PSRAM
    psram_single = []
    cycle_timeline = 0
    stalls = 0

    refresh_period_cycles = int(PSRAM_REFRESH_INTERVAL_US * CPU_FREQ_MHZ)

    for _ in range(PROBES):
        # Time progression between read attempts (random 20-50 cycles)
        cycle_timeline += random.randint(20, 50)
        in_refresh = (cycle_timeline % refresh_period_cycles) < PSRAM_REFRESH_DURATION_CYCLES

        lat = PSRAM_BASE_CYCLES + random.randint(0, 3)
        if in_refresh:
            lat += PSRAM_REFRESH_STALL
            stalls += 1
        psram_single.append(lat)

    # 3. Simulate Dual-Core Hedged PSRAM Read (Core 0 vs Core 1)
    # Replicas on 2 interleaved SPI banks / staggered refresh phase
    hedged_latencies = []
    hedged_stalls = 0
    phase_offset = refresh_period_cycles // 2

    for i in range(PROBES):
        timeline = i * 35
        # Core 0 read
        in_ref_c0 = (timeline % refresh_period_cycles) < PSRAM_REFRESH_DURATION_CYCLES
        lat_c0 = PSRAM_BASE_CYCLES + random.randint(0, 3) + (PSRAM_REFRESH_STALL if in_ref_c0 else 0)

        # Core 1 read (staggered bank / uncorrelated phase)
        in_ref_c1 = ((timeline + phase_offset) % refresh_period_cycles) < PSRAM_REFRESH_DURATION_CYCLES
        lat_c1 = PSRAM_BASE_CYCLES + random.randint(0, 3) + (PSRAM_REFRESH_STALL if in_ref_c1 else 0)

        # Hedged Winner: whichever responds first
        winner = min(lat_c0, lat_c1)
        if winner >= (PSRAM_BASE_CYCLES + 20):
            hedged_stalls += 1
        hedged_latencies.append(winner)

    # Calculate Percentiles
    def get_stats(data):
        s = sorted(data)
        n = len(s)
        return {
            "p50": s[int(n * 0.50)],
            "p90": s[int(n * 0.90)],
            "p99": s[int(n * 0.99)],
            "p999": s[int(n * 0.999)],
            "max": s[-1],
            "avg": sum(s) / n,
        }

    s_sram = get_stats(sram_latencies)
    s_psram = get_stats(psram_single)
    s_hedged = get_stats(hedged_latencies)

    print("\n📊 MEMORY LATENCY DISTRIBUTIONS (IN CPU CYCLES @ 240MHz)")
    print("-" * 72)
    print(f"{'Memory Configuration':<24} | {'p50':<6} | {'p90':<6} | {'p99':<6} | {'p99.9':<6} | {'Max':<6} | {'Avg':<6}")
    print("-" * 72)
    print(f"{'Internal SRAM':<24} | {s_sram['p50']:<6} | {s_sram['p90']:<6} | {s_sram['p99']:<6} | {s_sram['p999']:<6} | {s_sram['max']:<6} | {s_sram['avg']:<6.1f}")
    print(f"{'External PSRAM (Single)':<24} | {s_psram['p50']:<6} | {s_psram['p90']:<6} | {s_psram['p99']:<6} | {s_psram['p999']:<6} | {s_psram['max']:<6} | {s_psram['avg']:<6.1f}")
    print(f"{'Hedged PSRAM (Dual-Core)':<24} | {s_hedged['p50']:<6} | {s_hedged['p90']:<6} | {s_hedged['p99']:<6} | {s_hedged['p999']:<6} | {s_hedged['max']:<6} | {s_hedged['avg']:<6.1f}")
    print("-" * 72)

    tail_reduction = ((s_psram['p999'] - s_hedged['p999']) / s_psram['p999']) * 100.0
    print(f"\n[+] Tailslayer Hedged Reading Result on Dual-Core ESP32-S3:")
    print(f"    - Single-Core PSRAM p99.9 Latency : {s_psram['p999']} cycles (~{s_psram['p999'] * CYCLE_NS:.1f} ns)")
    print(f"    - Hedged Dual-Core p99.9 Latency  : {s_hedged['p999']} cycles (~{s_hedged['p999'] * CYCLE_NS:.1f} ns)")
    print(f"    - Tail Latency Reduction Rate     : {tail_reduction:.1f}% reduction in 99.9th percentile stalls!")
    print("========================================================================")


if __name__ == "__main__":
    simulate_esp32_memory()
