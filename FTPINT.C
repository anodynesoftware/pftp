/*
 * ftpint.c: pftp builtin commands
 *
 * Copyright (c) 2013, 2018 Roger Burrows
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See LICENSE.TXT for details.
 */

/*
 * the following internal commands have been implemented:
 *	ascii		bell		binary/image	bye/exit/quit
 *	cd			cdup		close/disconnect
 *	debug		delete		dir/ls
 *	get/recv	glob
 *	help/?
 *	lcd			ldir/lls
 *	mdelete		mget		mkdir			mput
 *	nlist
 *	open
 *	passive		prompt		put/send		pwd
 *	rename		rmdir
 *	status		system
 *	tick		type
 *	verbose
 *
 * the following internal commands *may* be implemented in a subsequent version:
 *	hash
 *	reset			clears reply queue.  perhaps add timeout to get_one_line(), then call get_reply() until timeout
 *	trace
 *	user
 */

#include "ftp.h"

/*
 *	COMMAND contains the command name and a pointer to
 *	the CMDINFO structure that contains all the info about
 *	a command.  This facilitates the usage of synonyms.
 */
typedef struct {
	WORD minargs;
	WORD maxargs;
	LONG (*func)(WORD argc,char **argv);
	const char * const *help;
} CMDINFO;

typedef struct {
	const char *name;
	const CMDINFO *info;
} COMMAND;

/*
 *	FINFO is used by the ldir/lls command
 */
typedef struct {
	char type;			/* to sort dirs ahead of files */
	char fname[13];
	short time;
	short date;
	long length;
} FINFO;

#define FINFO_QUANTUM	100

/*
 *	function prototypes
 */
PRIVATE LONG argcount_error(WORD argc,char **argv);

PRIVATE int dir_cmp(const void *a,const void *b);
PRIVATE void dir_display(FINFO *finfo);
PRIVATE WORD dir_read(char *path,FINFO **finfoptr);
PRIVATE void dir_sort(WORD entries,FINFO *finfo);

PRIVATE void help_display(const COMMAND *p,char *cmd);
PRIVATE WORD help_lines(const COMMAND *p);
PRIVATE WORD help_pause(void);
PRIVATE WORD help_synonym(const COMMAND *p);
PRIVATE WORD help_wanted(const COMMAND *p,char *cmd);

PRIVATE LONG run_ascii(WORD argc,char **argv);
PRIVATE LONG run_bell(WORD argc,char **argv);
PRIVATE LONG run_binary(WORD argc,char **argv);
PRIVATE LONG run_bye(WORD argc,char **argv);
PRIVATE LONG run_cd(WORD argc,char **argv);
PRIVATE LONG run_cdup(WORD argc,char **argv);
PRIVATE LONG run_close(WORD argc,char **argv);
PRIVATE LONG run_debug(WORD argc,char **argv);
PRIVATE LONG run_delete(WORD argc,char **argv);
PRIVATE LONG run_dir(WORD argc,char **argv);
PRIVATE LONG run_get(WORD argc,char **argv);
PRIVATE LONG run_glob(WORD argc,char **argv);
PRIVATE LONG run_help(WORD argc,char **argv);
PRIVATE LONG run_lcd(WORD argc,char **argv);
PRIVATE LONG run_ldir(WORD argc,char **argv);
PRIVATE LONG run_mdelete(WORD argc,char **argv);
PRIVATE LONG run_mget(WORD argc,char **argv);
PRIVATE LONG run_mkdir(WORD argc,char **argv);
PRIVATE LONG run_mput(WORD argc,char **argv);
PRIVATE LONG run_nlist(WORD argc,char **argv);
PRIVATE LONG run_open(WORD argc,char **argv);
PRIVATE LONG run_passive(WORD argc,char **argv);
PRIVATE LONG run_prompt(WORD argc,char **argv);
PRIVATE LONG run_put(WORD argc,char **argv);
PRIVATE LONG run_pwd(WORD argc,char **argv);
PRIVATE LONG run_rename(WORD argc,char **argv);
PRIVATE LONG run_rmdir(WORD argc,char **argv);
PRIVATE LONG run_status(WORD argc,char **argv);
PRIVATE LONG run_system(WORD argc,char **argv);
PRIVATE LONG run_tick(WORD argc,char **argv);
PRIVATE LONG run_type(WORD argc,char **argv);
PRIVATE LONG run_verbose(WORD argc,char **argv);

