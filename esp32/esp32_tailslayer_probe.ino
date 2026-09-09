/*
 * Path: clouds-labs/labs/tailslayer/esp32/esp32_tailslayer_probe.ino
 * Purpose: ESP32/ESP32-S3 Dual-Core Tailslayer DRAM/PSRAM & SRAM Jitter Probe
 * Target: ESP32-S3 DevKitC-1 / ESP32 with 8MB Octal/Quad PSRAM
 * Baud Rate: 115200
 * Max Column: 80 Columns
 * ============================================================================
 */

#include <Arduino.h>
#include "esp_cpu.h"
#include "esp_heap_caps.h"
#include "esp_cache.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define NUM_PROBES       100000
#define BUFFER_SIZE      (32 * 1024)
#define NUM_BINS         10

// Memory pointers
static uint8_t *sram_buf = NULL;
static uint8_t *psram_buf = NULL;

// Dual-Core race synchronization
static volatile bool start_race = false;
static volatile bool race_done = false;
static volatile uint32_t core0_latency = 0;
static volatile uint32_t core1_latency = 0;
static volatile uint8_t core0_val = 0;
static volatile uint8_t core1_val = 0;

// Statistics struct
struct LatencyStats {
    uint32_t min_cycles;
    uint32_t max_cycles;
    uint32_t p50_cycles;
    uint32_t p90_cycles;
    uint32_t p99_cycles;
    uint32_t spikes;
    float avg_cycles;
};

// Task running on Core 1 for Hedged Reading
void core1_worker(void *pvParameters) {
    (void)pvParameters;
    while (true) {
        while (!start_race) {
            vTaskDelay(0);
        }
        
        // Invalidate cache and race read
        esp_cache_msync(psram_buf, 64, ESP_CACHE_MSYNC_FLAG_INVALIDATE);
        uint32_t t0 = esp_cpu_get_cycle_count();
        core1_val = psram_buf[0];
        uint32_t t1 = esp_cpu_get_cycle_count();
        
        core1_latency = t1 - t0;
        race_done = true;
        
        while (start_race) {
            vTaskDelay(0);
        }
    }
}

