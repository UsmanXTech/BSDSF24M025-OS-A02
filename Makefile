CC = gcc
CFLAGS = -Wall -Wextra -std=c11

SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin

SRC = $(SRC_DIR)/ls-v1.0.0.c
OBJ = $(OBJ_DIR)/ls-v1.0.0.o
TARGET = $(BIN_DIR)/ls

all: $(TARGET)

$(TARGET): $(OBJ)
	mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ)

$(OBJ): $(SRC)
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $(SRC) -o $(OBJ)

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

.PHONY: all clean
