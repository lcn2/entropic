/*
 * entropic - measure the amount of entropy found within input records
 *
 * @(#) $Revision: 1.5 $
 * @(#) $Id: entropic.c,v 1.5 2003/01/30 10:18:56 chongo Exp chongo $
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
#include <string.h>
#include <ctype.h>


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
static char *usage =
	"[-h] [-v verbose] [-b bit_depth] [-r rec_size] [-k] [-m map_file]\n"
	"    [-c] input_file\n"
	"\n"
	"\t-h			print this help message and exit\n"
	"\n"
	"\t-v verbose		verbose level (def: 0 ==> none)\n"
	"\t-b bit_depth		tally depth for each record bit (def: 12)\n"
	"\t-r rec_size		read rec_size octet records (def: line mode)\n"
	"\t-k			do not discard newlines (not with -r)\n"
	"\t-m map_file		octet mask, octet to bit map, bit mask\n"
	"\t-c			keep after 1st = before 1st ; (not with -r)\n"
	"\n"
	"\tinput_file\tfile to read records from (- ==> stdin)\n"
	"\n"
	"\tThe map_file syntax:\n"
	"\n"
	"\t# comments start with a # and go thru the end of the line\n"
	"\t# empty and blank lines are ignored\n"
	"\n"
	"\t# The charmask line contains only x's and c's after the =\n"
	"\t# The charmask is optional, default is process all chars\n"
	"\tcharmask=[xc]+	# comments at the end of a line are ignored\n"
	"\n"
	"\t# Map the octet value (given as 2 hex chars) into 0 or more bits.\n"
	"\t# If no octet value are given, the default 8 bit binary value of\n"
	"\t#	each of the 256 octet values are used to convert octets\n"
	"\t#	to binary strings.\n"
	"\t# If any octet value is given, then only those octet values give\n"
	"\t#	in this file are processed.\n"
	"\t# So:\n"
	"\t#	61=01001\n"
	"\t# maps the octet 0x61 ('a') into 5 bits: 0, 1, 0, 0, and 1.\n"
	"\t[0-9a-fA-F][0-9a-fA-F]=[01]*\n"
	"\n"
	"\t# The bitmask line contains only x's and b's after the =\n"
	"\t# The bitmask is optional, default is process all bits\n"
	"\tbitmask=[xb]+\n"
	"\n"
	"\tSelected ASCII values:\n"
	"\n"
	"\t\\t 09      \\n 0a      \\r 0d\n"
	"\n"
	"\tsp 20      0  30      @  40      P  50	    `  60      p  70\n"
	"\t!  21      1  31      A  41      Q  51      a  61      q  71\n"
	"\t\"  22      2  32      B  42      R  52      b  62      r  72\n"
	"\t#  23      3  33      C  43      S  53      c  63      s  73\n"
	"\t$  24      4  34      D  44      T  54      d  64      t  74\n"
	"\t%  25      5  35      E  45      U  55      e  65      u  75\n"
	"\t&  26      6  36      F  46      V  56      f  66      v  76\n"
	"\t'  27      7  37      G  47      W  57      g  67      w  77\n"
	"\t(  28      8  38      H  48      X  58      h  68      x  78\n"
	"\t)  29      9  39      I  49      Y  59      i  69      y  79\n"
	"\t*  2a      :  3a      J  4a      Z  5a      j  6a      z  7a\n"
	"\t+  2b      ;  3b      K  4b      [  5b      k  6b      {  7b\n"
	"\t,  2c      <  3c      L  4c      \\  5c      l  6c      |  7c\n"
	"\t-  2d      =  3d      M  4d      ]  5d      m  6d      }  7d\n"
	"\t.  2e      >  3e      N  4e      ^  5e      n  6e      ~  7e\n"
	"\t/  2f      ?  3f      O  4f      _  5f      o  6f\n";
static char *program;		/* our name */
static int v_flag = 0;		/* verbosity level */
static int bit_depth = DEF_DEPTH;  /* tally bit depth for each bit in record */
static int rec_size = 0;	/* > 0 ==> record size, 0 ==> line mode */
static int line_mode = 1;	/* 0 ==> read binary recs, 1 ==> read lines */
static char *map_file = NULL;	/* x ==> remove, v ==> keep, else remove */
static char *filename;		/* name of input file, or - ==> stdin */


