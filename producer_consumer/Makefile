CC = gcc
CROSSCC := /home/tkatz/Downloads/cross-pi-gcc-10.2.0-0/bin/arm-linux-gnueabihf-gcc 
QUEUESIZE := 20
CFLAGS := -Wall -pedantic -g -DQUEUESIZE=$(QUEUESIZE)

BIN := bin

$(shell mkdir -p bin)

help:
	echo "USAGE: make <target> QUEUESIZE=<n>"
	echo "Default QUEUESIZE=20"

local: src/prod_cons.c
	$(CC) $(CFLAGS) $^ -o $(BIN)/$@  -lpthread -lm

cross: src/prod_cons.c
	$(CROSSCC) $(CFLAGS) $^ -o $(BIN)/$@  -lpthread -lm

test: src/prod_cons_original.c
	$(CC) $(CFLAGS) $^ -o $(BIN)/$@ -lpthread -lm

.PHONY: clean

clean:
	rm -rf bin/