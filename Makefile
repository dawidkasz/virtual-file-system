CC = gcc

build: vfs.c
	$(CC) -o vfs vfs.c

test:
	./test.sh

clean:
	rm disk test_file new_host_file

help:
	./vfs help
