CC=gcc
CFLAGS=-Wall -Wextra -O2 -g -fsanitize=address,undefined
LDFLAGS=
TARGET=myshell


all: $(TARGET)


$(TARGET): shell.o
$(CC) $(CFLAGS) -o $(TARGET) shell.o $(LDFLAGS)


shell.o: shell.c
$(CC) $(CFLAGS) -c shell.c


clean:
rm -f *.o $(TARGET)


.PHONY: all clean
