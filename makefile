all: vcube

vcube: vcube.o smpl.o rand.o
	$(LINK.cpp) -o $@ -Bstatic vcube.o smpl.o rand.o -lm

smpl.o: smpl.cpp smpl.h
	$(COMPILE.cpp)  -g smpl.cpp

vcube.o: vcube.cpp smpl.h
	$(COMPILE.cpp) -g  vcube.cpp -std=c++17

rand.o: rand.c
	$(COMPILE.c) -g rand.c

clean:
	$(RM) *.o vcube relat saida
