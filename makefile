# Project makefile

TARGET1 := yashd
TARGET2 := yash

# Important directories
CW_DIR := $(shell pwd)
BIN_DIR := $(CW_DIR)
INC_DIR := $(CW_DIR)
LIB_DIR := $(CW_DIR)
OBJ_DIR := $(CW_DIR)
SRC_DIR := $(CW_DIR)

# Define compiler and flags
CC := gcc
PFLAGS := -I$(INC_DIR)
#CFLAGS := -D_POSIX_C_SOURCE -std=gnu11 -Wall -Werror
CFLAGS := -std=gnu11 -Wall -Werror
#LDFLAGS := -Llib
LDLIBS1 := -lpthread -lreadline
LDLIBS2 := -lreadline

DEP := $(wildcard $(INC_DIR)/*.h)
SRC := $(wildcard $(SRC_DIR)/*.c)
OBJ := $(SRC:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

.PHONY: all clean

all: $(TARGET1) $(TARGET2)

debug: CFLAGS += -g
debug: $(TARGET1) $(TARGET2)

$(TARGET1): yashd.o shell.o
	mkdir -p $(BIN_DIR)
	$(CC) $(LDFLAGS) $^ $(LDLIBS1) -o $(BIN_DIR)/$@

$(TARGET2): yash.o
	mkdir -p $(BIN_DIR)
	$(CC) $(LDFLAGS) $^ $(LDLIBS2) -o $(BIN_DIR)/$@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(DEP) | $(OBJ_DIR)
	$(CC) $(PFLAGS) $(CFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $@

clean:
	$(RM) $(OBJ)
	rm -f core $(BIN_DIR)/$(TARGET1) $(BIN_DIR)/$(TARGET2)