void print_banner() {
    Serial.println("========================================================================");
    Serial.println("⚡ ESP32 / ESP32-S3 TAILSLAYER MEMORY JITTER & HEDGED READ PROBE");
    Serial.printf("   CPU Freq     : %lu MHz (Xtensa Dual-Core)\n", getCpuFrequencyMhz());
    Serial.printf("   Internal SRAM: %lu bytes free\n", (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    Serial.printf("   External PSRAM: %lu bytes free\n", (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    Serial.println("========================================================================");
}

void run_sram_benchmark() {
    Serial.println("\n[*] Probing Internal SRAM (Zero-Refresh Control Baseline)...");
    uint32_t min_lat = UINT32_MAX, max_lat = 0;
    uint64_t sum_lat = 0;
    uint32_t spikes = 0;
    uint32_t threshold = 10; // cycles

    for (int i = 0; i < NUM_PROBES; i++) {
        uint32_t t0 = esp_cpu_get_cycle_count();
        volatile uint8_t val = sram_buf[(i * 64) % BUFFER_SIZE];
        (void)val;
        uint32_t t1 = esp_cpu_get_cycle_count();
        uint32_t delta = t1 - t0;

        if (delta < min_lat) min_lat = delta;
        if (delta > max_lat) max_lat = delta;
        if (delta > threshold) spikes++;
        sum_lat += delta;
    }

    Serial.printf("[+] SRAM Results: Avg: %.2f cyc | Min: %lu | Max: %lu | Spikes: %lu (%.4f%%)\n",
                  (double)sum_lat / NUM_PROBES, (unsigned long)min_lat,
                  (unsigned long)max_lat, (unsigned long)spikes,
                  ((double)spikes / NUM_PROBES) * 100.0);
}

void run_psram_benchmark() {
    if (!psram_buf) {
        Serial.println("[-] PSRAM not available on this board.");
        return;
    }

    Serial.println("\n[*] Probing External PSRAM (DRAM Core + Auto-Refresh Stalls)...");
    uint32_t min_lat = UINT32_MAX, max_lat = 0;
    uint64_t sum_lat = 0;
    uint32_t spikes = 0;
    uint32_t threshold = 40; // cycles (~166ns at 240MHz)

    for (int i = 0; i < NUM_PROBES; i++) {
        esp_cache_msync(psram_buf, 64, ESP_CACHE_MSYNC_FLAG_INVALIDATE);
        
        uint32_t t0 = esp_cpu_get_cycle_count();
        volatile uint8_t val = psram_buf[(i * 64) % BUFFER_SIZE];
        (void)val;
        uint32_t t1 = esp_cpu_get_cycle_count();
        uint32_t delta = t1 - t0;

        if (delta < min_lat) min_lat = delta;
        if (delta > max_lat) max_lat = delta;
        if (delta > threshold) spikes++;
        sum_lat += delta;
    }

    Serial.printf("[+] PSRAM Results: Avg: %.2f cyc | Min: %lu | Max: %lu | Stalls: %lu (%.3f%%)\n",
                  (double)sum_lat / NUM_PROBES, (unsigned long)min_lat,
                  (unsigned long)max_lat, (unsigned long)spikes,
                  ((double)spikes / NUM_PROBES) * 100.0);
}

void run_hedged_read_benchmark() {
    if (!psram_buf) return;

    Serial.println("\n[*] Running Dual-Core Hedged Read Test (Core 0 vs Core 1 Race)...");
    uint32_t hedged_wins_c0 = 0, hedged_wins_c1 = 0;
    uint64_t sum_hedged = 0;
    uint32_t hedged_spikes = 0;
    uint32_t threshold = 40;

    for (int i = 0; i < (NUM_PROBES / 10); i++) {
        race_done = false;
        start_race = true; // Signal Core 1

        esp_cache_msync(psram_buf, 64, ESP_CACHE_MSYNC_FLAG_INVALIDATE);
        uint32_t t0 = esp_cpu_get_cycle_count();
        core0_val = psram_buf[0];
        uint32_t t1 = esp_cpu_get_cycle_count();
        core0_latency = t1 - t0;

        while (!race_done) {
            // Wait Core 1
        }
        start_race = false;

        uint32_t best_lat = (core0_latency < core1_latency) ? core0_latency : core1_latency;
        if (core0_latency < core1_latency) hedged_wins_c0++;
        else hedged_wins_c1++;

        if (best_lat > threshold) hedged_spikes++;
        sum_hedged += best_lat;
    }

    Serial.printf("[+] Hedged PSRAM: Avg: %.2f cyc | Spikes: %lu | Core0 Wins: %lu | Core1 Wins: %lu\n",
                  (double)sum_hedged / (NUM_PROBES / 10), (unsigned long)hedged_spikes,
                  (unsigned long)hedged_wins_c0, (unsigned long)hedged_wins_c1);
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    print_banner();

    // Allocate SRAM buffer
    sram_buf = (uint8_t *)heap_caps_malloc(BUFFER_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (sram_buf) memset(sram_buf, 0x55, BUFFER_SIZE);

    // Allocate PSRAM buffer
    psram_buf = (uint8_t *)heap_caps_malloc(BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (psram_buf) memset(psram_buf, 0xAA, BUFFER_SIZE);

    // Spawn Core 1 Worker Task
    xTaskCreatePinnedToCore(core1_worker, "tailslayer_c1", 4096, NULL, 5, NULL, 1);

    // Run Suite
    run_sram_benchmark();
    run_psram_benchmark();
    run_hedged_read_benchmark();

    Serial.println("\n========================================================================");
    Serial.println("✅ ESP32 Tailslayer probe suite finished.");
    Serial.println("========================================================================");
}

void loop() {
    delay(10000);
}
