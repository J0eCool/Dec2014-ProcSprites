FRAMEWORKS=-framework SDL2 -framework SDL2_image
LIBRARIES=-lboost_system -lboost_filesystem
DEFINES=-D MAC

all:
	mkdir -p out
	g++ -std=c++11 Dec2014-ProcSprites/Main.cpp $(FRAMEWORKS) $(LIBRARIES) $(DEFINES) -o out/spriteGen