/*
 * record pre-processing
 *
 * We will document the pre-processing performed on a record in order:
 *
 * keep_newline	(-k)
 *
 * 	If line_mode == 0: (-r rec_size)
 * 	     do nothing
 *
 * 	If line_mode == 1: (without -r)
 * 	     keep_newline == 0   ==>   discard trailing \n, \r, \r\n, or \n\r
 * 	     keep_newline == 1   ==>   do nothing
 *
 * cookie_trim (-c)
 *
 * 	If line_mode == 0: (-r rec_size)
 * 	     do cookie_trim
 *
 * 	If line_mode == 1: (without -r)
 * 	     cookie_trim == 0   ==>   do nothing
 * 	     cookie_trim == 1   ==>   keep text after 1st = and before 1st ;
 *
 * char_mask (from -m map_file)
 *
 * 	A string of "x"'s and "c"'s that indicate which chars in
 * 	an input record will be processed.  An "x" means that a
 * 	character in the input record is ignored.  A "c" means
 * 	that the character will be processed.
 *
 * 	NULL ==> process all characters (the default)
 *
 * octet_map[i] (from -m map_file)
 *
 * 	A string of ASCII "0"'s and "1"'s representing the bit pattern
 * 	that a the octet 'i' should be converted into during the
 * 	processing of a record.  An empty string means that the
 * 	given octet pattern is skipped.
 *
 * 	The default octet_map is the 8 bit value of the octet.
 *
 * bit_mask (from -m map_file)
 *
 * 	A string of "x"'s and "b"'s that indicate which bits will
 * 	be processed.  An "x" means that a bit will be ignored.
 * 	A "b" means that the bit will be processed.
 *
 * 	NULL ==> process all bits (the default)
 */
static int keep_newline = 0;	/* 0 ==> discard newline, 1 ==> keep them */
static int cookie_trim = 0;	/* 1 ==> keep after 1st = and before 1st ; */
static char *char_mask = NULL;
static char *octet_map[1 << OCTET_BITS] = {
    "00000000", "00000001", "00000010", "00000011",
    "00000100", "00000101", "00000110", "00000111",
    "00001000", "00001001", "00001010", "00001011",
    "00001100", "00001101", "00001110", "00001111",
    "00010000", "00010001", "00010010", "00010011",
    "00010100", "00010101", "00010110", "00010111",
    "00011000", "00011001", "00011010", "00011011",
    "00011100", "00011101", "00011110", "00011111",
    "00100000", "00100001", "00100010", "00100011",
    "00100100", "00100101", "00100110", "00100111",
    "00101000", "00101001", "00101010", "00101011",
    "00101100", "00101101", "00101110", "00101111",
    "00110000", "00110001", "00110010", "00110011",
    "00110100", "00110101", "00110110", "00110111",
    "00111000", "00111001", "00111010", "00111011",
    "00111100", "00111101", "00111110", "00111111",
    "01000000", "01000001", "01000010", "01000011",
    "01000100", "01000101", "01000110", "01000111",
    "01001000", "01001001", "01001010", "01001011",
    "01001100", "01001101", "01001110", "01001111",
    "01010000", "01010001", "01010010", "01010011",
    "01010100", "01010101", "01010110", "01010111",
    "01011000", "01011001", "01011010", "01011011",
    "01011100", "01011101", "01011110", "01011111",
    "01100000", "01100001", "01100010", "01100011",
    "01100100", "01100101", "01100110", "01100111",
    "01101000", "01101001", "01101010", "01101011",
    "01101100", "01101101", "01101110", "01101111",
    "01110000", "01110001", "01110010", "01110011",
    "01110100", "01110101", "01110110", "01110111",
    "01111000", "01111001", "01111010", "01111011",
    "01111100", "01111101", "01111110", "01111111",
    "10000000", "10000001", "10000010", "10000011",
    "10000100", "10000101", "10000110", "10000111",
    "10001000", "10001001", "10001010", "10001011",
    "10001100", "10001101", "10001110", "10001111",
    "10010000", "10010001", "10010010", "10010011",
    "10010100", "10010101", "10010110", "10010111",
    "10011000", "10011001", "10011010", "10011011",
    "10011100", "10011101", "10011110", "10011111",
    "10100000", "10100001", "10100010", "10100011",
    "10100100", "10100101", "10100110", "10100111",
    "10101000", "10101001", "10101010", "10101011",
    "10101100", "10101101", "10101110", "10101111",
    "10110000", "10110001", "10110010", "10110011",
    "10110100", "10110101", "10110110", "10110111",
    "10111000", "10111001", "10111010", "10111011",
    "10111100", "10111101", "10111110", "10111111",
    "11000000", "11000001", "11000010", "11000011",
    "11000100", "11000101", "11000110", "11000111",
    "11001000", "11001001", "11001010", "11001011",
    "11001100", "11001101", "11001110", "11001111",
    "11010000", "11010001", "11010010", "11010011",
    "11010100", "11010101", "11010110", "11010111",
    "11011000", "11011001", "11011010", "11011011",
    "11011100", "11011101", "11011110", "11011111",
    "11100000", "11100001", "11100010", "11100011",
    "11100100", "11100101", "11100110", "11100111",
    "11101000", "11101001", "11101010", "11101011",
    "11101100", "11101101", "11101110", "11101111",
    "11110000", "11110001", "11110010", "11110011",
    "11110100", "11110101", "11110110", "11110111",
    "11111000", "11111001", "11111010", "11111011",
    "11111100", "11111101", "11111110", "11111111"
};
static char *bit_mask = NULL;


