# Path: labs/tailslayer/discovery/benchmark/Makefile
# Purpose: Build Makefile for Tailslayer C++ Discovery Benchmark with structured build/artifact outputs.
# Max Column: 80 Columns (comments/docs) / 120 Chars (code)

CXX = g++
CXXFLAGS = -O2 -std=c++17 -pthread -D_GNU_SOURCE
LDFLAGS = -pthread

BUILD_DIR = build
ARTIFACT_OUT_DIR = artifacts

OBJS = $(BUILD_DIR)/app_config.o $(BUILD_DIR)/benchmark.o $(BUILD_DIR)/stats.o $(BUILD_DIR)/main.o
TARGET = $(BUILD_DIR)/hedged_read_cpp

.PHONY: all clean generate-artifacts

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(ARTIFACT_OUT_DIR):
	mkdir -p $(ARTIFACT_OUT_DIR)

$(TARGET): $(BUILD_DIR) $(OBJS)
	$(CXX) $(LDFLAGS) -o $@ $(OBJS)

$(BUILD_DIR)/%.o: %.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

generate-artifacts: $(TARGET) | $(ARTIFACT_OUT_DIR)
	@objdump -d $(TARGET) > $(ARTIFACT_OUT_DIR)/hedged_read_cpp.disasm
	@nm -C $(TARGET) > $(ARTIFACT_OUT_DIR)/hedged_read_cpp.symbols
	@echo "[+] Benchmark artifacts generated in $(ARTIFACT_OUT_DIR)/"

clean:
	rm -rf $(BUILD_DIR) $(ARTIFACT_OUT_DIR)

# end of file: labs/tailslayer/discovery/benchmark/Makefile
