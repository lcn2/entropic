/*
 * entropic - measure the amount of entropy found within input records
 *
 * @(#) $Revision: 1.2 $
 * @(#) $Id: entropic.c,v 1.2 2003/01/28 09:57:39 chongo Exp chongo $
 * @(#) $Source: /usr/local/src/cmd/entropic/RCS/entropic.c,v $
 *
 * Copyright (c) 2003 by Landon Curt Noll.  All Rights Reserved.
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose and without fee is hereby granted,
 * provided that the above copyright, this permission notice and text
 * this comment, and the disclaimer below appear in all of the following:
 *
 *       supporting documentation
 *       source copies
 *       source works derived from this source
 *       binaries derived from this source or from derived source
 *
 * LANDON CURT NOLL DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO
 * EVENT SHALL LANDON CURT NOLL BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF
 * USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 * chongo (Landon Curt Noll, http://www.isthe.com/chongo/index.html) /\oo/\
 *
 * Share and enjoy! :-)
 */


#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <errno.h>


/*
 * defaults
 *
 * OCTET_BITS	number of bits in an 8 bit octet
 *
 * DEF_DEPTH	default tally depth (-b) for each record bit
 *
 * HISTORY_BITS	We must have this many records before we have a full
 * 		history's worth of values for a given bit postion in a record.
 * 		Bit histories are kept in a u_int64_t.
 *
 * BACK_HISTORY	When we form xors of current values and history values,
 * 		we will go back in history up to this many bits.
 *
 * MAX_DEPTH	Deeper tally depths require more memory.  An increase in 1
 * 		for the depth requires twice as much memory.  A deeper tally
 * 		has a shorter history from which bit differences can be
 * 		examined.
 *
 * 		For each bit depth, we need BACK_HISTORY more bits in the
 * 		history.  So MAX_DEPTH+BACK_HISTORY must be <= HISTORY_BITS.
 * 		We go one less so that index offsets fit within signed 32 bits.
 * 		Most systems will not be able to allocate this much memory,
 * 		but we have to draw a limit somewhere.
 */
#define OCTET_BITS 8
#define DEF_DEPTH 12
#define HISTORY_BITS (sizeof(u_int64_t)*OCTET_BITS)
#define BACK_HISTORY (HISTORY_BITS/2)
#define MAX_DEPTH (BACK_HISTORY-1)


/*
 * tally_t - tally counter type
 *
 * If you want to process more than 512 Megabytes == 2^32 bits of
 * input, you must define HUGE_INPUT so that counters can be 64
 * instead of 32 bits.  When HUGE_INPUT is defined, a tally_t is large
 * enough to hold a tally count of 18446744073709551615 values.
 * Without HUGE_INPUT, a tally_t can only hold a count as
 * large as 4294967295.
 */
#if defined(HUGE_INPUT)
typedef u_int32_t tally_t;
#else
typedef u_int64_t tally_t;
#endif


/*
 * bitslice - tables and tally arrays for a given bit position in the record
 *
 * hist[i]
 * 	The tally table for the xor of the current bit history with
 * 	the bit history 'i' records back.
 *
 * 	The layout of a given tally array is defined in alloc_bittally()'s
 * 	comments.  For our example, simply note that hist[i][8] thru
 * 	hist[i][15] hold the 8 tally values for all possible 3-bit
 * 	combinations.  So hist[i][8] is a tally of all '000' 3-bit values.
 * 	And hist[i][9] is a tally of all '001' 3-bit values.
 * 	And hist[i][10] is a tally of all '010' 3-bit values.  ...
 *
 * 	Therefore hist[5][10] holds a tally of all '010' 3-bit values
 * 	that are computed by the xor of the current bit history
 * 	and the bit history 5 records back.  If b0 is the current
 * 	bit value, b1 is the previous bit value, b2 as the bit value, ...
 *
 * 	Using the notation that b0 is the current value, b1 previous,
 * 	b2 the bit value before that, we have:
 *
 * 	    hist[5][10] = count when xor( b2b1b0 , b7b6b5 ) was '010'
 * 	    hist[5][11] = count when xor( b2b1b0 , b7b6b5 ) was '011'
 * 	    hist[5][12] = count when xor( b2b1b0 , b7b6b5 ) was '100'
 *
 * 	    hist[6][12] = count when xor( b2b1b0 , b8b7b6 ) was '100'
 * 	    hist[7][12] = count when xor( b2b1b0 , b9b8b7 ) was '100'
 *
 * 	    hist[5][4] = count when xor( b1b0 , b6b5 ) was '00'
 * 	    hist[5][5] = count when xor( b1b0 , b6b5 ) was '01'
 * 	    hist[5][6] = count when xor( b1b0 , b6b5 ) was '10'
 * 	    hist[5][7] = count when xor( b1b0 , b6b5 ) was '11'
 *
 * 	    hist[4][4] = count when xor( b1b0 , b5b4 ) was '00'
 * 	    hist[4][5] = count when xor( b1b0 , b5b4 ) was '01'
 * 	    hist[4][6] = count when xor( b1b0 , b5b4 ) was '10'
 * 	    hist[4][7] = count when xor( b1b0 , b5b4 ) was '11'
 *
 *      assuming that the -b bit_depth was deep enough and hist[i] != NULL.
 *
 * 	As a specical case, hist[0] points to the tally table
 * 	of the current values only.  No xor is performed, thus:
 *
 * 	    hist[0][10] = count when b2b1b0 was '010'
 * 	    hist[0][11] = count when b2b1b0 was '100'
 * 	    hist[0][4]  = count when b1b0 was '00'
 * 	    hist[0][7]  = count when b1b0 was '11'
 */
