# Path: clouds-labs/labs/tailslayer/tailslayer.mk
# Modular Makefile for Tailslayer extreme build, telemetry & multi-cloud matrix
# Max Column: 80 Columns
# ==============================================================================

TAILSLAYER_DIR := $(REPO_ROOT)/labs/tailslayer
TAILSLAYER_SRC := $(TAILSLAYER_DIR)/tailslayer_extreme.c
TAILSLAYER_BUILD_DIR := $(REPO_ROOT)/build/tailslayer_extreme
TAILSLAYER_LOG_DIR := $(REPO_ROOT)/logs/tailslayer_extreme

# Strict Compiler Flags
TAILSLAYER_CC ?= gcc
TAILSLAYER_STRICT_FLAGS ?= -Wall -Wextra -Werror -Wpedantic -Wconversion \
                          -Wshadow -Wcast-align -Wpointer-arith -Wwrite-strings \
                          -O3 -fno-omit-frame-pointer -fverbose-asm
TAILSLAYER_LDFLAGS ?= -lm

# Telemetry Parameters
TAILSLAYER_PROBES ?= 2000000
TAILSLAYER_TREFI_US ?= 7.8
TAILSLAYER_THRESH_MULT ?= 2.0

.PHONY: tailslayer-build-extreme tailslayer-extreme tailslayer-clean-extreme \
	llama2c-extreme run-llama2c-fleet \
	tailslayer-rust-build tailslayer-rust-probe \
	llama2c-rust-build llama2c-rust-run

## tailslayer-build-extreme: Compile local extreme strict binary with full artifacts
tailslayer-build-extreme:
	@mkdir -p $(TAILSLAYER_BUILD_DIR)
	@echo "[*] Compiling Tailslayer Extreme with strict flags..."
	$(TAILSLAYER_CC) $(TAILSLAYER_STRICT_FLAGS) -save-temps=obj \
		-o $(TAILSLAYER_BUILD_DIR)/tailslayer_extreme $(TAILSLAYER_SRC) $(TAILSLAYER_LDFLAGS)
	@objdump -d $(TAILSLAYER_BUILD_DIR)/tailslayer_extreme > $(TAILSLAYER_BUILD_DIR)/tailslayer_extreme.disasm
	@nm -C $(TAILSLAYER_BUILD_DIR)/tailslayer_extreme > $(TAILSLAYER_BUILD_DIR)/tailslayer_extreme.symbols
	@readelf -S $(TAILSLAYER_BUILD_DIR)/tailslayer_extreme > $(TAILSLAYER_BUILD_DIR)/tailslayer_extreme.sections
	@echo "[+] Local build artifacts preserved in $(TAILSLAYER_BUILD_DIR)/"

## tailslayer-extreme: Execute extreme test matrix across OCI, Azure, and GCP
tailslayer-extreme:
	@mkdir -p $(TAILSLAYER_LOG_DIR)
	@$(PYTHON) $(REPO_ROOT)/tools/test_tailslayer_extreme.py \
		--probes $(TAILSLAYER_PROBES) \
		--trefi-us $(TAILSLAYER_TREFI_US) \
		--thresh-mult $(TAILSLAYER_THRESH_MULT)

## llama2c-extreme: Execute extreme strictness build, run & artifact retention across fleet
llama2c-extreme:
	@mkdir -p $(REPO_ROOT)/logs/llama2c_extreme
	@$(PYTHON) $(REPO_ROOT)/tools/test_llama2c_extreme.py --tokens 50

## tailslayer-rust-build: Build Tailslayer Rust release binary
tailslayer-rust-build:
	@cargo build --manifest-path $(TAILSLAYER_DIR)/rust/Cargo.toml --release
	@echo "[+] Tailslayer Rust engine built successfully."

## tailslayer-rust-probe: Run Tailslayer Rust DRAM refresh probe
tailslayer-rust-probe: tailslayer-rust-build
	@$(TAILSLAYER_DIR)/rust/target/release/tailslayer -n 500000

## llama2c-rust-build: Build Llama 2 + Tailslayer Rust release binary
llama2c-rust-build:
	@cargo build --manifest-path $(REPO_ROOT)/labs/llama2.c/rust/Cargo.toml --release
	@echo "[+] Llama2.rs + Tailslayer built successfully."

## llama2c-rust-run: Run Llama 2 + Tailslayer Rust inference benchmark
llama2c-rust-run: llama2c-rust-build
	@$(REPO_ROOT)/labs/llama2.c/rust/target/release/llama2-tailslayer \
		$(REPO_ROOT)/labs/llama2.c/stories15M.bin -n 50 --hedged

## tailslayer-esp32-sim: Run cycle-accurate ESP32-S3 PSRAM hedged read sim
tailslayer-esp32-sim:
	@$(PYTHON) $(TAILSLAYER_DIR)/esp32/esp32_tailslayer_sim.py

## tailslayer-clean-extreme: Clean extreme build artifacts
tailslayer-clean-extreme:
	@rm -rf $(TAILSLAYER_BUILD_DIR)
	@echo "[+] Tailslayer extreme build directory cleaned."

# end of file: clouds-labs/labs/tailslayer/tailslayer.mk
