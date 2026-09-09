/*
 * Path: clouds-labs/labs/tailslayer/tailslayer_extreme.c
 * Purpose: Extreme-strictness, cross-platform DRAM jitter probe & telemetry
 * Supported Architectures: x86_64 (AMD EPYC, Intel Xeon) & aarch64 (Ampere Altra)
 * Max Column: 80 Columns
 * ============================================================================
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <unistd.h>
#include <math.h>
#include <sys/mman.h>
#include <sched.h>
#include <getopt.h>
#include <time.h>
#include <errno.h>

#define HUGEPAGE_2M       (1ULL << 21)
#define CALIB_PROBES      500000
#define MAX_SPIKES        2000000
#define DEFAULT_PROBES    2000000
#define DEFAULT_TREFI_US  7.8

/* Architecture-specific cycle counters and cache flush barriers */
#if defined(__x86_64__) || defined(__i386__)
static inline uint64_t rdtsc_lfence(void)
{
    uint64_t lo, hi;
    asm volatile("lfence\n\t"
                 "rdtsc"
                 : "=a"(lo), "=d"(hi));
    return (hi << 32) | lo;
}

static inline uint64_t rdtscp_lfence(void)
{
    uint64_t lo, hi;
    uint32_t aux;
    asm volatile("rdtscp"
                 : "=a"(lo), "=d"(hi), "=c"(aux));
    asm volatile("lfence" ::: "memory");
    return (hi << 32) | lo;
}

static inline void clflush_addr(volatile void *addr)
{
    asm volatile("clflush (%0)" :: "r"(addr) : "memory");
}

static inline void mfence_inst(void)
{
    asm volatile("mfence" ::: "memory");
}

static inline void lfence_inst(void)
{
    asm volatile("lfence" ::: "memory");
}

#elif defined(__aarch64__)
static inline uint64_t rdtsc_lfence(void)
{
    uint64_t val;
    asm volatile("isb; mrs %0, cntvct_el0" : "=r"(val));
    return val;
}

static inline uint64_t rdtscp_lfence(void)
{
    uint64_t val;
    asm volatile("isb; mrs %0, cntvct_el0; isb" : "=r"(val));
    return val;
}

static inline void clflush_addr(volatile void *addr)
{
    asm volatile("dc civac, %0; dsb ish; isb" :: "r"(addr) : "memory");
}

static inline void mfence_inst(void)
{
    asm volatile("dsb ish" ::: "memory");
}

static inline void lfence_inst(void)
{
    asm volatile("isb" ::: "memory");
}
#else
#error "Unsupported target architecture. Requires x86_64 or aarch64."
#endif

/* Calibration for timer frequency in GHz */
static double calibrate_timer_ghz(void)
{
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    uint64_t tsc0 = rdtsc_lfence();

    struct timespec req = { 0, 100000000 }; /* 100ms */
    nanosleep(&req, NULL);

    uint64_t tsc1 = rdtscp_lfence();
    clock_gettime(CLOCK_MONOTONIC, &t1);

    double elapsed_ns = (double)(t1.tv_sec - t0.tv_sec) * 1e9 +
                        (double)(t1.tv_nsec - t0.tv_nsec);
    return (double)(tsc1 - tsc0) / elapsed_ns;
}

static inline uint64_t timed_probe(volatile char *addr)
{
    clflush_addr(addr);
    mfence_inst();
    lfence_inst();
    uint64_t t0 = rdtsc_lfence();
    *(volatile char *)addr;
    uint64_t t1 = rdtscp_lfence();
    return t1 - t0;
}

static int cmp_u64(const void *a, const void *b)
{
    uint64_t va = *(const uint64_t *)a;
    uint64_t vb = *(const uint64_t *)b;
    return (va > vb) - (va < vb);
}

struct spike_record {
    uint64_t tsc;
    uint64_t latency;
};

