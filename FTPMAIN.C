/*
 * pftp: a simple ftp client
 *
 * Copyright (c) 2013, 2018 Roger Burrows
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See LICENSE.TXT for details.
 */

/*
 * This is a minimalist ftp client, with the following features:
 *		builtin commands
 *		commandline history & editing
 *
 * The command handling code is based on EmuCON2, also written
 * by Roger Burrows.
 *
 * For maximum compatibility, only FTP commands that are included
 * in RFC959 are used.
 *
 * The commands and args are patterned after those in the Linux
 * ftp kit, with some minor changes (e.g. default transfer mode
 * is image).
 *
 * Note: written for Lattice C 5.60, using the following options
 * to handle the STinG API:
 *	-w	default short integers
 *	-aw	type-based stack alignment
 * Unfortunately the API was designed based on PureC parameter
 * passing, which is not ANSI C compliant, and requires -aw
 * to handle just one argument: the last one to CNgets().
 *
 * TODO:
 *	1. ADD TIMEOUTS!
 *	   especially in send_command() for when e.g. remote disconnects
 *	2. Add auto login
 *	3. Allow specification of TCPBUFSIZE via -b option
 *	   note that the default will differ between STinG & STiK because of GlueSTiK
 */
#include "ftp.h"

#ifdef STIK1_COMPATIBLE
#define PROGRAM_NAME	"pftpstik"
#else
#define PROGRAM_NAME	"pftp"
#endif
#define VERSION			"1.0"


/*
 *	global variables
 */
ULONG idt_value;
UWORD screen_cols, screen_rows;
UWORD linesize;
DTA dta;
int start_drive;
char start_path[MAXPATHLEN];

/* options set via args only */
int auto_login = TRUE;				//not used (yet?)

/* options set via args or command */
int passive = TRUE;
int prompting = TRUE;
int globbing = TRUE;
int verbose = TRUE;
int debug = 0;

/* options set via command only */
int bell = FALSE;
int tick = FALSE;

char *server = NULL;
int port = FTP_CONTROL_PORT;
IPADDR ip;							/* current ip address */

/*
 * local to this module
 */
MLOCAL char input_line[MAX_LINE_SIZE];
MLOCAL int myargc;
MLOCAL char *myargv[MAX_ARGS];

/*
 *	function prototypes
 */
PRIVATE WORD execute(WORD argc,char **argv);
PRIVATE void strip_quotes(int argc,char **argv);

int main(int argc,char **argv)
{
int c, rc;
ULONG n;

	cprintf("%s v%s: type HELP for builtin commands\r\n",PROGRAM_NAME,VERSION);

	/*
	 * see if STiK/STinG is present
	 */
	if ((rc=sting_init()) < 0) {
		cprintf("STiK/STinG not present (rc=%d)\r\n",rc);
		cgetc();
		return 1;
	}

	/*
	 *	initialise some global variables
	 */
    if (getcookie(_IDT_COOKIE,&idt_value) == 0)
        idt_value = DEFAULT_DT_FORMAT;      /* if not found, make sure it's initialised properly */

	n = getwh();							/* get max cell number for x and y */
	screen_cols = (UWORD)(n >> 16) + 1;
	screen_rows = (UWORD)(n & 0xffff) + 1;
	linesize = screen_cols + 1 - 3; 		/* allow for trailing NUL and prompt */

	Fsetdta(&dta);

	start_drive = Dgetdrv() + 'A';
	Dgetpath(start_path,0);

	srand48(clock());						/* for port generation in ftpsting.c */

	if (init_cmdedit() < 0)
		cputs("warning: no history buffers\r\n");

	/*
	 *	decode args
	 */
	while((c=getopt(argc,argv,"pinegvd")) >= 0) {
		switch(c) {
		case 'p':
			passive = FALSE;
			break;
		case 'i':
			prompting = FALSE;
			break;
		case 'n':
			auto_login = FALSE;
			break;
		case 'g':
			globbing = FALSE;
			break;
		case 'v':
			verbose = TRUE;
			break;
		case 'd':	/* allow multiple 'd's to set higher debugging levels */
			debug++;
			break;
		default:
			cgetc();
			return 1;
		}
	}

	if (optind < argc)
		server = argv[optind++];
	if (optind < argc)
		port = atoi(argv[optind]);

	if (server)
		message(ftp_connect(server,port));

	while(1) {
		rc = read_line(input_line);
		save_history(input_line);
		if (rc < 0) 		/* user cancelled line */
			continue;
		myargc = parse_line(input_line,myargv);
		if (myargc < 0)		/* parse error */
			continue;
		if (execute(myargc,myargv) < 0)
			break;
	}

	ftp_disconnect();

	return 0;
}

PRIVATE WORD execute(WORD argc,char **argv)
{
LONG (*func)(WORD argc,char **argv);
LONG rc = 0L;

	if (argc == 0)
		return 0;

	func = lookup_builtin(argc,argv);

	if (func) {
		strip_quotes(argc,argv);
		rc = func(argc,argv);
	} else rc = UNKNOWN_COMMAND;

	message(rc);

	if (rc == FTP_EXIT)
		return -1;

	return 0;
}

/*
 *	strips surrounding quotes from all args
 */
PRIVATE void strip_quotes(int argc,char **argv)
{
char *p;
int i;

	for (i = 0; i < argc; i++, argv++) {
		if (**argv == '"') {
			(*argv)++;
			for (p = *argv; *p; p++)
				;
			*(p-1) = '\0';
		}
	}
}
