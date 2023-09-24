CC = gcc
SRCS = mark_sweep.c mark_sweep_test.c
TARGET = mark_sweep

gc: $(SRCS)
	$(CC) -g -o $(TARGET) $(SRCS)

clean:
	rm -f $(TARGET)