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

.PHONY: tailslayer-build-extreme tailslayer-extreme tailslayer-clean-extreme

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

## tailslayer-clean-extreme: Clean extreme build artifacts
tailslayer-clean-extreme:
	@rm -rf $(TAILSLAYER_BUILD_DIR)
	@echo "[+] Tailslayer extreme build directory cleaned."

# end of file: clouds-labs/labs/tailslayer/tailslayer.mk
