# Define variables for paths and libraries
DECTALK_INCLUDE = /opt/dectalk/include
DECTALK_LIB = /opt/dectalk/lib
HOMEBREW_INCLUDE = /opt/homebrew/include
HOMEBREW_LIB = /opt/homebrew/lib

# Define the compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c11
INCLUDES = -I$(DECTALK_INCLUDE) -I$(HOMEBREW_INCLUDE)
LDFLAGS = -L$(DECTALK_LIB) -L$(HOMEBREW_LIB)
LIBS = -ltts -luv -lportaudio -lsndfile
RPATH = -Wl,-rpath,$(DECTALK_LIB)

# Define the target executable
TARGET = omnivox

# Define source files
SRCS = ov_main.c ov_input.c ov_audio.c ov_processing.c ov_output.c ov_commands.c
OBJS = $(SRCS:.c=.o)

# Declare phony targets
.PHONY: all run clean

# Default target
all: $(TARGET)

# Build the target executable
$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS) $(LIBS) $(RPATH)

# Compile source files
%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Run the executable
run: $(TARGET)
	./$(TARGET)

# Clean build artifacts and .wav files
clean:
	rm -f $(TARGET) $(OBJS) *.wav