PRIVATE void toggle(int *value,char *text);

/*
 *	help strings
 */
MLOCAL const char * const help_ascii[] = { "",
	"Set transfer type to ASCII", NULL };
MLOCAL const char * const help_bell[] = { "",
	"Toggle bell sound at end of file transfer", NULL };
MLOCAL const char * const help_binary[] = { "",
	"Set transfer type to binary", NULL };
MLOCAL const char * const help_bye[] = { "",
	"Exit pftp", NULL };
MLOCAL const char * const help_cd[] = { "<rmtdir>",
	"Change remote directory to <rmtdir>", NULL };
MLOCAL const char * const help_cdup[] = { "",
	"Change to parent of remote directory", NULL };
MLOCAL const char * const help_close[] = { "",
	"Disconnect from remote server", NULL };
MLOCAL const char * const help_debug[] = { "[<level>]",
	"Set debugging level to <level>, or toggle",
	"debugging option if <level> not specified", NULL };
MLOCAL const char * const help_delete[] = { "<rmtfile>",
	"Delete remote file <rmtfile>", NULL };
MLOCAL const char * const help_dir[] = { "[<rmtdir> [<localfile>]]",
	"Get full listing of <rmtdir> (or current remote",
	"directory if <rmtdir> not specified) into <localfile>",
	"(or to screen if <localfile> not specified)", NULL };
MLOCAL const char * const help_get[] = { "<rmtfile> [<localfile>]",
	"Get remote file <rmtfile> and store locally with",
	"the same name, or as <localfile> if specified", NULL };
MLOCAL const char * const help_glob[] = { "",
	"Toggle filename globbing, which controls",
	"wildcard expansion for mdelete/mget/mput", NULL };
MLOCAL const char * const help_help[] = { "[<cmd>]",
	"Get help about <cmd> or list available commands",
	"Use HELP ALL for help on all commands",
	"Use HELP EDIT for help on line editing", NULL };
MLOCAL const char * const help_lcd[] = { "<path>",
	"Change local directory to <path>", NULL };
MLOCAL const char * const help_ldir[] = { "[<localdir>]",
	"Get full listing of <localdir> (or current",
	"local directory if <localdir> not specified)", NULL };
MLOCAL const char * const help_mdelete[] = { "<rmtfiles>",
	"Delete multiple remote files specified by <rmtfiles>", NULL };
MLOCAL const char * const help_mget[] = { "<rmtfiles>",
	"Get multiple remote files specified by <rmtfiles>", NULL };
MLOCAL const char * const help_mkdir[] = { "<rmtdir>",
	"Create remote directory <rmtdir>", NULL };
MLOCAL const char * const help_mput[] = { "<localfiles>",
	"Put multiple local files specified by <localfiles>", NULL };
MLOCAL const char * const help_nlist[] = { "<rmtdir> [<localfile>]",
	"Get listing of <rmtdir> names into <localfile>",
	"or to screen if <localfile> not specified", NULL };
MLOCAL const char * const help_open[] = { "<server> [<port>]",
	"Connect to server", NULL };
MLOCAL const char * const help_passive[] = { "",
	"Toggle passive mode", NULL };
MLOCAL const char * const help_prompt[] = { "",
	"Toggle interactive prompting for",
	"mdelete/mget/mput", NULL };
MLOCAL const char * const help_put[] = { "<localfile> [<rmtfile>]",
	"Put local file <localfile> and store remotely with",
	"the same name, or as <rmtfile> if specified", NULL };
MLOCAL const char * const help_pwd[] = { "",
	"Display name of current directory on",
	"remote machine", NULL };
MLOCAL const char * const help_rename[] = { "<oldname> <newname>",
	"Rename remote file <oldname> to <newname>", NULL };
MLOCAL const char * const help_rmdir[] = { "<rmtdir>",
	"Remove remote directory <rmtdir>", NULL };
MLOCAL const char * const help_status[] = { "",
	"Display current ftp status", NULL };
MLOCAL const char * const help_system[] = { "",
	"Display info about remote system", NULL };
