SOURCES=$(addsuffix .cpp, Snipes $(addprefix sdl/, console keyboard sound timer))
CFLAGS=$(shell sdl2-config --cflags --libs) -lm -lSDL2_ttf # -g -fsanitize=address -fsanitize=undefined

snipes : $(SOURCES) config.h GNUmakefile
	g++ -o $@ $(CFLAGS) $(SOURCES)
