CXX ?= g++
CXXFLAGS ?= -std=c++11 -Wall -Wextra -pedantic

TARGET := test_real_cymb
SOURCES := realistic_cymbals.cpp test_real_cymb.cpp
WAVS := real_cymb_crash.wav real_cymb_ride.wav real_cymb_splash.wav real_cymb_gong.wav

.PHONY: all render clean

all: $(TARGET)

$(TARGET): $(SOURCES) realistic_cymbals.h
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $@

render: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) $(WAVS)
