SOURCES=$(addsuffix .cpp, Snipes $(addprefix sdl/, console keyboard sound timer))
CFLAGS=-fpermissive $(shell sdl2-config --cflags --libs) -lm # -g -fsanitize=address -fsanitize=undefined

snipes : $(SOURCES) GNUmakefile
	g++ -o $@ $(CFLAGS) $(SOURCES)
