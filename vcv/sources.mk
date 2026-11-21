# Sources of the VCV plug-in besides the Rack glue: the NT host and the Sary-Shagan
# NT plug-in (unchanged). Set NT (the repository root) before including.
SARY_CXX  := $(NT)/vcv/src/nt_host.cpp $(NT)/vcv/src/nt_api.cpp $(NT)/vcv/src/nt_globals.cpp \
               $(NT)/sary_shagan.cpp
SARY_INCS := -I$(NT)/distingNT_API/include -I$(NT) -I$(NT)/vcv/src -I$(NT)/vcv/src/fonts
