.PHONY: install

bluebird: *.c
	/usr/bin/cc -Os -pedantic -std=c11 -Wall -Wno-missing-braces -o bluebird *.c

install: bluebird
	mv bluebird /usr/local/bin/bluebird
