# Define variables for paths and libraries
DECTALK_INCLUDE = /opt/dectalk/include
DECTALK_LIB = /opt/dectalk/lib
HOMEBREW_INCLUDE = /opt/homebrew/include
HOMEBREW_LIB = /opt/homebrew/lib

# Define the libraries
LIBS = -ltts -luv -lportaudio -lsndfile
RPATH = -Wl,-rpath,$(DECTALK_LIB)

# Define the target executable
TARGET = omnivox

# Declare phony targets
.PHONY: all run clean

# Default target
all: $(TARGET)

$(TARGET): omnivox.c
	gcc $^ -o $@ \
		-I$(DECTALK_INCLUDE) \
		-L$(DECTALK_LIB) \
		-I$(HOMEBREW_INCLUDE) \
		-L$(HOMEBREW_LIB) \
		$(LIBS) \
		$(RPATH) \
		-Wall -Wextra -Wpedantic -Werror -Wshadow -Wformat=2 -Wfloat-equal -Wundef -Wconversion \
		-std=c11 -D_FORTIFY_SOURCE=2

# Run the executable
run: $(TARGET)
	./$(TARGET)

# Clean build artifacts and .wav files
clean:
	rm -f $(TARGET) *.wav

watch:
	find *.c | entr -r make 