MLOCAL const char * const help_tick[] = { "",
	"Toggle tick counter display during file transfer", NULL };
MLOCAL const char * const help_type[] = { "<type>",
	"Set transfer type to <type>; <type> is",
	"ASCII or BINARY or IMAGE", NULL };
MLOCAL const char * const help_verbose[] = { "",
	"Toggle verbose mode, which controls display",
	"of server messages and file transfer speeds", NULL };

MLOCAL const char * const help_edit[] = {
 "up/down arrow = previous/next line in history",
 "left/right arrow = previous/next character",
 "shift-left/right arrow = previous/next word", NULL };


/*
 *	command info structures
 */
MLOCAL CMDINFO info_ascii =		{ 0, 0, run_ascii, help_ascii };
MLOCAL CMDINFO info_bell =		{ 0, 0, run_bell, help_bell };
MLOCAL CMDINFO info_binary =	{ 0, 0, run_binary, help_binary };
MLOCAL CMDINFO info_bye =		{ 0, 0, run_bye, help_bye };
MLOCAL CMDINFO info_cd =		{ 1, 1, run_cd, help_cd };
MLOCAL CMDINFO info_cdup =		{ 0, 0, run_cdup, help_cdup };
MLOCAL CMDINFO info_close =		{ 0, 0, run_close, help_close };
MLOCAL CMDINFO info_debug =		{ 0, 1, run_debug, help_debug };
MLOCAL CMDINFO info_delete =	{ 1, 1, run_delete, help_delete };
MLOCAL CMDINFO info_dir =		{ 0, 2, run_dir, help_dir };
MLOCAL CMDINFO info_get =		{ 1, 2, run_get, help_get };
MLOCAL CMDINFO info_glob =		{ 0, 0, run_glob, help_glob };
MLOCAL CMDINFO info_help =		{ 0, 1, run_help, help_help };
MLOCAL CMDINFO info_lcd =		{ 0, 1, run_lcd, help_lcd };
MLOCAL CMDINFO info_ldir =		{ 0, 1, run_ldir, help_ldir };
MLOCAL CMDINFO info_mdelete =	{ 1, 1, run_mdelete, help_mdelete };
MLOCAL CMDINFO info_mget =		{ 1, 1, run_mget, help_mget };
MLOCAL CMDINFO info_mkdir =		{ 1, 1, run_mkdir, help_mkdir };
MLOCAL CMDINFO info_mput =		{ 1, 1, run_mput, help_mput };
MLOCAL CMDINFO info_nlist =		{ 0, 2, run_nlist, help_nlist };
MLOCAL CMDINFO info_open =		{ 1, 2, run_open, help_open };
MLOCAL CMDINFO info_passive =	{ 0, 0, run_passive, help_passive };
MLOCAL CMDINFO info_prompt =	{ 0, 0, run_prompt, help_prompt };
MLOCAL CMDINFO info_put =		{ 1, 2, run_put, help_put };
MLOCAL CMDINFO info_pwd =		{ 0, 0, run_pwd, help_pwd };
MLOCAL CMDINFO info_rename =	{ 2, 2, run_rename, help_rename };
MLOCAL CMDINFO info_rmdir =		{ 1, 1, run_rmdir, help_rmdir };
MLOCAL CMDINFO info_status =	{ 0, 0, run_status, help_status };
MLOCAL CMDINFO info_system =	{ 0, 0, run_system, help_system };
MLOCAL CMDINFO info_tick =		{ 0, 0, run_tick, help_tick };
MLOCAL CMDINFO info_type =		{ 0, 1, run_type, help_type };
MLOCAL CMDINFO info_verbose =	{ 0, 0, run_verbose, help_verbose };


/*
 *	command name table
 */
