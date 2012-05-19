CC := gcc
CFLAGS := -Wall
LDFLAGS := -lpng

all: illuminati

illuminati: illuminati.o

%: %.o
	$(CC) -o $@ $@.o $(LDFLAGS)

clean:
	rm -f *.o
	rm -f illuminati
