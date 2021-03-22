CC := /home/tkatz/Downloads/cross-pi-gcc-10.2.0-0/bin/arm-linux-gnueabihf-gcc 
CFLAGS := -Wall -pedantic -O3

BIN := bin
SRC := src/*.c

$(shell mkdir -p bin)

build: $(SRC)
	$(CC) $(CFLAGS) $^ -o $(BIN)/main  -lpthread

.PHONY: clean

clean:
	rm -rf bin/