MLOCAL const COMMAND cmdtable[] = {
	{ "?", &info_help },
	{ "ascii", &info_ascii },
	{ "bell", &info_bell },
	{ "binary", &info_binary },
	{ "bye", &info_bye },
	{ "cd", &info_cd },
	{ "cdup", &info_cdup },
	{ "close", &info_close },
	{ "debug", &info_debug },
	{ "delete", &info_delete },
	{ "dir", &info_dir },
	{ "disconnect", &info_close },
	{ "exit", &info_bye },
	{ "get", &info_get },
	{ "glob", &info_glob },
	{ "help", &info_help },
	{ "image", &info_binary },
	{ "lcd", &info_lcd },
	{ "ldir", &info_ldir },
	{ "lls", &info_ldir },
	{ "ls", &info_dir },
	{ "mdelete", &info_mdelete },
	{ "mget", &info_mget },
	{ "mkdir", &info_mkdir },
	{ "mput", &info_mput },
	{ "nlist", &info_nlist },
	{ "open", &info_open },
	{ "passive", &info_passive },
	{ "prompt", &info_prompt },
	{ "put", &info_put },
	{ "pwd", &info_pwd },
	{ "quit", &info_bye },
	{ "recv", &info_get },
	{ "rename", &info_rename },
	{ "rmdir", &info_rmdir },
	{ "send", &info_put },
	{ "status", &info_status },
	{ "system", &info_system },
	{ "tick", &info_tick },
	{ "type", &info_type },
	{ "verbose", &info_verbose },
	{ "", NULL }					/* end marker */
};

LONG (*lookup_builtin(WORD argc,char **argv))(WORD,char **)
{
const COMMAND *p;
const CMDINFO *q;

	/*
	 *	allow -h with any command to provide help
	 */
	if ((argc == 2) && strequal(argv[1],"-h")) {
		argv[1] = argv[0];
		argv[0] = "help";
	}

	/*
	 *	scan command table
	 */
	for (p = cmdtable; p->info; p++)
		if (strequal(argv[0],p->name))
			break;

	if (!p->info)
		return NULL;

	q = p->info;
	argc--;
	if ((argc < q->minargs) || (argc > q->maxargs))
		return argcount_error;

	return q->func;
}

PRIVATE LONG argcount_error(WORD argc,char **argv)
{
	return ARGCOUNT_ERROR;
}

PRIVATE LONG run_ascii(WORD argc,char **argv)
{
LONG rc;

	rc = ftp_type('A');

	if (rc >= 0)
		transfer_type = 'A';

	return rc;
}

PRIVATE LONG run_bell(WORD argc,char **argv)
{
	toggle(&bell,"Bell");

	return 0L;
}

PRIVATE LONG run_binary(WORD argc,char **argv)
{
LONG rc;

	rc = ftp_type('I');

	if (rc >= 0)
		transfer_type = 'I';

	return rc;
}

PRIVATE LONG run_bye(WORD argc,char **argv)
{
	ftp_bye();

	return FTP_EXIT;
}

PRIVATE LONG run_cd(WORD argc,char **argv)
{
	return ftp_cwd(argv[1]);
}

PRIVATE LONG run_cdup(WORD argc,char **argv)
{
	return ftp_cdup();
}

PRIVATE LONG run_close(WORD argc,char **argv)
{
	return ftp_disconnect();
}

PRIVATE LONG run_debug(WORD argc,char **argv)
{
	if (argc == 1)
		debug = debug ? 0 : 1;
	else debug = atoi(argv[1]);

	cprintf("Debugging is %s (debug=%d)\r\n",debug?"on":"off",debug);

	return 0L;
}

PRIVATE LONG run_delete(WORD argc,char **argv)
{
	return ftp_delete(argv[1],0);
}

PRIVATE LONG run_dir(WORD argc,char **argv)
{
char *remotedir = NULL;
char *localfile = NULL;

	switch(argc) {
	case 3:
		localfile = argv[2];
		/* drop thru */
	case 2:
		remotedir = argv[1];
	}
		
	return ftp_dir(remotedir,localfile);
}

PRIVATE LONG run_get(WORD argc,char **argv)
{
char *localfile, *remotefile;

	remotefile = argv[1];
	localfile = (argc == 3) ? argv[2] : remotefile;

	return ftp_get(localfile,remotefile,0);
}

PRIVATE LONG run_glob(WORD argc,char **argv)
{
	toggle(&globbing,"Globbing");

	return 0L;
}

