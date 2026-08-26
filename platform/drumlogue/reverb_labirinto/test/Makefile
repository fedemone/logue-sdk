# Host-side tests for the NeonLabirinto reverb engine.
#
# These build the real NeonAdvancedLabirinto.h on the development machine by
# putting a scalar stand-in for <arm_neon.h> and float_math.h ahead of the SDK
# ones on the include path. No cross toolchain or hardware needed.
#
# The suite is built twice. The second build adds -ffast-math, because that is
# what the drumlogue Makefile compiles the unit with, and it is not a neutral
# flag for this code: it implies -ffinite-math-only, under which the compiler
# assumes no value is ever NaN or infinite and folds away any comparison written
# to detect one. The engine's watchdog is exactly such a comparison. Building
# only at plain -O2 tested a configuration that does not ship.
#
#   make        build and run both configurations
#   make ieee    just the strict-IEEE build
#   make fast    just the -ffast-math build
#   make clean

CXX      ?= g++
CXXFLAGS ?= -std=c++14 -O2 -Wall -Wextra -Wno-unused-parameter
FASTFLAGS = -ffast-math
INCLUDES  = -I shim -I ..

TARGET = test_reverb
DEPS    = $(TARGET).cpp ../NeonAdvancedLabirinto.h shim/arm_neon.h shim/float_math.h

.PHONY: all ieee fast clean
all: ieee fast

$(TARGET): $(DEPS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $(TARGET).cpp -lm

$(TARGET)_fast: $(DEPS)
	$(CXX) $(CXXFLAGS) $(FASTFLAGS) $(INCLUDES) -o $@ $(TARGET).cpp -lm

ieee: $(TARGET)
	@echo "=== strict IEEE (-O2) ==="
	./$(TARGET)

fast: $(TARGET)_fast
	@echo "=== as shipped (-O2 -ffast-math) ==="
	./$(TARGET)_fast

clean:
	rm -f $(TARGET) $(TARGET)_fast
