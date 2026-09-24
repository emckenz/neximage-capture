CFLAGS ?= -std=c11 -Wall -Wextra -Werror -I.
SRCS = uvc_format.c uvc_desc.c uvc_assemble.c bayer_score.c

.PHONY: test clean

test: test_uvc
	./test_uvc

test_uvc: test_uvc.c $(SRCS)
	$(CC) $(CFLAGS) -o $@ test_uvc.c $(SRCS)

clean:
	rm -f test_uvc