PRIVATE LONG run_help(WORD argc,char **argv)
{
const COMMAND *p;
WORD i, lines;
int all;
const char * const *s;

	if (argc == 1) {
		cputs("Builtin commands:");
		for (i = 0, p = cmdtable; p->info; p++, i++) {
			if (i%5 == 0)
				cputs("\r\n  ");
			cprintf("%-12.12s",p->name);
		}
		cputs("\r\n");
		return 0L;
	}

	if (strequal(argv[1],"edit")) {
		for (s = &help_edit[0]; *s; s++)
			cprintf("  %s\r\n",*s);
		return 0L;
	}

	all = strequal(argv[1],"all");

	for (p = cmdtable, lines = 0; p->info; p++) {
		/*
		 *	if "all", check for synonyms to ensure we
		 *	only list help for each command once
		 */
		if (all)
			if (help_synonym(p))
				continue;

		if (help_wanted(p,argv[1])) {
			lines += help_lines(p); 	/* see if this help will fit on screen */
			if (all)
				lines++;				/* allow for blank line separator */
			if (lines >= screen_rows) {
				if (help_pause() < 0)
					break;
				lines = 0;
			}
			help_display(p,argv[1]);
			if (all)
				cputs("\r\n");
		}
	}

	return 0L;
}

PRIVATE LONG run_lcd(WORD argc,char **argv)
{
int olddrv, newdrv;
char *path = NULL;
char curpath[MAXPATHLEN];
char *p;
LONG rc;

	olddrv = Dgetdrv() + 'A';

	if (argc == 1) {					/* restore original */
		newdrv = start_drive;
		path = start_path;
	} else {							/* set new */
		p = argv[1];
		if (*(p+1) == ':') {
			newdrv = toupper(*p);
			path = p + 2;
		} else {
			newdrv = olddrv;
			path = p;
		}
		if (!*p)
			path = "\\";
	}

	Dsetdrv(newdrv-'A');
	if (Dgetdrv() == newdrv-'A') {		/* worked OK */
		rc = Dsetpath(path);
		if (rc)							/* if error changing path, */
			Dsetdrv(olddrv-'A');		/* restore old drive */
	}

	if (rc == 0L) {
		Dgetpath(curpath,0);
		cprintf("Local directory now %c:%s\r\n",Dgetdrv()+'A',curpath);
	} else rc = INVALID_PATH;

	return rc;
}

PRIVATE LONG run_ldir(WORD argc,char **argv)
{
char path[MAXPATHLEN];
FINFO *finfo, *p;
WORD i, entries;

	if (argc == 1)
		strcpy(path,"*.*");
	else {
		strcpy(path,argv[1]);
		strcat(path,"\\*.*");
	}

	entries = dir_read(path,&finfo);
	if (entries < 0)
		return entries;

	dir_sort(entries,finfo);

	for (i = 0, p = finfo; i < entries; i++, p++)
		dir_display(p);

	free(finfo);		/* free gotten memory */

	return 0L;
}

PRIVATE LONG run_mdelete(WORD argc,char **argv)
{
BUFCTL bufctl;
char *p;
LONG n, rc;

	if (!globbing)
		return ftp_delete(argv[1],1);

	rc = ftp_matching(&bufctl,argv[1]);

	if (rc >= 0L) {
		for (n = 0, p = bufctl.start, rc = 0L; n < bufctl.count; n++, p += strlen(p)+1, rc = 0L) {
			rc = ftp_delete(p,1);
			message(rc);
			if (rc < 0L)
				break;
		}
	}

	if (bufctl.start)
		free(bufctl.start);

	return 0L;
}

PRIVATE LONG run_mget(WORD argc,char **argv)
{
BUFCTL bufctl;
char *p;
LONG n, rc;

	if (!globbing)
		return ftp_get(argv[1],argv[1],1);

	rc = ftp_matching(&bufctl,argv[1]);

	if (rc >= 0L) {
		for (n = 0, p = bufctl.start, rc = 0L; n < bufctl.count; n++, p += strlen(p)+1, rc = 0L) {
			rc = ftp_get(p,p,1);
			message(rc);
			if (rc < 0L)
				break;
		}
	}

	if (bufctl.start)
		free(bufctl.start);

	return 0L;
}

PRIVATE LONG run_mkdir(WORD argc,char **argv)
{
	return ftp_mkdir(argv[1]);
}

