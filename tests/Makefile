CC := c++
CFLAGS := -std=c++11 -Wall -Wextra -g -fsanitize=address,undefined -pthread

all: test_four_dsp test_four_engine test_four_v2_model test_four_v2_routing test_four_v2_dsp test_four_v2_engine test_vortex_dsp test_vortex_v2_dsp test_vortex_v2_runtime test_brink_dsp test_polyphony test_ui_geometry test_panel_geometry test_brink_panel test_four_v2_panel test_four_v2_module test_vortex_v2_panel test_vortex_v2_module test_panel_output_style test_polyphony_metadata test_repository_hygiene test_panel_labels test_metamodule_graphics

test_four_dsp: test_four_dsp.cpp ../src/Four/dsp.h
	$(CC) $(CFLAGS) -o $@ $< -lm

test_four_engine: test_four_engine.cpp ../src/Four/engine.h ../src/Four/dsp.h
	$(CC) $(CFLAGS) -o $@ $< -lm

test_four_v2_model: test_four_v2_model.cpp ../src/FourV2/model.h
	$(CC) $(CFLAGS) -o $@ $< -lm

test_four_v2_routing: test_four_v2_routing.cpp ../src/FourV2/routing.h ../src/FourV2/model.h ../src/FourV2/layout.h
	$(CC) $(CFLAGS) -o $@ $< -lm

test_four_v2_dsp: test_four_v2_dsp.cpp ../src/FourV2/dsp.h
	$(CC) $(CFLAGS) -o $@ $< -lm

test_four_v2_engine: test_four_v2_engine.cpp ../src/FourV2/engine.h ../src/FourV2/dsp.h ../src/FourV2/model.h
	$(CC) $(CFLAGS) -o $@ $< -lm

test_vortex_dsp: test_vortex_dsp.cpp ../src/Vortex/dsp.h
	$(CC) $(CFLAGS) -o $@ $< -lm

test_vortex_v2_dsp: test_vortex_v2_dsp.cpp ../src/VortexV2/dsp.h ../src/Vortex/dsp.h
	$(CC) $(CFLAGS) -o $@ $< -lm

test_vortex_v2_runtime: test_vortex_v2_runtime.cpp ../src/VortexV2/runtime.h ../src/polyphony.h
	$(CC) $(CFLAGS) -o $@ $< -lm

test_brink_dsp: test_brink_dsp.cpp ../src/Brink/dsp.h ../src/polyphony.h
	$(CC) $(CFLAGS) -o $@ $< -lm

test_polyphony: test_polyphony.cpp ../src/polyphony.h
	$(CC) $(CFLAGS) -o $@ $< -lm

test_ui_geometry: test_ui_geometry.cpp ../src/ui_geometry.h
	$(CC) $(CFLAGS) -o $@ $< -lm

test_panel_geometry:
	python3 test_panel_geometry.py

test_brink_panel:
	python3 test_brink_panel.py

test_four_v2_panel:
	python3 test_four_v2_panel.py

test_four_v2_module:
	python3 test_four_v2_module.py

test_vortex_v2_panel:
	python3 test_vortex_v2_panel.py

test_vortex_v2_module:
	python3 test_vortex_v2_module.py

test_panel_output_style:
	python3 test_panel_output_style.py

test_polyphony_metadata:
	python3 test_polyphony_metadata.py

test_repository_hygiene:
	python3 test_repository_hygiene.py

test_panel_labels:
	python3 test_panel_labels.py

test_metamodule_graphics:
	python3 test_metamodule_graphics.py

run: test_four_dsp test_four_engine test_four_v2_model test_four_v2_routing test_four_v2_dsp test_four_v2_engine test_vortex_dsp test_vortex_v2_dsp test_vortex_v2_runtime test_brink_dsp test_polyphony test_ui_geometry test_panel_geometry test_brink_panel test_four_v2_panel test_four_v2_module test_vortex_v2_panel test_vortex_v2_module test_panel_output_style test_polyphony_metadata test_repository_hygiene test_panel_labels test_metamodule_graphics
	./test_four_dsp
	./test_four_engine
	./test_four_v2_model
	./test_four_v2_routing
	./test_four_v2_dsp
	./test_four_v2_engine
	./test_vortex_dsp
	./test_vortex_v2_dsp
	./test_vortex_v2_runtime
	./test_brink_dsp
	./test_polyphony
	./test_ui_geometry
	python3 test_panel_geometry.py
	python3 test_brink_panel.py
	python3 test_four_v2_panel.py
	python3 test_four_v2_module.py
	python3 test_vortex_v2_panel.py
	python3 test_vortex_v2_module.py
	python3 test_panel_output_style.py
	python3 test_polyphony_metadata.py
	python3 test_repository_hygiene.py
	python3 test_panel_labels.py
	python3 test_metamodule_graphics.py

clean:
	rm -f test_four_dsp test_four_engine test_four_v2_model test_four_v2_routing test_four_v2_dsp test_four_v2_engine test_vortex_dsp test_vortex_v2_dsp test_vortex_v2_runtime test_brink_dsp test_polyphony test_ui_geometry

.PHONY: all run clean test_four_v2_model test_four_v2_routing test_four_v2_dsp test_four_v2_engine test_vortex_v2_dsp test_vortex_v2_runtime test_panel_geometry test_brink_panel test_four_v2_panel test_four_v2_module test_vortex_v2_panel test_vortex_v2_module test_panel_output_style test_polyphony_metadata test_repository_hygiene test_panel_labels test_metamodule_graphics
