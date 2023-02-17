#!/usr/bin/env make
#
# entropic - measure the amount of entropy found within input records
#
# Please do not copyright this Makefile.  This Makefile is in the public domain.
#
# LANDON CURT NOLL DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
# INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO
# EVENT SHALL LANDON CURT NOLL BE LIABLE FOR ANY SPECIAL, INDIRECT OR
# CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF
# USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
# OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
# PERFORMANCE OF THIS SOFTWARE.
#
# chongo <was here> /\oo/\
#
# Share and enjoy!

SHELL= bash
CC= cc
CFLAGS= -O3 -g3
BINMODE= 0555
DESTBIN= /usr/local/bin
INSTALL= install
RM= rm

all: entropic

entropic: entropic.c
	${CC} ${CFLAGS} entropic.c -o $@ -lm

install: all
	${INSTALL} -m ${BINMODE} entropic ${DESTBIN}

clean:
	${RM} -f entropic.o

clobber: clean
	${RM} -f entropic
