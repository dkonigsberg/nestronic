# Simple Makefile

AS65 = ca65
LD65 = ld65

all: nestronic.nes

main.o: main.s
	$(AS65) -g main.s

nestronic.nes: main.o
	$(LD65) -C nes.cfg -o nestronic.nes main.o

clean:
	rm *.o nestronic.nes