struct bitslice {
    int bitnum;		/* bit position in record, 0 ==> low order bit */
    u_int64_t history;	/* history of bit positions, bit 0 ==> most recent */
    u_int64_t count;	/* number of bits processed for this position */
    tally_t *hist[MAX_DEPTH+1];	/* tally table for various xor differences */
};


/*
 * usage
 */
char *usage =
	"[-v verbose] [-b bit_depth] [-r rec_size] input_file\n"
	"\n"
	"\t-v verbose		verbose level (def: 0 ==> none)\n"
	"\t-b bit_depth		tally depth for each record bit (def: 12)\n"
	"\t-r rec_size		read rec_size octet records (def: line mode)\n"
	"\tinput_file\tfile to read records from (- ==> stdin)\n";
char *program;			/* our name */
int v_flag = 0;			/* verbosity level */
int bit_depth = DEF_DEPTH;	/* tally depth, in bits, for each record bit */
int rec_size = 0;		/* > 0 ==> record size, 0 ==> line mode */
int line_mode = 1;		/* 0 ==> read binary recs, 1 ==> read lines */


/*
 * forward declarations
 */
static tally_t *alloc_bittally(int depth);
static struct bitslice *alloc_bitslice(int bitnum, int depth);
static void record_bit(struct bitslice *slice, int value);
static void dbg(int level, char *fmt, ...);
static int read_record(FILE *input, u_int8_t *buf, int buf_size, int read_line);

/*
 * misc globals and static values
 */
static tally_t recnum = 0;	/* current record number, starting with 0 */
extern int errno;		/* last system error */


/*
 * main
 */
int
main(int argc, char *argv[])
{
    extern char *optarg;	/* argument to current option */
    extern int optind;		/* first argv-element that is not an option */
    char *filename;		/* name of input file, or - ==> stdin */
    FILE *input;		/* stream from which to read records */
    u_int8_t *raw_buf;		/* raw record input buffer */
    int raw_len;		/* length of raw record in octets */
    int i;

    /*
     * parse args
     */
    program = argv[0];
    while ((i = getopt(argc, argv, "v:b:r:")) != -1) {
	switch (i) {
	case 'v':	/* verbose level */
	    v_flag = strtol(optarg, NULL, 0);
	    break;
	case 'b':	/* tally depth */
	    bit_depth = strtol(optarg, NULL, 0);
	    break;
	case 'r':	/* binary record size */
	    rec_size = strtol(optarg, NULL, 0);
	    line_mode = 0;
	    break;
	default:
	    fprintf(stderr, "usage: %s %s\n", program, usage);
	    exit(1);
	}
    }
    /* requires 1 arg */
    if (optind >= argc) {
	fprintf(stderr, "usage: %s %s\n", program, usage);
	exit(2);
    }
    filename = argv[optind];
    dbg(1, "main: input file: %s", filename);
    if (bit_depth < 1) {
	fprintf(stderr, "%s: -b bit_depth must be > 0\n", program);
	exit(3);
    }
    if (bit_depth > MAX_DEPTH) {
	fprintf(stderr, "%s: -b bit_depth must <= %d\n", program, MAX_DEPTH);
	exit(4);
    }
    dbg(1, "main: bit_depth: %d", bit_depth);
    if (line_mode == 0 && rec_size <= 0) {
	fprintf(stderr, "%s: -r rec_size: %d must be > 0\n",
		program, rec_size);
	exit(5);
    } else if (line_mode == 0) {
	dbg(1, "main: binary record size: %d", rec_size);
    } else {
	rec_size = BUFSIZ;
	dbg(1, "main: line mode of up to %d octets\n", rec_size);
    }

    /*
     * open the file containing records
     */
    if (strcmp(filename, "-") == 0) {
	/* - means read from stdin */
	input = stdin;
    } else {
	input = fopen(filename, "r");
    }
    if (input == NULL) {
	fprintf(stderr, "%s: unable to open for reading: %s\n",
		program, filename);
	exit(6);
    }

    /*
     * allocate raw buffer
     */
    raw_buf = (u_int8_t *)malloc(rec_size+1);
    if (raw_buf == NULL) {
	fprintf(stderr, "%s: failed to allocate raw buffer: %d octets\n",
		program, rec_size+1);
	exit(7);
    }

    /*
     * process records, one at a time
     */
    recnum = 0;
    do {

	/*
	 * read the next record
	 */
	raw_len = read_record(input, raw_buf, rec_size, line_mode);
	if (raw_len < 0) {
	    /* EOF or error */
	    break;
	}

    } while (++recnum > 0);
}


