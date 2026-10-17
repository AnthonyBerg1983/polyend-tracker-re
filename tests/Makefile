# Host unit tests: builds every *_test.cpp against src/core and runs it.
CXX      ?= g++
SRC      := ../src
OUT      := ../build/host-tests
CXXFLAGS := -std=c++17 -Wall -Wextra -Werror -g -O1 -fsanitize=address,undefined \
            -fno-sanitize-recover=undefined -I$(SRC)/core -I$(SRC) -I. -MMD
LDFLAGS  := -fsanitize=address,undefined

CORE_SRCS := $(shell find $(SRC)/core -name '*.cpp')
CORE_OBJS := $(patsubst $(SRC)/%.cpp,$(OUT)/obj/%.o,$(CORE_SRCS))
TEST_SRCS := $(wildcard *_test.cpp)
TEST_BINS := $(patsubst %.cpp,$(OUT)/%,$(TEST_SRCS))

all: run

$(OUT)/obj/%.o: $(SRC)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OUT)/libcore.a: $(CORE_OBJS)
	@rm -f $@
	ar rcs $@ $^

$(OUT)/%_test: %_test.cpp $(OUT)/libcore.a
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $< $(OUT)/libcore.a $(LDFLAGS) -o $@

build: $(TEST_BINS)

run: $(TEST_BINS)
	@set -e; for t in $(TEST_BINS); do OT_FIXTURES=$(CURDIR)/fixtures $$t; done

# Audio render benchmark (tests/audio_bench.cpp): -O2, no sanitizers.
BENCH_OUT := ../build/bench
BENCH_FLAGS := -std=c++17 -O2 -g -DOT_AUDIO_SDRAM_STATS -I$(SRC)/core -I$(SRC) -I.
BENCH_SRCS := $(CORE_SRCS)
$(BENCH_OUT)/audio_bench: audio_bench.cpp $(CORE_SRCS)
	@mkdir -p $(dir $@)
	$(CXX) $(BENCH_FLAGS) audio_bench.cpp $(BENCH_SRCS) -o $@

bench: $(BENCH_OUT)/audio_bench
	$(BENCH_OUT)/audio_bench

# Same benchmark compiled with the device's compiler flags for Cortex-M4 and
# run under QEMU (mps2-an386) with -icount: reports executed M4 instructions.
M4_CXX := arm-none-eabi-g++
M4_CC := arm-none-eabi-gcc
M4_FLAGS := -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -O2 -ffunction-sections -fdata-sections
M4_SRCS := audio_bench.cpp $(wildcard $(SRC)/core/audio/*.cpp) $(SRC)/core/formats/wav.cpp \
           $(SRC)/core/formats/format_common.cpp $(SRC)/core/model/project.cpp $(SRC)/core/model/fx.cpp
$(BENCH_OUT)/audio_bench_m4.elf: $(M4_SRCS) bench_m4/startup.c bench_m4/mps2.ld
	@mkdir -p $(dir $@)
	$(M4_CC) $(M4_FLAGS) -c bench_m4/startup.c -o $(BENCH_OUT)/startup_m4.o
	$(M4_CXX) $(M4_FLAGS) -std=gnu++17 -fno-exceptions -fno-rtti -DOT_BENCH_M4 -DOT_AUDIO_SDRAM_STATS -I$(SRC)/core -I$(SRC) -I. \
	    $(M4_SRCS) $(BENCH_OUT)/startup_m4.o -T bench_m4/mps2.ld -nostartfiles --specs=nano.specs --specs=rdimon.specs -u _printf_float \
	    -Wl,--gc-sections -lm -o $@

bench-m4: $(BENCH_OUT)/audio_bench_m4.elf
	qemu-system-arm -M mps2-an386 -nographic -icount shift=0 -semihosting-config enable=on,target=native \
	    -kernel $<

clean:
	rm -rf $(OUT) $(BENCH_OUT)

-include $(CORE_OBJS:.o=.d) $(TEST_BINS:=.d)
.PHONY: all build run clean bench bench-m4
