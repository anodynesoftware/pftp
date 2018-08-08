/*
 * ftp.h: main header for pftp
 *
 * Copyright (c) 2013, 2018 Roger Burrows
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See LICENSE.TXT for details.
 */
#include <cookie.h>
#include <ctype.h>
#include <math.h>
#include <osbind.h>
#include <portab.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * By default, STinG is assumed to be providing the TCP/IP stack,
 * so STinG/STiK2 extensions to STiK are used.  To force strict
 * STiK1 compatibility, you may #define STIK1_COMPATIBLE here.
 */
/* #define STIK1_COMPATIBLE */

#define STiK_COOKIE		0x5354694bL

#define MAXPATHLEN      256

/*
 * program parameters
 */
#define REPLY_SIZE_INCR	1000	/* reply buffer is allocated in multiples of this */
#define MAX_LINE_SIZE   200     /* must be greater than the largest screen width */
#define HISTORY_SIZE    20      /* number of lines of history */
#define MAX_ARGS        30      /* maximum number of args we can parse */

#define PRIVATE         static  /* comment out for testing */


/*
 * date/time display format stuff
 */
#define _IDT_COOKIE     0x5f494454      /* '_IDT' */
#define _IDT_MDY        0               /* date format: month-day-year */
#define _IDT_DMY        1               /*              day-month-year */
#define _IDT_YMD        2               /*              year-month-day */
#define _IDT_YDM        3               /*              year-day-month */
#define _IDT_12H        0               /* time format: 12-hour */
#define _IDT_24H        1               /*              24-hour */

#define DEFAULT_DT_SEPARATOR    '/'
#define DEFAULT_DT_FORMAT   ((_IDT_12H<<12) + (_IDT_YMD<<8) + DEFAULT_DT_SEPARATOR)


/*
 *  typedefs
 */
typedef struct {
    char    d_reserved[21];
    char    d_attrib;
    WORD    d_time;
    WORD    d_date;
    LONG    d_length;
    char    d_fname[14];
} DTA;

typedef union {
	ULONG addr;
	UBYTE quad[4];
} IPADDR;

/* this controls the internal buffer used by mget()/mdelete() */
typedef struct {
	long count;							/* current number of entries */
	long size;							/* current size */
	char *start;						/* buffer start */
	char *pos;							/* current buffer position */
} BUFCTL;
#define INITIAL_BUFCTL_SIZE	1024		/* for 'size' above */


/*
 *  return codes from get_next_arg()
 */
#define ARG_NORMAL      1
#define NO_MORE_ARGS    0
#define QUOTING_ERROR   -1


/*
 *	error codes
 */
#define FTP_EXIT		999			/* not really an error ... */

#define UNKNOWN_COMMAND	-100
#define ARGCOUNT_ERROR	-101
#define INTERNAL_ERROR	-102
#define NOT_CONNECTED	-103
#define UNKNOWN_TYPE	-104
#define FILE_READ_ERROR	-105
#define FILE_WRITE_ERROR -106
#define FILE_NOT_FOUND	-107
#define INVALID_PATH    -108
#define MEMORY_ERROR	-109
#define USER_INTERRUPT	-110
#define NOMESSAGE_ERROR	-111		/* message() should not print message :-) */

#define ENMFIL			-49			/* standard GEMDOS */


/*
 *	manifest constants
 */
#define FTP_CONTROL_PORT	21

#define TAB             0x0f09
#define BKSP            0x0e08
#define DEL             0x537f
#define UPARROW         0x4800
#define DNARROW         0x5000
#define LTARROW         0x4b00
#define LTARROW_SHFT    0x4b34
#define RTARROW         0x4d00
#define RTARROW_SHFT    0x4d36

#define LF              0x0a
#define ESC             0x1b

#define CTL_C           ('C'-0x40)
#define CTL_Q           ('Q'-0x40)
#define CTL_S           ('S'-0x40)

#define cursor_left()   escape('D')
#define cursor_right()  escape('C')
#define conin()         Bconin(2)
#define constat()       Bconstat(2)
#define conout(c)       Bconout(2,c)

#define ring_bell()		conout(0x07)


/*
 *  global variables
 */
extern ULONG idt_value;
extern UWORD screen_cols, screen_rows;
extern UWORD linesize;
extern WORD transfer_type;
extern DTA dta;
extern int start_drive;
extern char start_path[MAXPATHLEN];

extern int globbing, passive, prompting, verbose, debug;
extern int bell, tick;

extern IPADDR ip;

/*
 *  function prototypes
 */
/* ftpmain.c */

/* ftpedit.c */
WORD init_cmdedit(void);
void insert_char(char *line,WORD pos,WORD len,char c);
WORD read_line(char *line);
void save_history(const char *line);

/* ftpint.c */
LONG (*lookup_builtin(WORD argc,char **argv))(WORD,char **);

/* ftpparse.c */
WORD parse_line(char *line,char **argv);

/* ftpsting.c */
LONG ftp_bye(void);
LONG ftp_cdup(void);
WORD ftp_connect(char *server,WORD port);
LONG ftp_cwd(char *path);
LONG ftp_delete(char *remotefile,int multiple);
LONG ftp_dir(char *remotedir,char *localfile);
WORD ftp_disconnect(void);
LONG ftp_get(char *localfile,char *remotefile,int multiple);
LONG ftp_matching(BUFCTL *bufctl,char *remotefile);
LONG ftp_mkdir(char *remotedir);
LONG ftp_nlist(char *remotedir,char *localfile);
LONG ftp_put(char *localfile,char *remotefile,int multiple);
LONG ftp_pwd(void);
LONG ftp_rename(char *oldname,char *newname);
LONG ftp_rmdir(char *remotedir);
LONG ftp_system(void);
LONG ftp_type(int type);
void message(LONG rc);
int sting_init(void);

/* ftputil.c */
int cgetc(void);
void cgets(char *buf);
void cgets_noecho(char *buf);
int cprintf(const char *fmt, ...);
void cputs(const char *buf);
void escape(char c);
WORD strequal(const char *s1,const char *s2);
void display_date_time(UWORD date,UWORD time);
char *get_basename(char *fullname);

/* ftpasm.s */
ULONG getwh(void);
