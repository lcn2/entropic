# entropic

measure the amount of entropy found within input records


# To install

```sh
make clobber all
sudo make install clobber
```


# Examples


## entropic

```sh
$ /usr/local/bin/entropic -m /usr/local/share/entropic/7bit.map Makefile

Entropy report:
record count: 121 with 224 bits: high entropy: 219.721914
record count: 121 with 224 bits: low entropy: 151.858990
high, median and low entropy: 219.721914 185.790452 151.858990
```


## ent_binary

```sh
$ /usr/local/bin/ent_binary /usr/local/bin/ent_binary

Entropy report:
record count: 52 with 8192 bits: high entropy: 5976.578986
record count: 52 with 8192 bits: low entropy: 1965.533835
high, median and low entropy: 5976.578986 3971.056411 1965.533835
```


# To use


## entropic

```
/usr/local/bin/entropic [-h] [-v verbose] [-c rept_cycle] [-b bit_depth]
	[-B back_history] [-f depth_factor] [-r rec_size] [-k]
	[-m map_file] [-C] input_file

	-h			print this help message and exit
	-v verbose		verbose level (def: 0 ==> none)
	-V			print version string and exit

	-c rept_cycle		report each rept_cycle records (def: at end)
	-b bit_depth		tally depth for each record bit (def: 8)
	-B back_history		xor diffs this many records back (def: 32)
	-f depth_factor		ave slot tally needed for entropy (def: 4)
	-r rec_size		read rec_size octet records (def: line mode)
	-k			do not discard newlines (not with -r)
	-m map_file		octet mask, octet to bit map, bit mask
	-C			keep after 1st = before 1st ; (not with -r)

	input_file		file to read records from (- ==> stdin)

	The map_file syntax:

	# comments start with a # and go thru the end of the line
	# empty and blank lines are ignored

	# The charmask line contains only x's and c's after the =
	# The charmask is optional, default is process all chars
	charmask=[xc]+		# comments at the end of a line are ignored

	# Map the octet value (given as 2 hex chars) into 0 or more bits.
	# If no octet value are given, the default 8 bit binary value of
	#	each of the 256 octet values are used to convert octets
	#	to binary strings.
	# If any octet value is given, then only those octet values give
	#	in this file are processed.
	# So:
	#	61=01001
	# maps the octet 0x61 ('a') into 5 bits: 0, 1, 0, 0, and 1.
	[0-9a-fA-F][0-9a-fA-F]=[01]*

	# The bitmask line contains only x's and b's after the =
	# The bitmask is optional, default is process all bits
	bitmask=[xb]+

	Selected ASCII values:

	\t 09      \n 0a      \r 0d

	sp 20      0  30      @  40      P  50	    `  60      p  70
	!  21      1  31      A  41      Q  51      a  61      q  71
	"  22      2  32      B  42      R  52      b  62      r  72
	#  23      3  33      C  43      S  53      c  63      s  73
	$  24      4  34      D  44      T  54      d  64      t  74
	%  25      5  35      E  45      U  55      e  65      u  75
	&  26      6  36      F  46      V  56      f  66      v  76
	'  27      7  37      G  47      W  57      g  67      w  77
	(  28      8  38      H  48      X  58      h  68      x  78
	)  29      9  39      I  49      Y  59      i  69      y  79
	*  2a      :  3a      J  4a      Z  5a      j  6a      z  7a
	+  2b      ;  3b      K  4b      [  5b      k  6b      {  7b
	,  2c      <  3c      L  4c      \  5c      l  6c      |  7c
	-  2d      =  3d      M  4d      ]  5d      m  6d      }  7d
	.  2e      >  3e      N  4e      ^  5e      n  6e      ~  7e
	/  2f      ?  3f      O  4f      _  5f      o  6f

entropic version: 1.17.1 2025-05-05
```


## ent_binary

```
/usr/local/bin/ent_binary [-h] [-v verbose] [-V] [-c rept_cycle] [-b bit_depth]
	[-B back_history] [-f depth_factor] [-r rec_size]
	input_file

	-h			print this help message and exit
	-v verbose		verbose level (def: 0 ==> none)
	-V			print version string and exit

	-c rept_cycle		report each rept_cycle records (def: at end)
	-b bit_depth		tally depth for each record bit (def: 8)
	-B back_history		xor diffs this many records back (def: 32)
	-f depth_factor		ave slot tally needed for entropy (def: 4)
	-r rec_size		read rec_size octet records (def: BUFSIZ (8192))

	input_file		file to read records from (- ==> stdin)

ent_binary version: 1.17.1 2025-05-05
```


# Reporting Security Issues

To report a security issue, please visit "[Reporting Security Issues](https://github.com/lcn2/entropic/security/policy)".
