CC = gcc
CROSSCC := /home/tkatz/Downloads/cross-pi-gcc-10.2.0-0/bin/arm-linux-gnueabihf-gcc 
CFLAGS := -Wall -pedantic -g

BIN := bin

$(shell mkdir -p bin)

local: src/prod_cons.c
	$(CC) $(CFLAGS) $^ -o $(BIN)/$@  -lpthread

cross: src/prod_cons.c
	$(CROSSCC) $(CFLAGS) $^ -o $(BIN)/$@  -lpthread

test: src/prod_cons_test.c
	$(CC) $(CFLAGS) $^ -o $(BIN)/$@ -lpthread

check: src/check.c
	$(CC) $(CFLAGS) $^ -o $(BIN)/$@ -lpthread

foo: src/foo.c
	$(CC) $(CFLAGS) $^ -o $(BIN)/$@ -lpthread

.PHONY: clean

clean:
	rm -rf bin/