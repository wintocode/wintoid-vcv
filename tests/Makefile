CC := c++
CFLAGS := -std=c++11 -Wall -Wextra -g -fsanitize=address,undefined -pthread

all: test_four_dsp test_four_engine test_vortex_dsp test_brink_dsp test_polyphony test_brink_panel test_panel_output_style test_polyphony_metadata test_repository_hygiene

test_four_dsp: test_four_dsp.cpp ../src/Four/dsp.h
	$(CC) $(CFLAGS) -o $@ $< -lm

test_four_engine: test_four_engine.cpp ../src/Four/engine.h ../src/Four/dsp.h
	$(CC) $(CFLAGS) -o $@ $< -lm

test_vortex_dsp: test_vortex_dsp.cpp ../src/Vortex/dsp.h
	$(CC) $(CFLAGS) -o $@ $< -lm

test_brink_dsp: test_brink_dsp.cpp ../src/Brink/dsp.h ../src/polyphony.h
	$(CC) $(CFLAGS) -o $@ $< -lm

test_polyphony: test_polyphony.cpp ../src/polyphony.h
	$(CC) $(CFLAGS) -o $@ $< -lm

test_brink_panel:
	python3 test_brink_panel.py

test_panel_output_style:
	python3 test_panel_output_style.py

test_polyphony_metadata:
	python3 test_polyphony_metadata.py

test_repository_hygiene:
	python3 test_repository_hygiene.py

run: test_four_dsp test_four_engine test_vortex_dsp test_brink_dsp test_polyphony test_brink_panel test_panel_output_style test_polyphony_metadata test_repository_hygiene
	./test_four_dsp
	./test_four_engine
	./test_vortex_dsp
	./test_brink_dsp
	./test_polyphony
	python3 test_brink_panel.py
	python3 test_panel_output_style.py
	python3 test_polyphony_metadata.py
	python3 test_repository_hygiene.py

clean:
	rm -f test_four_dsp test_four_engine test_vortex_dsp test_brink_dsp test_polyphony

.PHONY: all run clean test_brink_panel test_panel_output_style test_polyphony_metadata test_repository_hygiene
