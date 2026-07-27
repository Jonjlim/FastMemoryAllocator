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

.PHONY: all static shared clean r tr d mb t_clean

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

DEBUG := debug
RIGOR := rigor_test
THREAD_RIGOR := thread_rigor_test
MICRO_BENCH := micro_bench

r: static | bin
	$(CC) $(CFLAGS) $(CPPFLAGS) $(BENCH_CPPFLAGS) $(TEST_DIR)/$(RIGOR).c $(BENCH_LINK) -o $(TEST_BIN_DIR)/$(RIGOR)
	@echo "Running rigor..."
	./$(TEST_BIN_DIR)/$(RIGOR)

tr: static | bin
	$(CC) $(CFLAGS) $(CPPFLAGS) $(BENCH_CPPFLAGS) $(TEST_DIR)/$(THREAD_RIGOR).c $(BENCH_LINK) -lpthread -o $(TEST_BIN_DIR)/$(THREAD_RIGOR)
	@echo "Running thread rigor..."
	./$(TEST_BIN_DIR)/$(THREAD_RIGOR)

d: static | bin
	$(CC) $(CFLAGS) $(CPPFLAGS) $(TEST_DIR)/$(DEBUG).c -L$(LIB_DIR) -l$(LIB_NAME) -o $(TEST_BIN_DIR)/$(DEBUG)
	@echo "Running debug..."
	./$(TEST_BIN_DIR)/$(DEBUG)

# Micro-benchmark: isolates the optimized alloc-path and free-path costs.
mb: static | bin
	$(CC) $(CFLAGS) $(CPPFLAGS) $(BENCH_CPPFLAGS) $(TEST_DIR)/$(MICRO_BENCH).c $(BENCH_LINK) -o $(TEST_BIN_DIR)/$(MICRO_BENCH)
	@echo "Running micro-benchmark..."
	./$(TEST_BIN_DIR)/$(MICRO_BENCH)

bin:
	mkdir -p tests/bin

t_clean:
	rm -rf $(TEST_BIN_DIR)