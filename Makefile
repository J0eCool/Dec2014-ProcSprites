all:
	mkdir -p out
	g++ -std=c++11 Dec2014-ProcSprites/Main.cpp -framework SDL2 -framework SDL2_image -D MAC -o out/spriteGen