/*
 * forward declarations
 */
static void parse_args(int argc, char **argv);
static void load_map_file(char *map_file);
static tally_t *alloc_bittally(int depth);
static struct bitslice *alloc_bitslice(int bitnum, int depth);
static void record_bit(struct bitslice *slice, int value);
static void dbg(int level, char *fmt, ...);
static int read_record(FILE *input, u_int8_t *buf, int buf_size,
		       int read_line);
static int pre_process(u_int8_t *inbuf, int inbuf_len, u_int8_t **outbuf,
		       int *outbuf_len);


/*
 * misc globals and static values
 */
static tally_t recnum = 0;	/* current record number, starting with 0 */
extern int errno;		/* last system error */
static int hex_to_value[1 << OCTET_BITS] = {
    /* 00 */	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    /* 08 */	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    /* 10 */	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    /* 18 */	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    /* 20 */	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    /* 28 */	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    /* 30 */	0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
    /* 38 */    0x8, 0x9, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    /* 40 */    0x0, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf, 0x0,
    /* 48 */    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    /* 50 */    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    /* 58 */    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    /* 60 */    0x0, 0xA, 0xB, 0xC, 0xD, 0xE, 0xF, 0x0,
    /* 68 */    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    /* 70 */    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    /* 78 */    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    /* 80 */    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    /* 88 */    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    /* 90 */    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    /* 98 */    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    /* a0 */    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    /* a8 */    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    /* b0 */    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    /* b8 */    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    /* c0 */    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    /* c8 */    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    /* d0 */    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    /* d8 */    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    /* e0 */    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    /* e8 */    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    /* f0 */    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    /* f8 */    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
};


/*
 * main
 */
