CC = gcc
SRCS = reference_counting.c reference_counting_test.c
TARGET = reference_counting

gc: $(SRCS)
	$(CC) -g -o $(TARGET) $(SRCS)

clean:
	rm -f $(TARGET)