CC = gcc
CROSSCC := /home/tkatz/Downloads/cross-pi-gcc-10.2.0-0/bin/arm-linux-gnueabihf-gcc 
QUEUESIZE := 20
CFLAGS := -Wall -pedantic -g -DQUEUESIZE=$(QUEUESIZE) -DLWS_ROLE_WS=1 -DLWS_WITH_SECURE_STREAMS=1 -DLWS_WITHOUT_EXTENSIONS=0
LIBS := -lpthread -lm -lwebsockets -lsystemd

BIN := bin

$(shell mkdir -p bin)

help:
	echo "USAGE: make <target> QUEUESIZE=<n>"
	echo "Default QUEUESIZE=20"

local: src/main.c src/finhub-ss.c
	$(CC) $(CFLAGS) $^ -o $(BIN)/$@ $(LIBS)

cross: src/main.c src/finhub-ss.c
	$(CROSSCC) $(CFLAGS) $^ -o $(BIN)/$@ $(LIBS)


.PHONY: clean

clean:
	rm -rf bin/