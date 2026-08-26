CC := c++
CFLAGS := -std=c++11 -Wall -Wextra -g -fsanitize=address,undefined

all: test_four_dsp test_four_engine test_vortex_dsp test_brink_dsp test_polyphony test_brink_panel

test_four_dsp: test_four_dsp.cpp ../src/Four/dsp.h
	$(CC) $(CFLAGS) -o $@ $< -lm

test_four_engine: test_four_engine.cpp ../src/Four/engine.h ../src/Four/dsp.h
	$(CC) $(CFLAGS) -o $@ $< -lm

test_vortex_dsp: test_vortex_dsp.cpp ../src/Vortex/dsp.h
	$(CC) $(CFLAGS) -o $@ $< -lm

test_brink_dsp: test_brink_dsp.cpp ../src/Brink/dsp.h
	$(CC) $(CFLAGS) -o $@ $< -lm

test_polyphony: test_polyphony.cpp ../src/polyphony.h
	$(CC) $(CFLAGS) -o $@ $< -lm

test_brink_panel:
	python3 test_brink_panel.py

run: test_four_dsp test_four_engine test_vortex_dsp test_brink_dsp test_polyphony test_brink_panel
	./test_four_dsp
	./test_four_engine
	./test_vortex_dsp
	./test_brink_dsp
	./test_polyphony
	python3 test_brink_panel.py

clean:
	rm -f test_four_dsp test_four_engine test_vortex_dsp test_brink_dsp test_polyphony

.PHONY: all run clean test_brink_panel
