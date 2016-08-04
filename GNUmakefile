SOURCES=$(addsuffix .cpp, Snipes $(addprefix sdl/, console keyboard sound timer))
CFLAGS=-Werror -Wall -Wextra -O1 $(shell sdl2-config --cflags) # -g -fsanitize=address -fsanitize=undefined
LDFLAGS=$(shell sdl2-config --libs) -lm -lSDL2_ttf

snipes : $(SOURCES) config.h GNUmakefile
	g++ -o $@ $(CFLAGS) $(SOURCES) $(LDFLAGS)
