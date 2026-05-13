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
CFLAGS  := -Wall -Wextra -O2 -fPIC
CPPFLAGS := -I$(INC_DIR)

# Files
SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

.PHONY: all static shared clean

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


# Testing
TEST_SRC := tests/test.c
TEST_BIN_DIR := tests/bin
TEST_BIN := $(TEST_BIN_DIR)/test

.PHONY: test

test: static | bin
	$(CC) $(CFLAGS) $(CPPFLAGS) $(TEST_SRC) -L$(LIB_DIR) -l$(LIB_NAME) -o $(TEST_BIN)
	@echo "Running test..."
	./$(TEST_BIN)

bin:
	mkdir -p tests/bin

test_clean:
	rm -rf $(TEST_BIN_DIR) $(TEST_BIN)