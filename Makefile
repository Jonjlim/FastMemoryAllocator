# Library configuration
LIB_NAME := cmalloc
SRC_DIR  := src
INC_DIR  := include
OBJ_DIR  := .obj
LIB_DIR  := lib

# Targets
STATIC_LIB := $(LIB_DIR)/lib$(LIB_NAME).a
SHARED_LIB := $(LIB_DIR)/lib$(LIB_NAME).so

# Compiler and Flags
CC      := gcc
CFLAGS  := -Wall -Wextra -flto -O3 -fPIC
CPPFLAGS := -I$(INC_DIR)

# Files
SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

.PHONY: all static shared clean t r lr d bench mb benchmarks tb ts t_clean

all: static shared

static: $(STATIC_LIB)
$(STATIC_LIB): $(OBJS) | $(LIB_DIR)
	ar rcs $@ $^

shared: $(SHARED_LIB)
$(SHARED_LIB): $(OBJS) | $(LIB_DIR)
	$(CC) -shared -o $@ $^

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(LIB_DIR) $(OBJ_DIR):
	mkdir -p $@

clean:
	rm -rf $(OBJ_DIR) $(LIB_DIR)


# Optional third-party allocators for benchmark comparisons (mimalloc, jemalloc,
# tcmalloc). Detected via Homebrew prefix or explicit BREW_PREFIX override.
BREW_PREFIX ?= $(shell brew --prefix 2>/dev/null)
BENCH_CPPFLAGS :=
BENCH_LDFLAGS :=

ifneq ($(BREW_PREFIX),)
ifneq ($(wildcard $(BREW_PREFIX)/lib/libmimalloc.dylib),)
  BENCH_CPPFLAGS += -DHAVE_MIMALLOC -I$(BREW_PREFIX)/include
  BENCH_LDFLAGS += -L$(BREW_PREFIX)/lib -lmimalloc
endif
ifneq ($(wildcard $(BREW_PREFIX)/lib/libjemalloc.dylib),)
  BENCH_CPPFLAGS += -DHAVE_JEMALLOC -I$(BREW_PREFIX)/include
  BENCH_LDFLAGS += -L$(BREW_PREFIX)/lib -ljemalloc
  BENCH_LDFLAGS += -ldl
endif
ifneq ($(wildcard $(BREW_PREFIX)/lib/libtcmalloc.dylib),)
  BENCH_CPPFLAGS += -DHAVE_TCMALLOC -I$(BREW_PREFIX)/include
  BENCH_LDFLAGS += -L$(BREW_PREFIX)/lib -ltcmalloc
endif
endif

BENCH_LINK := -L$(LIB_DIR) -l$(LIB_NAME) $(BENCH_LDFLAGS)

# Testing
TEST_DIR := tests
TEST_BIN_DIR := tests/bin

SIMPLE_TEST := simple_test
DEBUG := debug
RIGOR := rigor_test
LONG_RIGOR := long_rigor_test
REALISTIC_BENCH := realistic_bench
MICRO_BENCH := micro_bench
THREAD_BASIC := thread_basic_test
THREAD_STRESS := thread_stress_bench

r: static | bin
	$(CC) $(CFLAGS) $(CPPFLAGS) $(BENCH_CPPFLAGS) $(TEST_DIR)/$(RIGOR).c $(BENCH_LINK) -o $(TEST_BIN_DIR)/$(RIGOR)
	@echo "Running long rigor..."
	./$(TEST_BIN_DIR)/$(RIGOR)

lr: static | bin
	$(CC) $(CFLAGS) $(CPPFLAGS) $(TEST_DIR)/$(LONG_RIGOR).c -L$(LIB_DIR) -l$(LIB_NAME) -o $(TEST_BIN_DIR)/$(LONG_RIGOR)
	@echo "Running long rigor..."
	./$(TEST_BIN_DIR)/$(LONG_RIGOR)

d: static | bin
	$(CC) $(CFLAGS) $(CPPFLAGS) $(TEST_DIR)/$(DEBUG).c -L$(LIB_DIR) -l$(LIB_NAME) -o $(TEST_BIN_DIR)/$(DEBUG)
	@echo "Running debug..."
	./$(TEST_BIN_DIR)/$(DEBUG)

t: static | bin
	$(CC) $(CFLAGS) $(CPPFLAGS) $(TEST_DIR)/$(SIMPLE_TEST).c -L$(LIB_DIR) -l$(LIB_NAME) -o $(TEST_BIN_DIR)/$(SIMPLE_TEST)
	@echo "Running simple test..."
	./$(TEST_BIN_DIR)/$(SIMPLE_TEST)

# Realistic benchmark suite: cmalloc vs system malloc on real-world patterns.
bench: static | bin
	$(CC) $(CFLAGS) $(CPPFLAGS) $(BENCH_CPPFLAGS) $(TEST_DIR)/$(REALISTIC_BENCH).c $(BENCH_LINK) -lm -o $(TEST_BIN_DIR)/$(REALISTIC_BENCH)
	@echo "Running realistic benchmark..."
	./$(TEST_BIN_DIR)/$(REALISTIC_BENCH)

# Micro-benchmark: isolates the optimized alloc-path and free-path costs.
mb: static | bin
	$(CC) $(CFLAGS) $(CPPFLAGS) $(BENCH_CPPFLAGS) $(TEST_DIR)/$(MICRO_BENCH).c $(BENCH_LINK) -o $(TEST_BIN_DIR)/$(MICRO_BENCH)
	@echo "Running micro-benchmark..."
	./$(TEST_BIN_DIR)/$(MICRO_BENCH)

# Run the full benchmark suite (micro + realistic).
benchmarks: mb bench

# Short concurrent correctness check.
tb: static | bin
	$(CC) $(CFLAGS) $(CPPFLAGS) $(TEST_DIR)/$(THREAD_BASIC).c -L$(LIB_DIR) -l$(LIB_NAME) -lpthread -o $(TEST_BIN_DIR)/$(THREAD_BASIC)
	@echo "Running thread basic test..."
	./$(TEST_BIN_DIR)/$(THREAD_BASIC)

# Concurrent stress + throughput benchmark.
ts: static | bin
	$(CC) $(CFLAGS) $(CPPFLAGS) $(TEST_DIR)/$(THREAD_STRESS).c -L$(LIB_DIR) -l$(LIB_NAME) -lpthread -o $(TEST_BIN_DIR)/$(THREAD_STRESS)
	@echo "Running thread stress benchmark..."
	./$(TEST_BIN_DIR)/$(THREAD_STRESS)

bin:
	mkdir -p tests/bin

t_clean:
	rm -rf $(TEST_BIN_DIR) $(TEST_BIN)