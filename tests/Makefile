CC := c++
CFLAGS := -std=c++11 -Wall -Wextra -g -fsanitize=address,undefined

all: test_four_dsp test_four_engine test_vortex_dsp

test_four_dsp: test_four_dsp.cpp ../src/Four/dsp.h
	$(CC) $(CFLAGS) -o $@ $< -lm

test_four_engine: test_four_engine.cpp ../src/Four/engine.h ../src/Four/dsp.h
	$(CC) $(CFLAGS) -o $@ $< -lm

test_vortex_dsp: test_vortex_dsp.cpp ../src/Vortex/dsp.h
	$(CC) $(CFLAGS) -o $@ $< -lm

run: test_four_dsp test_four_engine test_vortex_dsp
	./test_four_dsp
	./test_four_engine
	./test_vortex_dsp

clean:
	rm -f test_four_dsp test_four_engine test_vortex_dsp

.PHONY: all run clean
