//! Path: clouds-labs/labs/tailslayer/rust/src/main.rs
//! Purpose: Tailslayer Rust CLI Benchmark & Telemetry Harvester.

use std::fs::File;
use std::io::Write;
use tailslayer::{calibrate_timer_ghz, DRAMProbe, ProbeConfig};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args: Vec<String> = std::env::args().collect();
    let mut num_probes = 2_000_000usize;
    let mut trefi_us = 7.8f64;
    let mut thresh_mult = 2.0f64;
    let mut csv_path: Option<String> = None;
    let mut json_path: Option<String> = None;

    let mut i = 1;
    while i < args.len() {
        match args[i].as_str() {
            "-n" | "--probes" => {
                if i + 1 < args.len() { num_probes = args[i + 1].parse().unwrap_or(2_000_000); i += 1; }
            }
            "-t" | "--trefi" => {
                if i + 1 < args.len() { trefi_us = args[i + 1].parse().unwrap_or(7.8); i += 1; }
            }
            "-m" | "--mult" => {
                if i + 1 < args.len() { thresh_mult = args[i + 1].parse().unwrap_or(2.0); i += 1; }
            }
            "-c" | "--csv" => {
                if i + 1 < args.len() { csv_path = Some(args[i + 1].clone()); i += 1; }
            }
            "-j" | "--json" => {
                if i + 1 < args.len() { json_path = Some(args[i + 1].clone()); i += 1; }
            }
            _ => {}
        }
        i += 1;
    }

    println!("========================================================================");
    println!("⚡ TAILSLAYER RUST DRAM REFRESH PROBE & ARTIFACT GENERATOR");
    println!("   Target Probes : {:>10}", num_probes);
    println!("   Expected tREFI: {:>10.2} us", trefi_us);
    println!("   Multiplier    : {:>10.2}x", thresh_mult);
    println!("========================================================================");

    print!("[*] Calibrating hardware timer...");
    let timer_ghz = calibrate_timer_ghz();
    println!(" {:.3} GHz", timer_ghz);

    let config = ProbeConfig {
        num_probes,
        trefi_us,
        thresh_multiplier: thresh_mult,
        buffer_size: 16 * 1024 * 1024,
    };

    let probe = DRAMProbe::new(config, timer_ghz)?;
    println!("[*] Allocating dual-channel hedged buffer & warming pages...");
    println!("[*] Executing {:?} DRAM read probes...", num_probes);

    let (report, spikes) = probe.run();

    println!("\n📊 TAILSLAYER TELEMETRY REPORT");
    println!("------------------------------------------------------------------------");
    println!("Page Mode         : {}", report.page_mode);
    println!("Elapsed Time      : {:.3} seconds", report.elapsed_seconds);
    println!("Baseline p50      : {} cycles ({:.1} ns)", report.baseline_p50, report.baseline_p50 as f64 / timer_ghz);
    println!("Baseline p90      : {} cycles ({:.1} ns)", report.baseline_p90, report.baseline_p90 as f64 / timer_ghz);
    println!("Baseline p99      : {} cycles ({:.1} ns)", report.baseline_p99, report.baseline_p99 as f64 / timer_ghz);
    println!("Spike Threshold   : {} cycles", report.threshold_cycles);
    println!("Spikes Detected   : {} ({:.4}%)", report.spike_count, report.spike_rate_pct);
    println!("Spike p50 Latency : {} cycles", report.spike_p50);
    println!("Spike p90 Latency : {} cycles", report.spike_p90);
    println!("Spike p99 Latency : {} cycles", report.spike_p99);
    println!("Spike Max Latency : {} cycles", report.spike_max);
    println!("Spike Mean Latency: {:.1} cycles", report.spike_mean);
    println!("Spike StdDev      : {:.1} cycles", report.spike_stddev);
    println!("========================================================================");

    if let Some(cp) = csv_path {
        let mut f = File::create(&cp)?;
        writeln!(f, "index,latency_cycles,delta_prev_cycles")?;
        for s in &spikes {
            writeln!(f, "{},{},{}", s.index, s.latency_cycles, s.delta_prev_cycles)?;
        }
        println!("[+] Raw spike data written to: {}", cp);
    }

    if let Some(jp) = json_path {
        let mut f = File::create(&jp)?;
        serde_json::to_writer_pretty(&mut f, &report)?;
        println!("[+] Telemetry JSON written to: {}", jp);
    }

    Ok(())
}