int
main(int argc, char *argv[])
{
    extern char *optarg;	/* argument to current option */
    extern int optind;		/* first argv-element that is not an option */
    FILE *input;		/* stream from which to read records */
    u_int8_t *raw_buf;		/* malloced raw record input buffer */
    int raw_len;		/* length of raw record in octets */
    u_int8_t *bit_buf;		/* malloced bit buffer of 0x00 or 0x01 octets */
    int bit_len;		/* length bit_buf */
    int bit_buf_used;		/* number of octets in bit_buf being used */

    /*
     * parse args
     */
    program = argv[0];
    parse_args(argc, argv);

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
	exit(1);
    }

    /*
     * allocate raw input buffer and bit buffer with extra room in each
     */
    raw_buf = (u_int8_t *)malloc(rec_size+1);
    if (raw_buf == NULL) {
	fprintf(stderr, "%s: failed to allocate raw buffer: %d octets\n",
		program, rec_size+1);
	exit(2);
    }
    bit_len = rec_size * OCTET_BITS;
    bit_buf = (u_int8_t *)malloc((rec_size+1) * OCTET_BITS + 1);
    if (bit_buf == NULL) {
	fprintf(stderr, "%s: failed to allocate bit buffer: %d octets\n",
		program, (rec_size+1) * OCTET_BITS + 1);
	exit(3);
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
	if (raw_len <= 0) {
	    break;
	}

	/*
	 * pre-process raw record and produce a bit buffer
	 */
	bit_buf_used = pre_process(raw_buf, raw_len, &bit_buf, &bit_len);
	if (bit_buf_used <= 0) {
	    /* EOF or error */
	    dbg(3, "main: skipping record, bit_buf_used returned: %d <= 0",
		    bit_buf_used);
	    continue;
	}
	dbg(3, "main: bit buffer has %d bits", bit_buf_used);

    } while (++recnum > 0);

    /*
     * final entropy processing
     *
     * XXX - write this
     */
    dbg(1, "final entropy processing");

    /*
     * all done!  -- Jessica Noll, Age 2
     */
    dbg(1, "all done!");
    exit(0);
}


/*
 * parse_args - parse and check command line arguments
 */
static void
parse_args(int argc, char **argv)
{
    int i;

    /*
     * process command line options
     *
     * See the usage static string for details on command line options
     */
    while ((i = getopt(argc, argv, "hv:b:r:km:c")) != -1) {
	switch (i) {
	case 'h':	/* print usage message and then exit */
	    fprintf(stderr, "usage: %s %s\n", program, usage);
	    exit(0);
	    /*NOTREACHED*/
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
	case 'k':	/* keep newlines */
	    keep_newline = 1;
	    break;
	case 'm':	/* map filename */
	    map_file = optarg;
	    break;
	case 'c':	/* cookie trim */
	    cookie_trim = 1;
	    break;
	default:
	    fprintf(stderr, "usage: %s %s\n", program, usage);
	    exit(4);
	}
    }

    /*
     * note the input filename
     */
    if (optind >= argc) {
	fprintf(stderr, "usage: %s %s\n", program, usage);
	exit(5);
    }
    filename = argv[optind];
    dbg(1, "main: input file: %s", filename);

    /*
     * check bit depth
     */
    if (bit_depth < 1) {
	fprintf(stderr, "%s: -b bit_depth must be > 0\n", program);
	exit(6);
    }
    if (bit_depth > MAX_DEPTH) {
	fprintf(stderr, "%s: -b bit_depth must <= %d\n", program, MAX_DEPTH);
	exit(7);
    }
    dbg(1, "main: bit_depth: %d", bit_depth);

    /*
     * check raw record size, if given
     */
    if (line_mode == 0 && rec_size <= 0) {
	fprintf(stderr, "%s: -r rec_size: %d must be > 0\n",
		program, rec_size);
	exit(8);
    } else if (line_mode == 0) {
	dbg(1, "main: binary record size: %d", rec_size);
    } else {
	rec_size = BUFSIZ;
	dbg(1, "main: line mode of up to %d octets", rec_size);
    }

    /*
     * -k implies line mode, but -r rec_size implies raw mode
     */
    if (line_mode == 0 && keep_newline) {
	fprintf(stderr, "%s: -r rec_size and -k conflict\n", program);
	exit(9);
    }

    /*
     * -c implies line mode, but -r rec_size implies raw mode
     */
    if (line_mode == 0 && cookie_trim) {
	fprintf(stderr, "%s: -r rec_size and -c conflict\n", program);
	exit(10);
    }

    /*
     * map_file processing
     */
    if (map_file != NULL) {
	(void) load_map_file(map_file);
    }
    return;
}


/*
 * load_map_file - parse and load a map file (-m map_file)
 *
 * See the usage string above for the map_file syntax.
 *
 * given:
 * 	map_file	map filename
 *
 * This function will modify:
 *
 * 	keep_newline
 * 	cookie_trim
 * 	char_mask
 * 	octet_map
 * 	bit_mask
 *
 * This function does not return on error.
 */
