# Sary-Shagan for VCV Rack 2 — the disting NT plug-in in a native module.
#   make RACK_DIR=<Rack-SDK>            plugin.dll / .so / .dylib
#   make RACK_DIR=<Rack-SDK> dist       plugin folder dist/3F4-SaryShagan (ZIP it, see README)
#   make RACK_DIR=<Rack-SDK> install    into the Rack user folder
#   make -C test                        native host test (no Rack needed)
#   python3 panel_def.py                panel SVG + src/panel_layout.hpp
RACK_DIR ?= ../../Rack-SDK

NT := ..
include sources.mk

FLAGS    += $(SARY_INCS)
CFLAGS   += -Wno-unused
EXTRA_CXXFLAGS += -std=c++17
CXXFLAGS += -Wno-suggest-override -Wno-unused-parameter -Wno-missing-field-initializers

SOURCES += src/plugin.cpp src/SaryShagan.cpp

# Shared sources from outside src/: objects in build/ext.
EXT_SRC := $(filter-out src/%,$(SARY_CXX))
OBJECTS += $(patsubst %,build/ext/%.o,$(notdir $(EXT_SRC)))
vpath %.cpp $(sort $(dir $(EXT_SRC)))

DISTRIBUTABLES += res README.md LICENSE

include $(RACK_DIR)/plugin.mk

# No libgcc DLL next to the plug-in: link it statically, like libstdc++.
ifdef ARCH_WIN
	LDFLAGS += -static-libgcc
endif

build/ext/%.cpp.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

-include $(wildcard build/ext/*.d)