PRIVATE LONG run_mput(WORD argc,char **argv)
{
LONG rc;

	if (!globbing)
		return ftp_put(argv[1],argv[1],1);

	/*
	 *	read dir, ignoring . and ..
	 */
	for (rc = Fsfirst(argv[1],0); rc == 0; rc = Fsnext()) {
		if (dta.d_fname[0] == '.')
			continue;
		rc = ftp_put(dta.d_fname,dta.d_fname,1);
		message(rc);
		if (rc < 0L)
			break;
	}

	return 0L;
}

PRIVATE LONG run_nlist(WORD argc,char **argv)
{
char *remotedir = NULL;
char *localfile = NULL;

	switch(argc) {
	case 3:
		localfile = argv[2];
		/* drop thru */
	case 2:
		remotedir = argv[1];
	}
		
	return ftp_nlist(remotedir,localfile);
}

PRIVATE LONG run_open(WORD argc,char **argv)
{
WORD port = FTP_CONTROL_PORT;

	if (argc == 3)
		port = atoi(argv[2]);

	return ftp_connect(argv[1],port);
}

PRIVATE LONG run_passive(WORD argc,char **argv)
{
	toggle(&passive,"Passive mode");

	return 0L;
}

PRIVATE LONG run_prompt(WORD argc,char **argv)
{
	toggle(&prompting,"Prompting");

	return 0L;
}

PRIVATE LONG run_put(WORD argc,char **argv)
{
char *localfile, *remotefile;
LONG rc;

	localfile = argv[1];
	remotefile = (argc == 3) ? argv[2] : get_basename(localfile);

	if (Fsfirst(localfile,0) != 0) {
		cprintf("local: %s: no such file\r\n",localfile);
		return 0L;
	}

	rc = ftp_put(localfile,remotefile,0);

	return 0L;
}

PRIVATE LONG run_pwd(WORD argc,char **argv)
{
	return ftp_pwd();
}

PRIVATE LONG run_rename(WORD argc,char **argv)
{
	return ftp_rename(argv[1],argv[2]);
}

PRIVATE LONG run_rmdir(WORD argc,char **argv)
{
	return ftp_rmdir(argv[1]);
}

PRIVATE LONG run_status(WORD argc,char **argv)
{
	if (ip.addr)
		cprintf("Connected to %d.%d.%d.%d\r\n",ip.quad[0],ip.quad[1],ip.quad[2],ip.quad[3]);
	else cputs("Not connected\r\n");
	cprintf("Type: %s\r\n",(transfer_type=='A')?"ascii":"binary");
	cprintf("Verbose: %s; Bell: %s; Prompting: %s; Globbing: %s\r\n",
		verbose?"on":"off",bell?"on":"off",prompting?"on":"off",globbing?"on":"off");
	cprintf("Tick counter printing: %s\r\n",tick?"on":"off");

	return 0L;
}

PRIVATE LONG run_system(WORD argc,char **argv)
{
	return ftp_system();
}

PRIVATE LONG run_tick(WORD argc,char **argv)
{
	toggle(&tick,"Tick counter");

	return 0L;
}

PRIVATE LONG run_type(WORD argc,char **argv)
{
LONG rc;

	if (argc == 1) {
		cprintf("Using %s mode to transfer files\r\n",
				(transfer_type=='A')?"ascii":"binary");
		return 0L;
	}

	if (strequal(argv[1],"ascii"))
		rc = run_ascii(0,NULL);
	else if (strequal(argv[1],"binary") || strequal(argv[1],"image"))
		rc = run_binary(0,NULL);
	else rc = UNKNOWN_TYPE;

	return rc;
}

PRIVATE LONG run_verbose(WORD argc,char **argv)
{
	toggle(&verbose,"Verbose mode");

	return 0L;
}



/*  *  *  *  *  *  *  *  *  *  *  *  *  *  *
 *                                         *
 *  I N T E R N A L   F U N C T I O N S    *
 *                                         *
 *  *  *  *  *  *  *  *  *  *  *  *  *  *  */

