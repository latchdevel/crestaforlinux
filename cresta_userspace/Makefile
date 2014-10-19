CC=gcc
CFLAGS=-Wall
BINARYNAME=cresta

all: cresta

cresta: cresta_decoder.o
	$(CC) $(CFLAGS) cresta_decoder.o -o $(BINARYNAME)



clean:
	rm *.o $(BINARYNAME)