/*
 * alloc_bittally - allocate and initialize the tally array for a bit
 *
 * given:
 * 	depth	tally depth, in bits
 *
 * returns:
 * 	pointer to allocated and initialized tally array
 * 	does not return (exits non-zero) on memory allocation failure
 *
 * The tally array layout:
 *
 * 	unused				(1 value)
 * 	unused				(1 value)
 * 	tally for depth of 1 bit	(2 values)
 * 	tally for depth of 2 bits	(4 values)
 * 	tally for depth of 3 bits	(8 values)
 * 	...
 * 	tally for depth of 'depth' bits	(2**depth values)
 *
 * The total size of the bitslice array is 2**(depth+1) values.
 *
 * The bitslice array is initialized to 0 values.
 */
static tally_t *
alloc_bittally(int depth)
{
    tally_t *ret;	/* allocated bitslice tally layout */
    size_t values;	/* number of values in tally array */

    /*
     * firewall
     */
    if (depth < 1) {
	fprintf(stderr, "%s: alloc_bittally: depth: %d must be > 0\n",
		program, depth);
	exit(5);
    }
    values = 1<<(depth+1);
    if (values < 0 || values == 0) {
	fprintf(stderr, "%s: alloc_bittally: depth: %d is too large\n",
		program, depth);
	exit(6);
    }

    /*
     * allocate and zero
     */
    ret = (tally_t *)calloc(values, sizeof(long));
    if (ret == NULL) {
	fprintf(stderr, "%s: alloc_bittally: "
			"not enough memory for depth: %d\n",
		program, depth);
	exit(7);
    }

    /*
     * return tally array
     */
    return ret;
}


/*
 * alloc_bitslice - allocate and initialize all values given bit position
 *
 * given:
 * 	bitnum		bit number in record for which we are allocating
 * 	depth	tally depth, in bits
 *
 * returns:
 * 	pointer to allocated and initialized bitslice
 * 	does not return (exits non-zero) on memory allocation failure
 */
static struct bitslice *
alloc_bitslice(int bitnum, int depth)
{
    struct bitslice *ret;		/* bit position table */
    int i;

    /*
     * firewall
     */
    if (depth < 1) {
	fprintf(stderr, "%s: alloc_bittbl: depth: %d must be > 0\n",
		program, depth);
	exit(8);
    }

    /*
     * allocate the bitslice
     */
    ret = (struct bitslice *)malloc(sizeof(struct bitslice));
    if (ret == NULL) {
	fprintf(stderr, "%s: cannot allocate struct bitslice\n", program);
	exit(9);
    }

    /*
     * initialize bitslice
     */
    ret->bitnum = bitnum;
    ret->history = 0;
    ret->count = 0;

    /*
     * allocate tally tables for current and past xor differences
     */
    for (i=0; i <= BACK_HISTORY; ++i) {
	ret->hist[i] = alloc_bittally(depth);
    }

    /*
     * return bitslice
     */
    return ret;
}


