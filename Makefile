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
TEST_DIR := tests
TEST_BIN_DIR := tests/bin

SIMPLE_TEST := simple_test
STRESS_TEST := stress_test
DEBUG := debug

d: static | bin
	$(CC) $(CFLAGS) $(CPPFLAGS) $(TEST_DIR)/$(DEBUG).c -L$(LIB_DIR) -l$(LIB_NAME) -o $(TEST_BIN_DIR)/$(DEBUG)
	@echo "Running debug..."
	./$(TEST_BIN_DIR)/$(DEBUG)

t: static | bin
	$(CC) $(CFLAGS) $(CPPFLAGS) $(TEST_DIR)/$(SIMPLE_TEST).c -L$(LIB_DIR) -l$(LIB_NAME) -o $(TEST_BIN_DIR)/$(SIMPLE_TEST)
	@echo "Running simple test..."
	./$(TEST_BIN_DIR)/$(SIMPLE_TEST)

stress: static | bin
	$(CC) $(CFLAGS) $(CPPFLAGS) $(TEST_DIR)/$(STRESS_TEST).c -L$(LIB_DIR) -l$(LIB_NAME) -o $(TEST_BIN_DIR)/$(STRESS_TEST)
	@echo "Running simple test..."
	./$(TEST_BIN_DIR)/$(STRESS_TEST)

bin:
	mkdir -p tests/bin

t_clean:
	rm -rf $(TEST_BIN_DIR) $(TEST_BIN)