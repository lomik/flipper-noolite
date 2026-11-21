CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined

test: test_protocol
	./test_protocol

test_protocol: test_protocol.c ../noolite_protocol.c ../noolite_protocol.h
	$(CC) $(CFLAGS) -o $@ test_protocol.c ../noolite_protocol.c

clean:
	rm -f test_protocol

.PHONY: test clean
