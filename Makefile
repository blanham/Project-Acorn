CC = gcc
CFLAGS = -Wall -Wextra -Werror -std=c11 -O2 $(shell sdl2-config --cflags)
LDFLAGS = $(shell sdl2-config --libs)
TARGET = B8086
TEST_TARGET = test_runner
OBJS = 5150emu.o intel8086.o
TEST_OBJS = test_runner.o intel8086.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $(TARGET) $(OBJS) $(LDFLAGS)

$(TEST_TARGET): $(TEST_OBJS)
	$(CC) -o $(TEST_TARGET) $(TEST_OBJS)

5150emu.o: 5150emu.c intel8086.h
	$(CC) $(CFLAGS) -c 5150emu.c

intel8086.o: intel8086.c intel8086.h opcode.h
	$(CC) $(CFLAGS) -c intel8086.c

test_runner.o: test_runner.c intel8086.h opcode.h
	$(CC) $(CFLAGS) -c test_runner.c

test: $(TEST_TARGET)
	./$(TEST_TARGET)

clean:
	rm -f $(OBJS) $(TEST_OBJS) $(TARGET) $(TEST_TARGET)

.PHONY: all test clean
