This is the source for PFTP, an FTP client for use with Atari systems
under plain TOS, using STiK or STinG networking.  The source code can
be used to generate two binaries: PFTP.PRG and PFTPSTIK.PRG.  The only
difference between the two is that PFTP.PRG uses some STinG extensions
and so can only be used with STinG.  PFTPSTIK.PRG should work with either
STiK or STinG.

This code was written for Lattice C 5.60, using the following options to
handle the STiK/STinG API:
	-w	default short integers
	-aw	type-based stack alignment

Note that all files except this one and LICENSE.TXT use cr/lf line
endings, since they are set up for compilation on an Atari system.
