# Sary-Shagan – Dattorro-Plate-Reverb für den disting NT (Port von Valley Plateau)
#
#   make              plugins/sary_shagan.o   NT-Plug-in (arm-none-eabi-gcc)
#   make test         Simulation ohne Hardware: DSP, Bedienung, Display (g++)
#   make win          emu/sary_shagan.dll     für nt_emu in VCV Rack (MinGW)
#   make render       render/sary-shagan-screens.png  alle Bildschirme, pixelgenau
#   make list         render/parameter.tsv    Parameterliste für den Handbuch-Anhang
#   make compare      Vergleich mit dem Original-Plateau (Submodul third_party/ValleyRackFree)
#   make handbuch     docs/sary-shagan-handbuch.html (braucht render, list und das Panelbild)
#   make release      release/: .o, .dll und das VCV-Modul als ZIP (braucht RACK_DIR, siehe vcv/README.md)
#
# Vorher einmal: git submodule update --init

NT_API_PATH ?= distingNT_API
INCLUDE_PATH := $(NT_API_PATH)/include
VALLEY ?= third_party/ValleyRackFree
VCV_VERSION := $(shell python3 -c "import json;print(json.load(open('vcv/plugin.json'))['version'])")

all: plugins/sary_shagan.o

plugins/sary_shagan.o: sary_shagan.cpp sary_shagan_dsp.h
	mkdir -p plugins
	arm-none-eabi-c++ -std=c++17 -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -fno-rtti -fno-exceptions -O2 -fPIC -Wall -I$(INCLUDE_PATH) -c -o $@ $<

# Simulation ohne Hardware: DSP, Bedienung, Display
test: test/host
	./test/host

test/host: test/host.cpp sary_shagan.cpp sary_shagan_dsp.h
	g++ -std=c++17 -O2 -Wall -I$(INCLUDE_PATH) -o $@ test/host.cpp

# Bildschirm-Renderings (pixelgenau mit den nt_emu-Schriften) -> render/sary-shagan-screens.png
render: test/host
	rm -rf render/frames && mkdir -p render/frames
	./test/host --render render/frames > /dev/null
	python3 render/sheet.py render/frames render/sary-shagan-screens.png

# Parameterliste (Seite, Name, Bereich, Standard) so, wie das NT-Menü sie zeigt
list: test/host
	./test/host --list > render/parameter.tsv

# Vergleich mit dem Original-Plateau
compare: test/compare
	./test/compare

test/compare: test/compare.cpp sary_shagan_dsp.h
	g++ -std=c++17 -O2 -w -I$(VALLEY)/src -I. -o $@ test/compare.cpp $(VALLEY)/src/Plateau/Dattorro.cpp $(VALLEY)/src/dsp/filters/OnePoleFilters.cpp

# Windows-DLL für den VCV-Rack-Emulator nt_emu (Brücke in emu/nt_win_shim.*)
WINCXX ?= x86_64-w64-mingw32-g++
WINSTRIP ?= x86_64-w64-mingw32-strip
emu/sary_shagan.dll: sary_shagan.cpp sary_shagan_dsp.h emu/nt_win_shim.cpp emu/nt_win_shim.h
	$(WINCXX) -std=c++17 -O2 -Wall -Wno-parentheses -shared -include emu/nt_win_shim.h -I$(INCLUDE_PATH) -o $@ sary_shagan.cpp emu/nt_win_shim.cpp -static-libgcc -static-libstdc++ -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic -lpsapi
	$(WINSTRIP) $@

win: emu/sary_shagan.dll

# Handbuch: Quelle docs/handbuch/, Bilder und Anhang aus dem Plug-in
handbuch: render list
	$(MAKE) -C vcv/test
	python3 docs/handbuch/build.py

# Alles, was man ausliefert, nach release/
release: all win
	-$(MAKE) -C vcv dist   # endet ohne zstd mit Fehler; der Ordner dist/3F4-SaryShagan reicht
	rm -rf release && mkdir -p release
	cp plugins/sary_shagan.o emu/sary_shagan.dll release/
	test -f vcv/dist/3F4-SaryShagan/plugin.dll
	cd vcv/dist && rm -f ../../release/*.zip && zip -qr ../../release/3F4-SaryShagan-$(VCV_VERSION)-win-x64.zip 3F4-SaryShagan
	ls -l release

clean:
	rm -rf plugins test/host test/compare emu/sary_shagan.dll render/frames release

.PHONY: all test render list compare clean win handbuch release
