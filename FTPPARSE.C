/*
 * ftpparse: pftp parsing functions
 *
 * Copyright (c) 2013, 2018 Roger Burrows
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See LICENSE.TXT for details.
 */
#include "ftp.h"

/*
 *	function prototypes
 */
PRIVATE WORD get_next_arg(char **p,char **argv);

WORD parse_line(char *line,char **argv)
{
char *p, *temp;
WORD argc, rc;

	p = line;
	argc = 0;

	while(1) {
		rc = get_next_arg(&p,&temp);
		switch(rc) {
		case ARG_NORMAL:
			argv[argc++] = temp;
			break;
		case NO_MORE_ARGS:
			return argc;
			break;
		case QUOTING_ERROR:
			cputs("error in quoted field\r\n");
			return -1;
			break;
		default:
			cputs("error parsing line\r\n");
			return -1;
			break;
		}
	}

	return -1;
}

/*
 *	scans buffer for next arg, handles quoted args
 *
 *	returns:
 *		1	arg is normal
 *		0	no more args
 *		-1	quoting error
 *
 *	the buffer pointer is updated iff return code >= 0
 */
PRIVATE WORD get_next_arg(char **pp,char **arg)
{
char *p;
WORD inquotes = 0;

	/*
	 *	look for start of next arg
	 */
	for (p = *pp, *arg = NULL; *p; p++)
		if (*p != ' ')
			break;
	if (!*p) {			/* end of buffer */
		*pp = p;
		return NO_MORE_ARGS;
	}

	*arg = p;
	if (*p == '"') {
		inquotes = 1;
		p++;
	}

	for ( ; *p; p++) {
		if (*p == '"') {
			if (!inquotes)
				return QUOTING_ERROR;
			inquotes = 0;
			continue;
		}
		if (inquotes)
			continue;
		if (*p == ' ') {
			*p++ = '\0';
			break;
		}
	}

	if (inquotes)
		return QUOTING_ERROR;

	*pp = p;

	return ARG_NORMAL;
}
