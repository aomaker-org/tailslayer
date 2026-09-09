//! Path: clouds-labs/labs/tailslayer/rust/src/probe.rs
//! Purpose: DRAM refresh jitter probe, spike detector, and statistical ledger.

use crate::allocator::HedgedBuffer;
use crate::arch::{clflush, read_cycles};
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ProbeConfig {
    pub num_probes: usize,
    pub trefi_us: f64,
    pub thresh_multiplier: f64,
    pub buffer_size: usize,
}

impl Default for ProbeConfig {
    fn default() -> Self {
        Self {
            num_probes: 2_000_000,
            trefi_us: 7.8,
            thresh_multiplier: 2.0,
            buffer_size: 16 * 1024 * 1024,
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SpikeRecord {
    pub index: usize,
    pub latency_cycles: u64,
    pub delta_prev_cycles: u64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TelemetryReport {
    pub total_probes: usize,
    pub elapsed_seconds: f64,
    pub timer_ghz: f64,
    pub page_mode: String,
    pub baseline_p50: u64,
    pub baseline_p90: u64,
    pub baseline_p99: u64,
    pub threshold_cycles: u64,
    pub spike_count: usize,
    pub spike_rate_pct: f64,
    pub spike_p50: u64,
    pub spike_p90: u64,
    pub spike_p99: u64,
    pub spike_max: u64,
    pub spike_mean: f64,
    pub spike_stddev: f64,
}

pub struct DRAMProbe {
    config: ProbeConfig,
    buffer: HedgedBuffer,
    timer_ghz: f64,
}

impl DRAMProbe {
    pub fn new(config: ProbeConfig, timer_ghz: f64) -> Result<Self, String> {
        let buffer = HedgedBuffer::allocate(config.buffer_size)?;
        Ok(Self {
            config,
            buffer,
            timer_ghz,
        })
    }

    pub fn run_calibration(&self, samples: usize) -> Vec<u64> {
        let mut lats = Vec::with_capacity(samples);
        let ptr = self.buffer.as_ptr();
        for i in 0..samples {
            let offset = (i * 64) % (self.buffer.len() - 64);
            unsafe {
                let target = ptr.add(offset);
                clflush(target);
                let t0 = read_cycles();
                let _val = std::ptr::read_volatile(target);
                let t1 = read_cycles();
                lats.push(t1.saturating_sub(t0));
            }
        }
        lats.sort_unstable();
        lats
    }

    pub fn run(&self) -> (TelemetryReport, Vec<SpikeRecord>) {
        let cal_lats = self.run_calibration(10_000);
        let baseline_p50 = cal_lats[cal_lats.len() / 2];
        let baseline_p90 = cal_lats[(cal_lats.len() as f64 * 0.90) as usize];
        let baseline_p99 = cal_lats[(cal_lats.len() as f64 * 0.99) as usize];
        let threshold = ((baseline_p50 as f64) * self.config.thresh_multiplier) as u64;

        let num_probes = self.config.num_probes;
        let mut spikes = Vec::with_capacity(num_probes / 100);
        let mut last_spike_cycle = 0u64;

        let ptr = self.buffer.as_ptr();
        let buf_len = self.buffer.len();

        let t0_wall = std::time::Instant::now();

        for i in 0..num_probes {
            let offset = ((i * 128) ^ (i >> 3)) % (buf_len - 64);
            unsafe {
                let target = ptr.add(offset);
                clflush(target);
                let t0 = read_cycles();
                let _val = std::ptr::read_volatile(target);
                let t1 = read_cycles();
                let lat = t1.saturating_sub(t0);

                if lat >= threshold {
                    let delta = if last_spike_cycle == 0 { 0 } else { t1.saturating_sub(last_spike_cycle) };
                    last_spike_cycle = t1;
                    spikes.push(SpikeRecord {
                        index: i,
                        latency_cycles: lat,
                        delta_prev_cycles: delta,
                    });
                }
            }
        }

        let elapsed_sec = t0_wall.elapsed().as_secs_f64();
        let spike_count = spikes.len();
        let spike_rate = (spike_count as f64 / num_probes as f64) * 100.0;

        let mut spike_lats: Vec<u64> = spikes.iter().map(|s| s.latency_cycles).collect();
        spike_lats.sort_unstable();

        let (sp_p50, sp_p90, sp_p99, sp_max, sp_mean, sp_std) = if !spike_lats.is_empty() {
            let n = spike_lats.len();
            let p50 = spike_lats[n / 2];
            let p90 = spike_lats[(n as f64 * 0.90) as usize];
            let p99 = spike_lats[(n as f64 * 0.99) as usize];
            let max = spike_lats[n - 1];
            let sum: u64 = spike_lats.iter().sum();
            let mean = sum as f64 / n as f64;
            let var_sum: f64 = spike_lats.iter().map(|&x| (x as f64 - mean).powi(2)).sum();
            let stddev = (var_sum / n as f64).sqrt();
            (p50, p90, p99, max, mean, stddev)
        } else {
            (0, 0, 0, 0, 0.0, 0.0)
        };

        let report = TelemetryReport {
            total_probes: num_probes,
            elapsed_seconds: elapsed_sec,
            timer_ghz: self.timer_ghz,
            page_mode: if self.buffer.is_hugepage() { "HUGETLB_2M".into() } else { "4K_PAGES".into() },
            baseline_p50,
            baseline_p90,
            baseline_p99,
            threshold_cycles: threshold,
            spike_count,
            spike_rate_pct: spike_rate,
            spike_p50: sp_p50,
            spike_p90: sp_p90,
            spike_p99: sp_p99,
            spike_max: sp_max,
            spike_mean: sp_mean,
            spike_stddev: sp_std,
        };

        (report, spikes)
    }
}
