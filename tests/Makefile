CC := c++
CFLAGS := -std=c++11 -Wall -Wextra -g -fsanitize=address,undefined

all: test_fourmm_dsp test_fourmm_engine test_vortexmm_dsp

test_fourmm_dsp: test_fourmm_dsp.cpp ../src/FourMM/dsp.h
	$(CC) $(CFLAGS) -o $@ $< -lm

test_fourmm_engine: test_fourmm_engine.cpp ../src/FourMM/engine.h ../src/FourMM/dsp.h
	$(CC) $(CFLAGS) -o $@ $< -lm

test_vortexmm_dsp: test_vortexmm_dsp.cpp ../src/VortexMM/dsp.h
	$(CC) $(CFLAGS) -o $@ $< -lm

run: test_fourmm_dsp test_fourmm_engine test_vortexmm_dsp
	./test_fourmm_dsp
	./test_fourmm_engine
	./test_vortexmm_dsp

clean:
	rm -f test_fourmm_dsp test_fourmm_engine test_vortexmm_dsp

.PHONY: all run clean