int main(int argc, char **argv)
{
    int n_probes = DEFAULT_PROBES;
    uint64_t manual_threshold = 0;
    double trefi_us = DEFAULT_TREFI_US;
    double thresh_mult = 2.0;
    const char *csv_path = NULL;
    const char *json_path = NULL;

    static struct option long_opts[] = {
        {"probes",       required_argument, NULL, 'n'},
        {"threshold",    required_argument, NULL, 'T'},
        {"trefi-us",     required_argument, NULL, 't'},
        {"thresh-mult",  required_argument, NULL, 'm'},
        {"csv",          required_argument, NULL, 'c'},
        {"json",         required_argument, NULL, 'j'},
        {"help",         no_argument,       NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "n:T:t:m:c:j:h", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'n': n_probes = atoi(optarg); break;
        case 'T': manual_threshold = (uint64_t)atoll(optarg); break;
        case 't': trefi_us = atof(optarg); break;
        case 'm': thresh_mult = atof(optarg); break;
        case 'c': csv_path = optarg; break;
        case 'j': json_path = optarg; break;
        case 'h':
        default:
            fprintf(stderr,
                    "Usage: %s [opts]\n"
                    "  -n, --probes N       Total probes (default: %d)\n"
                    "  -T, --threshold T    Manual cycle spike threshold\n"
                    "  -t, --trefi-us US    Expected tREFI (default: %.1f)\n"
                    "  -m, --thresh-mult M  Baseline multiplier (default: %.1f)\n"
                    "  -c, --csv PATH       Export raw spikes to CSV\n"
                    "  -j, --json PATH      Export full telemetry to JSON\n",
                    argv[0], DEFAULT_PROBES, DEFAULT_TREFI_US, 2.0);
            return 1;
        }
    }

    if (n_probes <= 0) n_probes = DEFAULT_PROBES;

    /* Calibrate timing */
    double timer_ghz = calibrate_timer_ghz();
    if (timer_ghz <= 0.001) timer_ghz = 2.0; /* fallback */
    uint64_t expected_trefi_cycles = (uint64_t)(trefi_us * 1000.0 * timer_ghz);

    /* Allocate memory page with hugepage fallback */
    char *page = (char *)mmap(NULL, HUGEPAGE_2M, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
    int is_huge = 1;
    if (page == MAP_FAILED) {
        is_huge = 0;
        page = (char *)mmap(NULL, HUGEPAGE_2M, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (page == MAP_FAILED) {
            fprintf(stderr, "[-] mmap allocation failed: %s\n", strerror(errno));
            return 1;
        }
    }
    memset(page, 0xA5, HUGEPAGE_2M);
    mlock(page, HUGEPAGE_2M);

    volatile char *probe_ptr = page + 64; /* offset within page */

    /* Baseline calibration */
    uint64_t *calib = (uint64_t *)malloc(CALIB_PROBES * sizeof(uint64_t));
    if (!calib) {
        fprintf(stderr, "[-] Memory alloc failure for calibration\n");
        return 1;
    }

    for (int i = 0; i < CALIB_PROBES; i++) {
        calib[i] = timed_probe(probe_ptr);
    }
    qsort(calib, CALIB_PROBES, sizeof(uint64_t), cmp_u64);
    uint64_t p50_base = calib[CALIB_PROBES / 2];
    uint64_t p99_base = calib[(int)(CALIB_PROBES * 0.99)];
    free(calib);

    uint64_t threshold = manual_threshold;
    if (threshold == 0) {
        threshold = (uint64_t)((double)p50_base * thresh_mult);
        if (threshold <= p99_base) threshold = p99_base + 10;
    }

    /* Allocate spike record buffer */
    struct spike_record *spikes = (struct spike_record *)malloc(
        MAX_SPIKES * sizeof(struct spike_record));
    if (!spikes) {
        fprintf(stderr, "[-] Failed to allocate spike buffer\n");
        return 1;
    }

    /* Execute Extreme Probe Loop */
    int spike_count = 0;
    uint64_t total_latency_sum = 0;
    uint64_t min_lat = UINT64_MAX;
    uint64_t max_lat = 0;

    struct timespec run_t0, run_t1;
    clock_gettime(CLOCK_MONOTONIC, &run_t0);

    for (int i = 0; i < n_probes; i++) {
        clflush_addr(probe_ptr);
        mfence_inst();
        lfence_inst();
        uint64_t t0 = rdtsc_lfence();
        *(volatile char *)probe_ptr;
        uint64_t t1 = rdtscp_lfence();
        uint64_t delta = t1 - t0;

        if (delta >= threshold && spike_count < MAX_SPIKES) {
            spikes[spike_count].tsc = t0;
            spikes[spike_count].latency = delta;
            spike_count++;
        }
        total_latency_sum += delta;
        if (delta < min_lat) min_lat = delta;
        if (delta > max_lat) max_lat = delta;
    }

    clock_gettime(CLOCK_MONOTONIC, &run_t1);
    double elapsed_sec = (double)(run_t1.tv_sec - run_t0.tv_sec) +
                         (double)(run_t1.tv_nsec - run_t0.tv_nsec) * 1e-9;

    /* Compute Percentiles on Spikes */
    uint64_t spike_p50 = 0, spike_p90 = 0, spike_p99 = 0, spike_max = 0;
    double spike_mean = 0.0, spike_stddev = 0.0;

    if (spike_count > 0) {
        uint64_t *spike_lats = (uint64_t *)malloc((size_t)spike_count * sizeof(uint64_t));
        if (spike_lats) {
            double sum = 0.0;
            for (int i = 0; i < spike_count; i++) {
                spike_lats[i] = spikes[i].latency;
                sum += (double)spikes[i].latency;
            }
            qsort(spike_lats, (size_t)spike_count, sizeof(uint64_t), cmp_u64);
            spike_p50 = spike_lats[spike_count / 2];
            spike_p90 = spike_lats[(int)((double)spike_count * 0.90)];
            spike_p99 = spike_lats[(int)((double)spike_count * 0.99)];
            spike_max = spike_lats[spike_count - 1];
            spike_mean = sum / (double)spike_count;

            double sq_sum = 0.0;
            for (int i = 0; i < spike_count; i++) {
                double diff = (double)spikes[i].latency - spike_mean;
                sq_sum += diff * diff;
            }
            spike_stddev = sqrt(sq_sum / (double)spike_count);
            free(spike_lats);
        }
    }

    /* Print Console Telemetry Summary */
    printf("========================================================================\n");
    printf("⚡ TAILSLAYER EXTREME TELEMETRY REPORT\n");
    printf("========================================================================\n");
    printf("Hardware Timer Freq : %.4f GHz\n", timer_ghz);
    printf("Memory Page Mode    : %s (2MB page locked)\n", is_huge ? "HUGETLB_2MB" : "STANDARD_MMAP");
    printf("Expected tREFI      : %.2f us (~%" PRIu64 " cycles)\n", trefi_us, expected_trefi_cycles);
    printf("Baseline (p50/p99)  : %" PRIu64 " / %" PRIu64 " cycles\n", p50_base, p99_base);
    printf("Spike Threshold     : %" PRIu64 " cycles\n", threshold);
    printf("------------------------------------------------------------------------\n");
    printf("Total Probes Run    : %d\n", n_probes);
    printf("Execution Duration  : %.4f seconds (%.2f Kprobes/sec)\n",
           elapsed_sec, (double)n_probes / (elapsed_sec * 1000.0));
    printf("Detected Spikes     : %d (%.3f%% stall rate)\n",
           spike_count, ((double)spike_count / (double)n_probes) * 100.0);
    printf("All-Probes Min/Max  : %" PRIu64 " / %" PRIu64 " cycles\n", min_lat, max_lat);
    printf("Spike Distribution  :\n");
    printf("  - p50 Latency     : %" PRIu64 " cycles\n", spike_p50);
    printf("  - p90 Latency     : %" PRIu64 " cycles\n", spike_p90);
    printf("  - p99 Latency     : %" PRIu64 " cycles\n", spike_p99);
    printf("  - Max Spike Lat   : %" PRIu64 " cycles\n", spike_max);
    printf("  - Mean +/- StdDev : %.1f +/- %.1f cycles\n", spike_mean, spike_stddev);
    printf("========================================================================\n");

    /* Export CSV if requested */
    if (csv_path) {
        FILE *fcsv = fopen(csv_path, "w");
        if (fcsv) {
            fprintf(fcsv, "timestamp_tsc,latency_cycles\n");
            for (int i = 0; i < spike_count; i++) {
                fprintf(fcsv, "%" PRIu64 ",%" PRIu64 "\n", spikes[i].tsc, spikes[i].latency);
            }
            fclose(fcsv);
        }
    }

    /* Export JSON if requested */
    if (json_path) {
        FILE *fjson = fopen(json_path, "w");
        if (fjson) {
            fprintf(fjson, "{\n"
                           "  \"timer_ghz\": %.4f,\n"
                           "  \"page_mode\": \"%s\",\n"
                           "  \"total_probes\": %d,\n"
                           "  \"elapsed_seconds\": %.4f,\n"
                           "  \"spike_count\": %d,\n"
                           "  \"spike_rate_pct\": %.4f,\n"
                           "  \"baseline_p50\": %" PRIu64 ",\n"
                           "  \"baseline_p99\": %" PRIu64 ",\n"
                           "  \"threshold_cycles\": %" PRIu64 ",\n"
                           "  \"spike_p50\": %" PRIu64 ",\n"
                           "  \"spike_p90\": %" PRIu64 ",\n"
                           "  \"spike_p99\": %" PRIu64 ",\n"
                           "  \"spike_max\": %" PRIu64 ",\n"
                           "  \"spike_mean\": %.2f,\n"
                           "  \"spike_stddev\": %.2f\n"
                           "}\n",
                    timer_ghz, is_huge ? "HUGETLB_2MB" : "STANDARD_MMAP",
                    n_probes, elapsed_sec, spike_count,
                    ((double)spike_count / (double)n_probes) * 100.0,
                    p50_base, p99_base, threshold,
                    spike_p50, spike_p90, spike_p99, spike_max,
                    spike_mean, spike_stddev);
            fclose(fjson);
        }
    }

    free(spikes);
    munmap(page, HUGEPAGE_2M);
    return 0;
}
/* end of file: clouds-labs/labs/tailslayer/tailslayer_extreme.c */
