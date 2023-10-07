CC = gcc
SRCS = mark_compact.c mark_compact_test.c
TARGET = mark_compact

gc: $(SRCS)
	$(CC) -g -o $(TARGET) $(SRCS)

clean:
	rm -f $(TARGET)