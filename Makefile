CC := gcc
LDFLAGS := -lpng

all: ogets

ogets: ogets.o

%: %.o
	$(CC) -o $@ $@.o $(LDFLAGS)

clean:
	rm -f *.o
	rm -f ogets