PRIVATE void help_display(const COMMAND *p,char *cmd)
{
const COMMAND *r;
const CMDINFO *q = p->info;
const char * const *s;
int synonyms = 0;

	/*
	 *	if "all", check for synonyms to ensure we
	 *	only list help for each command once
	 */
	if (strequal(cmd,"all")) {
		for (r = cmdtable; r->info; r++) {
			if (p == r)				/* first occurrence */
				break;
			if (r->info == p->info)	/* synonym of an earlier command */
				return;
		}
	}

	cprintf("  %s %s\r\n",p->name,q->help[0]);

	for (s = &q->help[1]; *s; s++)
		cprintf("    %s\r\n",*s);

	/*
	 *	display synonyms
	 */
	cputs("  Synonyms:");

	for (r = cmdtable; r->info; r++) {
		if (p == r)		/* ignore ourselves */
			continue;
		if (r->info == p->info) {
			cprintf("  %s",r->name);
			++synonyms;
		}
	}
	if (!synonyms)
		cputs("  (none)");
	cputs("\r\n");
}

PRIVATE WORD help_lines(const COMMAND *p)
{
const CMDINFO *q = p->info;
const char * const *s;
WORD lines;

	for (s = &q->help[0], lines = 0; *s; s++, lines++)
		;

	return lines+1;		/* allow for synonym line */
}

PRIVATE WORD help_pause(void)
{
char c;
WORD rc;

	cputs("CR to continue ...");
	while(1) {
		c = conin() & 0xff;
		if (c == '\r') {
			rc = 0;
			break;
		}
		if (c == CTL_C) {
			rc = -1;
			break;
		}
	}
	cputs("\r\n");

	return rc;
}

PRIVATE WORD help_synonym(const COMMAND *p)
{
const COMMAND *q;

	for (q = cmdtable; q->info; q++) {
		if (p == q)		/* this is the first occurrence */
			break;
		if (q->info == p->info)
			return 1;
	}

	return 0;
}

PRIVATE WORD help_wanted(const COMMAND *p,char *cmd)
{
	if (strequal(cmd,"all"))
		return 1;
	if (strequal(cmd,p->name))
		return 1;

	return 0;
}

PRIVATE int dir_cmp(const void *a,const void *b)
{
	return memcmp(a,b,sizeof(FINFO));
}

PRIVATE void dir_display(FINFO *finfo)
{
	cprintf("%-12.12s ",finfo->fname);

	if (finfo->type)
		cprintf("%10ld  ",finfo->length);
	else cputs("     <dir>  ");

	display_date_time(finfo->date,finfo->time);
	cputs("\r\n");
}

PRIVATE WORD dir_read(char *path,FINFO **finfoptr)
{
FINFO *start = NULL, *p, *temp;
LONG rc;
WORD entries;

	/*
	 *	ignore . and ..
	 */
	for (rc = Fsfirst(path,0x17); rc == 0; rc = Fsnext())
		if (dta.d_fname[0] != '.')
			break;

	if (rc < 0)
		return 0;

	/*
	 *	store file info in memory
	 *
	 *	we read until end of the directory or until we fill
	 *	the current array.  in the latter case, we allocate
	 *	a new array, copy the existing one to the new one,
	 *	and continue.
	 */
	for (rc = 0, p = start, entries = 0; rc == 0; rc = Fsnext(), entries++, p++) {
		if (entries%FINFO_QUANTUM == 0) {
			temp = calloc(entries+FINFO_QUANTUM,sizeof(FINFO));
			if (!temp) {		/* out of memory */
				rc = MEMORY_ERROR;
				break;
			}
			if (start) {
				memcpy(temp,start,entries*sizeof(FINFO));
				free(start);
			}
			start = temp;
			p = start + entries;
		}
		p->type = (dta.d_attrib & 0x10) ? 0x00 : 0x01;
		strcpy(p->fname,dta.d_fname);
		p->time = dta.d_time;
		p->date = dta.d_date;
		p->length = dta.d_length;
	}

	if (rc != ENMFIL) {
		if (start)
			free(start);
		return (WORD)rc;
	}

	*finfoptr = start;	/* return ptr to current array */

	return entries;
}

PRIVATE void dir_sort(WORD entries,FINFO *finfo)
{
	qsort(finfo,entries,sizeof(FINFO),dir_cmp);
}

PRIVATE void toggle(int *value,char *text)
{
	*value = *value ? FALSE : TRUE;
	cprintf("%s is %s\r\n",text,*value?"on":"off");
}
