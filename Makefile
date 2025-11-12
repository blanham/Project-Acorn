CC = gcc
CFLAGS = -Wall -Wextra -Werror -std=c11 -O2 $(shell sdl2-config --cflags)
LDFLAGS = $(shell sdl2-config --libs)
TARGET = B8086
TEST_TARGET = test_runner
JSON_TEST_TARGET = json_test_runner
OBJS = 5150emu.o intel8086.o
TEST_OBJS = test_runner.o intel8086.o
JSON_TEST_OBJS = json_test_runner.o intel8086.o cJSON.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $(TARGET) $(OBJS) $(LDFLAGS)

$(TEST_TARGET): $(TEST_OBJS)
	$(CC) -o $(TEST_TARGET) $(TEST_OBJS)

$(JSON_TEST_TARGET): $(JSON_TEST_OBJS)
	$(CC) -o $(JSON_TEST_TARGET) $(JSON_TEST_OBJS) -lz

5150emu.o: 5150emu.c intel8086.h
	$(CC) $(CFLAGS) -c 5150emu.c

intel8086.o: intel8086.c intel8086.h opcode.h
	$(CC) $(CFLAGS) -c intel8086.c

test_runner.o: test_runner.c intel8086.h opcode.h
	$(CC) $(CFLAGS) -c test_runner.c

json_test_runner.o: json_test_runner.c intel8086.h opcode.h cJSON.h
	$(CC) $(CFLAGS) -c json_test_runner.c

cJSON.o: cJSON.c cJSON.h
	$(CC) $(CFLAGS) -Wno-error -c cJSON.c

test: $(TEST_TARGET)
	./$(TEST_TARGET)

jsontest: $(JSON_TEST_TARGET)
	./$(JSON_TEST_TARGET)

fulltest: $(JSON_TEST_TARGET)
	./$(JSON_TEST_TARGET)

clean:
	rm -f $(OBJS) $(TEST_OBJS) $(JSON_TEST_OBJS) $(TARGET) $(TEST_TARGET) $(JSON_TEST_TARGET)

.PHONY: all test jsontest fulltest clean
