all: bpc

bpc: 5150emu.o intel8086.o
	gcc -o B8086 5150emu.o intel8086.o
	
5150emu.o: 5150emu.c
	gcc -c 5150emu.c
	
intel8086.o: intel8086.c
	gcc -c intel8086.c
	
clean:
	rm -rf *o B8086