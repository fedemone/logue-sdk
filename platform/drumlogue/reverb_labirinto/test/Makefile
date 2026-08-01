# Host-side tests for the NeonLabirinto reverb engine.
#
# These build the real NeonAdvancedLabirinto.h on the development machine by
# putting a scalar stand-in for <arm_neon.h> and float_math.h ahead of the SDK
# ones on the include path. No cross toolchain or hardware needed.
#
#   make        build and run
#   make clean

CXX      ?= g++
CXXFLAGS ?= -std=c++14 -O2 -Wall -Wextra -Wno-unused-parameter
INCLUDES  = -I shim -I ..

TARGET = test_reverb

.PHONY: all run clean
all: run

$(TARGET): $(TARGET).cpp ../NeonAdvancedLabirinto.h shim/arm_neon.h shim/float_math.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $(TARGET).cpp -lm

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
