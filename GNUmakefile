SOURCES=$(addsuffix .cpp, Snipes $(addprefix sdl/, console keyboard sound timer))
CFLAGS=-std=c++11 $(shell sdl2-config --cflags)
# Sources use <SDL2/...> includes, so also add the parent include dir (needed for Homebrew)
CFLAGS+=-I$(shell sdl2-config --prefix)/include
CFLAGS+=$(if $(MAINT),-Werror -Wall -Wextra,)
CFLAGS2=-O3 -fstack-protector
#CFLAGS2=-Og -g -fsanitize=address -fsanitize=undefined
LDFLAGS=$(shell sdl2-config --libs) -lm -lSDL2_ttf
CXX=g++
#CXX="clang++"

snipes : $(SOURCES) config.h GNUmakefile
	$(CXX) -o $@ $(CFLAGS) $(CFLAGS2) $(SOURCES) $(LDFLAGS)

config.h :
	@echo "Automatically creating config.h with default settings - edit this file to customize your build"
	cp config-sample.h config.h

clean :
	rm -f snipes