/*
 * record_bit - record and tally a bit value for a given bitslice
 *
 * given:
 * 	slice	bitslice record for a given bit position in our records
 * 	value	next value for the given bit postion (0 or 1)
 */
static void
record_bit(struct bitslice *slice, int value)
{
    int depth;		/* bit depth being processed */
    int back;		/* number of bits going back into history */
    u_int32_t offset;	/* tally array offset */
    u_int32_t cur;	/* current bit values (for a given depth), xor-ed */
    u_int32_t past;	/* bit values going back into history */

    /*
     * firewall
     */
    if (slice == NULL) {
	fprintf(stderr, "%s: record_bit: slice is NULL\n", program);
	exit(10);
    }

    /*
     * push the value onto the history
     *
     * The new value is shifted into the 0th bit postion of our history.
     * Bit values are either 0 and 1 (non-zero).
     */
    slice->history <<= 1;
    if (value != 0) {
	slice->history |= 1;
    }

    /*
     * We do not do anything if we lack a full history.  We want to
     * be sure that slice->history is full of bit values from actual
     * records.  Count the bit that we just recorded.
     */
    if (++slice->count < HISTORY_BITS) {
	return;
    }

    /*
     * process just the values
     */
    for (depth=1, offset=1; depth <= bit_depth; ++depth, offset <<= 1) {

	/* get the i-depth value - (offset-1) is an i-bit mask of 1's */
	cur = (u_int32_t)slice->history ^ (offset-1);

	/* tally the i-depth value - no x-or with history in the 0 case */
	++slice->hist[0][offset + cur];

	/* tally the i-depth value xored with previous history */
	for (back=1; back <= BACK_HISTORY; ++back) {

	    /* get the i-depth value going back in history h bits */
	    past = (u_int32_t)(slice->history >> back) ^ (offset-1);

	    /* tally the i-depth value xored with history back h bits */
	    ++slice->hist[back][offset + (cur^past)];
	}
    }
    return;
}


/*
 * read_record - read the next record from the input file stream
 *
 * given:
 * 	input	    input file stream to read
 * 	buf	    buffer of buf_size octets if raw_read, else BUFSIZ octets
 * 	buf_size    size of raw buffer in octets, if raw_read
 * 	read_line   1 ==> lines of up to BUFSIZ octets, 0 ==> binary reads
 *
 * return:
 * 	number of octets read, or -1 ==> error
 */
static int
read_record(FILE *input, u_int8_t *buf, int buf_size, int read_line)
{
    int raw_len;		/* length of raw record in octets */

    /*
     * setup to read
     */
    clearerr(input);
    errno = 0;

    /*
     * raw read
     */
    if (read_line == 0) {
	raw_len = fread(buf, buf_size, 1, input);
	if (ferror(input)) {
	    dbg(1, "fread error: %s", strerror(errno));
	} else if (feof(input)) {
	    dbg(2, "EOF in fread");
	} else if (raw_len <= 0) {
	    dbg(1, "no EOF or error, but fread returned: %d", raw_len);
	    raw_len = -1;	/* force error */
	} else {
	    dbg(3, "fread %d octets for record %lld",
		    buf_size, (u_int64_t)recnum);
	}

    /*
     * line based read
     */
    } else if (fgets(buf, BUFSIZ, input) == NULL) {
	/* report fgets error */
	if (ferror(input)) {
	    dbg(1, "fgets error: %s", strerror(errno));
	} else if (feof(input)) {
	    dbg(2, "EOF in fgets");
	}
	raw_len = -1;
    } else {
	/* obtain line stats */
	buf[BUFSIZ] = '\0';
	raw_len = strlen(buf);
	if (raw_len <= 0) {
	    dbg(1, "no EOF or error, but fgets returned %d octets", raw_len);
	    raw_len = -1;	/* force error */
	} else {
	    dbg(3, "fgets read %d octet line for record %lld",
		raw_len, (u_int64_t)recnum);
	}
    }

    /*
     * return result
     */
    return raw_len;
}


/*
 * dbg - print a debug message, if -v level is high enough
 */
static void
dbg(int level, char *fmt, ...)
{
    va_list ap;		/* argument pointer */

    /* start the var arg setup and fetch our first arg */
    va_start(ap, fmt);

    /* print the message */
    fprintf(stderr, "Debug[%d]: ", level);
    if (fmt == NULL) {
	fmt = "<<NULL>> format";
    }
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    fflush(stderr);

    /* clean up */
    va_end(ap);
    return;
}
