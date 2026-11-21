# Native test of the VCV host with the real plug-in (no Rack needed).
#   make -C vcv/test         run the checks
#   make -C vcv/test render  host-screens.png
NT := ../..
include $(NT)/vcv/sources.mk

CXXFLAGS := -std=c++17 -O2 -g -Wall -Wno-unused-function $(SARY_INCS)

host_test: host_test.cpp $(SARY_CXX)
	g++ $(CXXFLAGS) -o $@ $^

all: host_test
	./host_test > frames.txt

render: all
	python3 render.py frames.txt host-screens.png

# Panel preview with Rack's SVG renderer (needs RACK_DIR with res/ComponentLibrary and dep/include)
RACK_DIR ?= ../../../Rack-SDK
preview: preview.cpp
	g++ -O2 -I$(RACK_DIR)/dep/include -o $@ $< -lm
panel: preview
	./preview $(RACK_DIR)/res/ComponentLibrary ../res/SaryShagan.svg panel_parts.txt 2 panel-preview.ppm
	python3 -c "from PIL import Image; Image.open('panel-preview.ppm').save('panel-preview.png')"
	rm -f panel-preview.ppm

clean:
	rm -f host_test frames.txt preview panel-preview.ppm

.DEFAULT_GOAL := all
.PHONY: all render panel clean
