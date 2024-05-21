all: helper.c helper.h monitor.h WriteOutput.c WriteOutput.h simulator.cpp
	gcc -c helper.c
	gcc -c WriteOutput.c
	g++ -o simulator simulator.cpp WriteOutput.o helper.o monitor.h -I ./