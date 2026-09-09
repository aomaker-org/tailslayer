//! Path: clouds-labs/labs/tailslayer/rust/src/lib.rs
//! Purpose: Tailslayer DRAM Refresh Latency Mitigation & Dual-Channel Memory Engine.

pub mod allocator;
pub mod arch;
pub mod probe;

pub use allocator::HedgedBuffer;
pub use arch::{calibrate_timer_ghz, clflush, read_cycles};
pub use probe::{DRAMProbe, ProbeConfig, SpikeRecord, TelemetryReport};