static void
load_map_file(char *map_file)
{
    FILE *map;			/* map file stream */
    char buf[BUFSIZ+1];		/* input buffer */
    int len;			/* length of input line */
    int linenum;		/* map file line number */
    int seen_octet_map;		/* 1 ==> already saw a octet map directive */
    int octet;			/* octet being mapped */
    char *p;
    int i;

    /*
     * firewall
     */
    if (map_file == 0) {
	fprintf(stderr, "%s: map_file arg is NULL\n", program);
	exit(11);
    }
    buf[BUFSIZ] = '\0';

    /*
     * open the map file
     */
    dbg(1, "load_map_file: opening map file: %s", map_file);
    map = fopen(map_file, "r");
    if (map == NULL) {
	fprintf(stderr, "%s: failed to open map file: %s\n",
		program, map_file);
	exit(12);
    }

    /*
     * read lines until EOF
     */
    seen_octet_map = 0;
    linenum = 0;
    while (fgets(buf, BUFSIZ, map) != NULL) {

	/*
	 * remove # comments
	 */
	++linenum;
	p = strchr(buf, '#');
	if (p != NULL) {
	    *p = '\0';
	}

	/*
	 * remove trailing newline and whitespace
	 */
	len = strlen(buf);
	while (len > 0 && isspace(buf[len-1])) {
	    buf[len-1] = '\0';
	    --len;
	}

	/*
	 * ignore blank / empty lines
	 */
	if (len <= 0) {
	    continue;
	}
	dbg(8, "load_map_file: line %d: %s", linenum, buf);

	/*
	 * case: charmask line
	 */
	if (strncmp(buf, "charmask=", sizeof("charmask=")-1) == 0) {

	    /*
	     * must have only one or more x's and c's
	     */
	    p = buf + sizeof("charmask=")-1;
	    if (strspn(p, "xc") != len-(sizeof("charmask=")-1)) {
		fprintf(stderr, "%s: map file: %s line %d charmask "
				"may only have x's and c's\n",
			program, map_file, linenum);
		exit(13);

	    }

	    /*
	     * save charmask
	     */
	    if (char_mask != NULL) {
		free(char_mask);
	    }
	    char_mask = strdup(p);
	    if (char_mask == NULL) {
		fprintf(stderr, "%s: failed to malloc charmask\n", program);
		exit(14);
	    }

	/*
	 * case: bitmask line
	 */
	} else if (strncmp(buf, "bitmask=", sizeof("bitmask=")-1) == 0) {

	    /*
	     * must have only one or more x's and b's
	     */
	    p = buf + sizeof("bitmask=")-1;
	    dbg(4, "bitmask: %s", p);
	    if (strspn(p, "xb") != len-(sizeof("bitmask=")-1)) {
		fprintf(stderr, "%s: map file: %s line %d bitmask "
				"may only have x's and b's\n",
			program, map_file, linenum);
		exit(15);

	    }

	    /*
	     * save bitmask
	     */
	    if (bit_mask != NULL) {
		free(bit_mask);
	    }
	    bit_mask = strdup(p);
	    if (bit_mask == NULL) {
		fprintf(stderr, "%s: failed to malloc bitmask\n", program);
		exit(16);
	    }

	/*
	 * octet map line
	 */
	} else if (isxdigit(buf[0]) && isxdigit(buf[1]) && buf[2] == '=') {

	    /*
	     * clear old octet map if we found our first octet map directive
	     */
	    if (seen_octet_map == 0) {
		for (i=0; i < 1 << OCTET_BITS; ++i) {
		    octet_map[i] = "";
		}
		seen_octet_map = 1;
	    }

	    /*
	     * determine which octet is being mapped
	     */
	    octet = (hex_to_value[buf[0]]<<4) + hex_to_value[buf[1]];

	    /*
	     * add to octet map
	     */
	    octet_map[octet] = strdup(buf+3);
	    if (octet_map[octet] == NULL) {
		fprintf(stderr, "%s: failed to malloc octet map\n", program);
		exit(17);
	    }

	/*
	 * unknown line
	 */
	} else {
	    fprintf(stderr, "%s: map file: %s line %d unknown directive\n",
		    program, map_file, linenum);
	    exit(18);
	}
    }
    dbg(4, "load_map_file: processed %d lines from map file: %s",
	     linenum, map_file);
    return;
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
	exit(19);
    }
    values = 1<<(depth+1);
    if (values < 0 || values == 0) {
	fprintf(stderr, "%s: alloc_bittally: depth: %d is too large\n",
		program, depth);
	exit(20);
    }

    /*
     * allocate and zero
     */
    ret = (tally_t *)calloc(values, sizeof(long));
    if (ret == NULL) {
	fprintf(stderr, "%s: alloc_bittally: "
			"not enough memory for depth: %d\n",
		program, depth);
	exit(21);
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
	exit(22);
    }

    /*
     * allocate the bitslice
     */
    ret = (struct bitslice *)malloc(sizeof(struct bitslice));
    if (ret == NULL) {
	fprintf(stderr, "%s: cannot allocate struct bitslice\n", program);
	exit(23);
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
	exit(24);
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
 *
 * NOTE: We will ensure that the octet beyond the end of the buffer is '\0'.
 */
static int
read_record(FILE *input, u_int8_t *buf, int buf_size, int read_line)
{
    int rec_len;		/* length of raw record in octets */

    /*
     * setup to read
     */
    clearerr(input);
    errno = 0;

    /*
     * raw read
     */
    if (read_line == 0) {
	rec_len = fread(buf, buf_size, 1, input);
	if (ferror(input)) {
	    dbg(1, "fread error: %s", strerror(errno));
	} else if (feof(input)) {
	    dbg(1, "EOF in fread");
	} else if (rec_len <= 0) {
	    dbg(1, "no EOF or error, but fread returned: %d", rec_len);
	    rec_len = -1;	/* force error */
	} else {
	    dbg(4, "fread %d octets for record %lld",
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
	    dbg(1, "EOF in fgets");
	}
	rec_len = -1;
    } else {
	/* obtain line stats */
	buf[BUFSIZ] = '\0';
	rec_len = strlen(buf);
	if (rec_len <= 0) {
	    dbg(1, "no EOF or error, but fgets returned %d octets", rec_len);
	    rec_len = -1;	/* force error */
	} else {
	    dbg(4, "fgets read %d octet line for record %lld",
		rec_len, (u_int64_t)recnum);
	}
    }

    /*
     * force trailing '\0'
     */
    if (rec_len > 0) {
	buf[rec_len] = '\0';
    } else {
	buf[0] = '\0';
    }

    /*
     * return result
     */
    return rec_len;
}


/*
 * pre_process - convert an input record into bit values to be processed
 *
 * This function will pre-process a raw character based record and produce
 * a bit buffer of bits entropy process.  This function is given records
 * that have just been read and produces a set of 0 and 1 bits that will
 * go into the entropy measurements for a given bit position.
 *
 * The input buffer of a collection of octets starting at "inbuf" and
 * going for inbuf_len octets.  The input buffer may not be a string.
 * The input buffer may not be NUL terminated.
 *
 * The output buffer is a string of octets of either 0x00 or 0x01 value.
 * This function is given a pointer to the output buffer pointer.
 * This function is given a pointer malloced length of outbuf.
 * The output buffer must be a malloced buffer because, if needed,
 * this function will realloc it to a larger size.
 *
 * given:
 * 	inbuf		the raw record buffer
 * 	inbuf_len	length of inbuf in octets
 *	outbuf		pointer to a malloced output bit buffer
 * 	outbuf_len	pointer to the malloced length of outbuf
 *
 * returns:
 * 	the amount of outbuf used
 *
 * NOTE: The inbuf will be altered according to
 */
static int
pre_process(u_int8_t *inbuf, int inbuf_len, u_int8_t **outbuf, int *outbuf_len)
{
    int orig_inbuf_len = inbuf_len;	/* original inbuf_len value */
    int outbuf_need;	/* amount of outbuf we will use */
    int i;
    char *p;
    u_int8_t *q;
    u_int8_t *r;
    char *s;

    /*
     * firewall
     */
    if (inbuf == NULL) {
	fprintf(stderr, "%s: trim_record: inbuf is NULL\n", program);
	exit(25);
    }
    if (inbuf_len < 0) {
	fprintf(stderr, "%s: trim_record: buf_len <= 0: %d\n",
		program, inbuf_len);
	exit(26);
    }
    if (outbuf == NULL) {
	fprintf(stderr, "%s: trim_record: outbuf is NULL\n", program);
	exit(27);
    }
    if (outbuf_len == NULL) {
	fprintf(stderr, "%s: trim_record: outbuf_len is NULL\n", program);
	exit(28);
    }
    if (*outbuf == NULL) {
	fprintf(stderr, "%s: trim_record: outbuf points to a NULL\n", program);
	exit(29);
    }
    if (*outbuf_len <= 0) {
	fprintf(stderr, "%s: trim_record: outbuf_len points to %d <= 0\n",
		program, *outbuf_len);
	exit(30);
    }

    /*
     * do nothing if input buffer is empty
     */
    dbg(10, "inital inbuf pre newline trim: ((%s))", inbuf);
    dbg(9, "pre inbuf len: %d", orig_inbuf_len);
    if (inbuf_len <= 0) {
	dbg(5, "trim_record: empty inbuf");
	return 0;
    }

    /*
     * trim newline, if requested
     *
     * We trim a trailing \n or a trailing \r\n or a trailing \n\r
     */
    if (keep_newline == 0) {
	if (inbuf[inbuf_len-1] == '\n') {
	    inbuf[inbuf_len-1] = '\0';
	    --inbuf_len;
	    if (inbuf_len > 0 && inbuf[inbuf_len-1] == '\r') {
		inbuf[inbuf_len-1] = '\0';
		--inbuf_len;
	    }
	} else if (inbuf[inbuf_len-1] == '\r') {
	    inbuf[inbuf_len-1] = '\0';
	    --inbuf_len;
	    if (inbuf_len > 0 && inbuf[inbuf_len-1] == '\n') {
		inbuf[inbuf_len-1] = '\0';
		--inbuf_len;
	    }
	}
    }
    dbg(8, "inbuf len: %d", inbuf_len);
    dbg(8, "1st inbuf: %s", inbuf);
    if (inbuf_len <= 0) {
	/* trimed the line down to nothing */
	return 0;
    }

    /*
     * cookie trim, if requested
     *
     * Programs such as cookie_monister will output lines of the form:
     *
     *    [optional_simestamp:] Set-cookie: COOKIE_NAME=VALUE; stuff ...
     *
     * This trim will reduce the above line down to just:
     *
     *    VALUE
     *
     * NOTE: If the line does not have a = and a ;, then the entire line
     * 	     is discarded.
     */
    if (cookie_trim) {
	char *equal;	/* first = or NULL */
	char *semi;	/* first ; or NULL */

	/*
	 * look for the cookie value boundaries
	 */
	equal = strchr(inbuf, '=');
	if (equal == NULL) {
	    dbg(5, "trim_record: line has no =, discarding line");
	    return 0;
	}
	semi = strchr(equal+1, ';');
	if (semi == NULL) {
	    dbg(5, "trim_record: no ; after 1st =, discarding line");
	    return 0;
	}

	/*
	 * copy value to front of buffer
	 */
	inbuf_len = semi-equal;
	memmove(inbuf, equal+1, inbuf_len);
	inbuf[inbuf_len] = '\0';
	dbg(9, "inbuf after cookie trim: %s", inbuf);
    }

    /*
     * character mask, if requested
     *
     * If charmask is a string, then we keep only those characters
     * in the input buffer that correspond to a 'c' in the charmask.
     */
    if (char_mask != NULL) {

	/*
	 * walk the charmask looking for c's
	 */
	if ((i = strlen(char_mask)) > inbuf_len) {
	    s = char_mask + inbuf_len;
	} else {
	    s = char_mask + i;
	}
	for (q=inbuf, p=char_mask; *p != '\0' && p < s; ++p) {

	    /* skip non-c chars (presumably x's) */
	    if (*p != 'c') {
		continue;
	    }

	    /* save this inbuf character */
	    *q++ = inbuf[p - char_mask];
	}
	inbuf_len = q - inbuf;
	inbuf[inbuf_len] = '\0';	/* for debugging */
	dbg(9, "char_mask: %s", char_mask);
	dbg(9, "inbuf after char_mask: %s", inbuf);
	dbg(7, "inbuf trimmed to %d octets", inbuf_len);
    }

    /*
     * do nothing if trimmed input buffer is empty
     */
    if (inbuf_len <= 0) {
	dbg(5, "trim_record: trimmed inbuf is empty");
	return 0;
    }

    /*
     * determine how many bits we will produce
     */
    outbuf_need = 0;
    for (i=0; i < inbuf_len; ++i) {
	outbuf_need += strlen(octet_map[inbuf[i]]);
    }

    /*
     * do nothing if we will produce no bits
     */
    if (outbuf_need <= 0) {
	dbg(5, "trim_record: line will yield no bits");
	return 0;
    }

    /*
     * be sure we have enough room in our output buffer
     */
    if (*outbuf_len < outbuf_need) {

	/* grow output buffer */
	*outbuf = (u_int8_t *)realloc(outbuf, outbuf_need+1);
	if (outbuf == NULL) {
	    fprintf(stderr, "%s: trim_record: failed to realloc outbuf from "
		    	    "%d octets to %d octets\n",
		    program, *outbuf_len, outbuf_need);
	    exit(31);
	}
	dbg(8, "outbuf grew from %d octets to %d octets",
	       *outbuf_len, outbuf_need);
	*outbuf_len = outbuf_need;
	*outbuf[outbuf_need] = '\0';
    }

    /*
     * load output buffer with 0x00's and 0x01's
     */
    r = *outbuf;
    for (i=0; i < inbuf_len; ++i) {
	/*
	 * load 0x01's for every '1' and 0x00's otherwise
	 */
	for (q = octet_map[inbuf[i]]; *q != '\0'; ++q) {
	    *r++ = ((*q == '1') ? 0x01 : 0x00);
	}
    }
    *r = '\0';

    /*
     * special binary debugging output
     */
    if (v_flag >= 7) {
	r = *outbuf;
	dbg(7, "initialy have %d bits", outbuf_need);
	fprintf(stderr, "Debug[7]: encoding: ");
	for (i=0; i < outbuf_need; ++i) {
	    if (r[i]) {
		fputc('1', stderr);
	    } else {
		fputc('0', stderr);
	    }
	}
	fputc('\n', stderr);
    }

    /*
     * bit mask, if requested
     *
     * If butmask is a string, then we keep only those bits
     * in the output buffer that correspond to a 'b' in the charmask.
     */
    if (bit_mask != NULL) {

	/*
	 * walk the charmask looking for b's
	 */
	r = *outbuf;
	if ((i = strlen(bit_mask)) > outbuf_need) {
	    s = bit_mask + outbuf_need;
	} else {
	    s = bit_mask + i;
	}
	for (q=r, p=bit_mask; *p != '\0' && p < s; ++p) {

	    /* skip non-b chars (presumably x's) */
	    if (*p != 'b') {
		continue;
	    }

	    /* save this inbuf character */
	    *q++ = r[p - bit_mask];
	}
	dbg(9, "bit_mask: %s", bit_mask);
	dbg(8, "masked %d bits down to %d bits", outbuf_need, q - r);
	outbuf_need = q - r;
	r[outbuf_need] = '\0';	/* for debugging */

	/*
	 * special binary debugging output
	 */
	if (v_flag >= 7) {
	    r = *outbuf;
	    fprintf(stderr, "Debug[7]: the bits: ");
	    for (i=0; i < outbuf_need; ++i) {
		if (r[i]) {
		    fputc('1', stderr);
		} else {
		    fputc('0', stderr);
		}
	    }
	    fputc('\n', stderr);
	    dbg(7, "masked down to %d octets", outbuf_need);
	}
    }

    /*
     * return use count
     */
    return outbuf_need;
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

    /* if high enough debug, print a message */
    if (level <= v_flag) {

	/* print the message */
	fprintf(stderr, "Debug[%d]: ", level);
	if (fmt == NULL) {
	    fmt = "<<NULL>> format";
	}
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	fflush(stderr);
    }

    /* clean up */
    va_end(ap);
    return;
}
