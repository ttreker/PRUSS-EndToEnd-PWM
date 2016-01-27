all : pruss-ete client-ete data-pru0.bin ddr-pru1.bin

pruss-ete : pruss-ete.c
	gcc -g pruss-ete.c -o pruss-ete -lpthread -lprussdrv

client-ete : client-ete.c
	gcc -g client-ete.c -o client-ete

data-pru0.bin : data-pru0.p prussints.hp
	pasm -b -V3 data-pru0.p

ddr-pru1.bin : ddr-pru1.p prussints.hp
	pasm -b -V3 ddr-pru1.p

