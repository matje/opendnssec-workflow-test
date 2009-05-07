# $Id$
#
# Copyright (c) 2008-2009 Nominet UK. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
# GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
# IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
# OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
# IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#

# status_messages.pl - Generate Status Messages
#
# Description:
# 		A message code file defines the status codes used by a program
# 		with a line starting:
#
# 			#define <prefix>_BASE	number
#
# 		followed by successive lines of:
#
# 			#define <prefix>_<string> (<perfix>_BASE + <offset>)	/* <message text> */
#
# 		This Perl script creates a file containing the message text that can
# 		be included into a module.  The include file contains the C-code that
# 		defines the message array.
#
# Invocation:
# 		perl status_messages.pl prefix infile output
#
# 		prefix:		Message prefix that defines symbols.
# 		infile:		Name of the .h input file
# 		outfile		Name of the .inc output file

use strict;

my %messages = ();		# Hold messages until we have got the maximum number
my $max_offset = 0;		# Maximum offset in the array
my $search = "";

# Run through input header file storing the message against the message offset.

open INFILE, "<", $ARGV[1] || die "Unable to open $ARGV[0] definitions file";
while (<INFILE>) {

	# The regular expression in the following statement matches a string of the
	# form:
	#
	# 	#define <perfix>_<string> (<prefix>_BASE + <offset>)	/* <message text> */

	if ($_ =~ /^#define\s+$ARGV[0]\_\w+\s+\($ARGV[0]\_BASE\s+\+\s+(\d+)\)\s+\/\*\s+(.+)\s+\*\/\s*$/) {

		# Match.  Store the offset and the message for now until we determine
		# how large the array should be.

		$messages{$1} = $2;							# Store message for later
		$max_offset = $1 if ($1 > $max_offset);		# Record the maximum offset
	}
}
close INFILE;

# Now write the C fragment that defines the messages.

open MESSAGE, ">$ARGV[2]" || die "Unable to open output message file";
my $array_size = $max_offset + 1;

print MESSAGE "/* This file is generated by a perl script - it should not be edited */\n";
print MESSAGE "\n";
print MESSAGE "#include \"$ARGV[1]\"\n";
print MESSAGE "\n";
print MESSAGE "#define $ARGV[0]_MIN_VALUE ($ARGV[0]_BASE)\n";
print MESSAGE "#define $ARGV[0]_MAX_VALUE ($ARGV[0]_BASE + $max_offset)\n";
print MESSAGE "#define $ARGV[0]_ARRAY_SIZE ($array_size)\n";
print MESSAGE "\n";
print MESSAGE "static const char* m_messages[] = {\n";

# Print out the message assignments.

my $i = 0;
for ($i = 0; $i < $array_size; ++$i) {
	if (defined($messages{$i})) {
		print MESSAGE "\t\"$messages{$i}\",\n";
	}
	else {
		print MESSAGE "\tNULL,\n";
	}
}

# ... and round off the declaration.

print MESSAGE "\n";
print MESSAGE "/* ... and null-terminate the array to be on the safe side */\n";
print MESSAGE "\n";
print MESSAGE "\tNULL\n";
print MESSAGE "};\n";

close MESSAGE;
