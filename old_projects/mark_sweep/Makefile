PROJECT := $(shell pwd)
SRCDIR  := $(PROJECT)/../../
TEST := $(wildcard $(PROJECT)/*.c)
SRC := $(wildcard $(SRCDIR)/minigc/*.c)
override SRC := $(SRC) $(TEST) 
TARGET := mark_sweep_test
CXX := gcc
INCLUDE := -I $(SRCDIR)/src/ -I $(PROJECT)
CFLAGS :=  -g


$(TARGET): $(SRC)
	$(CXX) $(CFLAGS) $(INCLUDE) -o $@

clean :
	find . -name '*.o' | xargs rm -f
	find . -name $(TARGET) | xargs rm -f