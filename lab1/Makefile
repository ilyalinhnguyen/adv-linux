CC := cc
CFLAGS := -std=c11 -O2 -Wall -Wextra -pedantic
TARGET := bldd
SRC := bldd.c

.PHONY: all clean run-help sample-report

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

run-help: $(TARGET)
	./$(TARGET) --help

sample-report: $(TARGET)
	./$(TARGET) libc.so.6 libm.so.6 --scan-dir /bin --arch x86_64 --output report_task1.txt

clean:
	rm -f $(TARGET)
