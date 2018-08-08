/*
 * ftpsting.c: pftp interface to STinG
 *
 * Copyright (c) 2013, 2018 Roger Burrows
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See LICENSE.TXT for details.
 */
#include "ftp.h"
#include <stdio.h>
#include <limits.h>

/*
 *	Lattice C compatibility stuff
 */
#ifdef LATTICE
	#define cdecl
	#include <sys/types.h>
	#include <basepage.h>
	#define BASPAG	BASEPAGE
#endif

#include <transprt.h>

/*
 *	buffer sizes: both must be <= SHRT_MAX
 */
#define IOBUFSIZE			32000		/* for ls/get/put */
#ifdef STIK1_COMPATIBLE
#define TCPBUFSIZE			8000		/* undocumented max for MagicNet's GlueSTiK (at least) */
#else
#define TCPBUFSIZE			32000		/* STinG allows this */
#endif

#define MAXCMDLEN			256			/* longest command to send to server */

/*
 *	range of ports to use per IANA
 *		*except*
 *	we don't use 65535 since that's reserved by STinG to
 *	indicate STinG-style extended addressing for TCP_open()
 */
#define FIRST_DYNAMIC_PORT	49152U
#define LAST_DYNAMIC_PORT	65534U
#define NUM_DYNAMIC_PORTS	(LAST_DYNAMIC_PORT-FIRST_DYNAMIC_PORT+1)

#define HEADER_LEN			4			/* "NNN " or "NNN-" */

/* for 'tick' display */
#define XFER_QUANTUM		(10 * 1024)
#define FORMAT_XFER_MSG		"Bytes transferred = %ld\r"
#define BLANKOUT_XFER_MSG	"                              \r"

/*
 *	globals
 */
WORD transfer_type = 'I';				/* note, image (binary) by default */

MLOCAL TPL *tpl = NULL;
MLOCAL WORD handle = -1;
MLOCAL WORD last_type_set = -1;
MLOCAL ULONG transfer_bytes;			/* for measuring get/put */
MLOCAL ULONG transfer_ticks;			/*  transfer rates       */

MLOCAL char cmdsave[80];				/* holds text of command actually sent */
MLOCAL char header[HEADER_LEN+1];		/* holds NNN prefix */
MLOCAL long reply_size = 0L;			/* current size of following buffer */
MLOCAL char *reply;						/* holds text of reply (including header(s)) */

MLOCAL char iobuf[IOBUFSIZE+1];			/* for file transfer */

/*
 *	function prototypes
 */
PRIVATE WORD abort_transfer(void);
PRIVATE char *expand_buffer(void);
PRIVATE int extract_hp(CAB *cab);
PRIVATE void display_transfer_stats(void);
PRIVATE WORD ftp_data_connect(void);
PRIVATE LONG ftp_directory(char *cmd,char *remotedir,char *localfile,BUFCTL *bufctl);
PRIVATE UWORD generate_port(void);
PRIVATE WORD get_one_line(WORD handle,char **replyptr,long maxlen);
PRIVATE WORD get_reply(WORD handle);
PRIVATE WORD open_connection(char *server,int port,ULONG *addr);
PRIVATE int prompt_and_reply(char *cmd,char *file);
PRIVATE WORD send_command(WORD handle,char *command);
PRIVATE WORD user_break(void);
PRIVATE WORD user_input(void);
PRIVATE void bufstore(BUFCTL *bufctl,char *name);


/*
 *	standard STinG initialisation
 */
int sting_init(void)
{
DRV_LIST *drivers;

	if (!getcookie(STiK_COOKIE,(LONG *)&drivers))
		return -4;			/* no cookie */

	if (!drivers)
		return -3;			/* shouldn't happen */

	if (strcmp(MAGIC,drivers->magic))
		return -2;			/* corrupted ... */

	if (!(tpl=(TPL *)get_dftab(TRANSPORT_DRIVER)))
		return -1;			/* no transport layer */

	return 0;
}

/*
 *	Attempts to connect to specified server
 *	Returns:	<0	error (standard STinG)
 *				0	ok (control handle is in 'handle')
 *				>0	message from server is in reply[]
 */
WORD ftp_connect(char *server,WORD port)
{
int rc;
char buf[80];
char command[MAXCMDLEN];

	/*
	 *	allocate reply buffer if necessary
	 */
	if (!reply) {
		reply = expand_buffer();
		if (!reply)
			return MEMORY_ERROR;
	}

	/*
	 *	open connection
	 */
	handle = open_connection(server,port,&ip.addr);

	if (handle < 0)
		return handle;

	rc = get_reply(handle);

	if (rc == 120) {
		cputs(reply);
		rc = get_reply(handle);
	}

	if (rc != 220)
		return rc;

	cputs(reply);

	/*
	 *	prompt for name
	 */
	cprintf("Name (%d.%d.%d.%d): ",ip.quad[0],ip.quad[1],ip.quad[2],ip.quad[3]);
	cgets(buf);
	sprintf(command,"USER %s",buf);
	rc = send_command(handle,command);
	if (rc < 0)
		return rc;

	/*
	 *	handle password prompt
	 */
	if (rc == 331) {
		cputs(reply);
		cputs("Password: ");
		cgets_noecho(buf);			/* do not display! */
		sprintf(command,"PASS %s",buf);
		rc = send_command(handle,command);
		if (rc < 0)
			return rc;
	}

	/*
	 *	handle the rarely-seen account prompt
	 */
	if (rc == 332) {
		cputs(reply);
		cputs("Account: ");
		cgets(buf);
		sprintf(command,"ACCT %s",buf);
		rc = send_command(handle,command);
		if (rc < 0)
			return rc;
	}

	return send_command(handle,"SYST");
}

/*
 *	Attempt to set up a data transfer connection
 *	Returns:	<0	error (standard STinG or our own)
 *				>=0	handle
 */
PRIVATE WORD ftp_data_connect(void)
{
WORD rc;
CIB *cib;
IPADDR u;
CAB cab;
char command[MAXCMDLEN];

	/*
	 *	get our ip address
	 */
	cib = CNgetinfo(handle);
	if (!cib)
		return INTERNAL_ERROR;

	/*
	 *	for passive connections, need to query server to find
	 *	out which of its ports to use before we can connect
	 */
	if (passive) {
		rc = send_command(handle,"PASV");
		message(rc);
		if (rc != 227)
			return NOMESSAGE_ERROR;
		if (extract_hp(&cab) < 0)		/* get server's ip addr & port */
			return INTERNAL_ERROR;
#ifdef STIK1_COMPATIBLE
		return TCP_open(cab.rhost,cab.rport,0,TCPBUFSIZE);
#else
		cab.lhost = cib->address.lhost;
		cab.lport = 0;
		return TCP_open((uint32)&cab,TCP_ACTIVE,0,TCPBUFSIZE);
#endif
	}

	/*
	 *	handle non-passive (active?) connections
	 */
	cab.lhost = cib->address.lhost;		/* our ip address */
	cab.lport = generate_port();		/* generate a port */

	/*
	 *	tell server which of our ports to use
	 */
	u.addr = cab.lhost;
	sprintf(command,"PORT %d,%d,%d,%d,%d,%d",
			u.quad[0],u.quad[1],u.quad[2],u.quad[3],cab.lport>>8,cab.lport&0xff);
	if ((rc=send_command(handle,command)) < 0)
		return rc;
	message(rc);

#ifdef STIK1_COMPATIBLE
	return TCP_open(0,cab.lport,0,TCPBUFSIZE);
#else
	cab.rhost = cib->address.rhost;
	cab.rport = 0;
	return TCP_open((uint32)&cab,TCP_PASSIVE,0,TCPBUFSIZE);
#endif
}

/*
 *	Disconnect
 */
WORD ftp_disconnect(void)
{
WORD rc;

	if (handle < 0)
		return NOT_CONNECTED;

	rc = TCP_close(handle,5,NULL);
	handle = -1;

	return rc;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                       *
 *   U S E R   C O M M A N D   H A N D L E R S           *
 *                                                       *
 *   Return:  <0   error (standard STinG + our own)      *
 *            =0   ok                                    *
 *            >0   message from server is in reply[]     *
 *                                                       *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
LONG ftp_bye(void)
{
	if (handle < 0)
		return NOT_CONNECTED;

	return send_command(handle,"QUIT");
}

LONG ftp_cdup(void)
{
	if (handle < 0)
		return NOT_CONNECTED;

	return send_command(handle,"CDUP");
}

LONG ftp_cwd(char *path)
{
char command[MAXCMDLEN];

	if (handle < 0)
		return NOT_CONNECTED;

	sprintf(command,"CWD %s",path);
	return send_command(handle,command);
}

LONG ftp_delete(char *remotefile,int multiple)
{
char command[MAXCMDLEN];

	if (handle < 0)
		return NOT_CONNECTED;

	/*
	 *	if this is an mdelete(), handle prompt
	 */
	if (multiple) {
		switch(prompt_and_reply("mdelete",remotefile)) {
		case -1:
			return USER_INTERRUPT;
		case 0:
			return 0L;
		}
	}

	sprintf(command,"DELE %s",remotefile);
	return send_command(handle,command);
}

LONG ftp_dir(char *remotedir,char *localfile)
{
	return ftp_directory("LIST",remotedir,localfile,NULL);
}

LONG ftp_get(char *localfile,char *remotefile,int multiple)
{
WORD data;		/* data port handle */
WORD fh;		/* file handle */
WORD n, rc;
LONG rc2;
ULONG start, prev_bytes;
char command[MAXCMDLEN];

	if (handle < 0)
		return NOT_CONNECTED;

	/*
	 *	if this is an mget(), handle prompt
	 */
	if (multiple) {
		switch(prompt_and_reply("mget",remotefile)) {
		case -1:
			return USER_INTERRUPT;
		case 0:
			return 0L;
		}
	} else cprintf("local: %s remote: %s\r\n",localfile,remotefile);

	/*
	 *	make sure the type is correct
	 */
	if (last_type_set != transfer_type) {
		rc = (WORD)ftp_type(transfer_type);
		if (rc != 200)
			return rc;
		message(rc);
	}

	/*
	 *	establish a data connection
	 */
	data = ftp_data_connect();
	if (data < 0)
		return data;

	/*
	 *	tell server we want to retrieve a file
	 */
	sprintf(command,"RETR %s",remotefile);
	rc = send_command(handle,command);

	/*
	 *	see if that's ok
	 */
	if ((rc != 125) && (rc != 150)) {
		TCP_close(data,5,NULL);
		return rc;
	}
	message(rc);

	start = clock();		/* start timing */
	prev_bytes = transfer_bytes = 0UL;

	/*
	 *	copy file across network
	 */
	rc2 = Fcreate(localfile,0);
	if (rc2 >= 0L) {
		fh = (WORD)rc2;
		while(1) {
			if (constat()) {
				if (user_break()) {
					rc = abort_transfer();
					break;
				}
			}
			rc = n = CNbyte_count(data);
			if (rc == 0)
				continue;
			if (rc < 0)
				break;
			if (n > IOBUFSIZE)
				n = IOBUFSIZE;
			rc = CNget_block(data,iobuf,n);
			if (rc != n) {
				if (rc >= 0)
					rc = INTERNAL_ERROR;
				break;
			}
			rc = (WORD)Fwrite(fh,n,iobuf);
			if (rc != n) {
				rc = FILE_WRITE_ERROR;
				break;
			}
			transfer_bytes += n;
			if (tick && (transfer_bytes-prev_bytes > XFER_QUANTUM)) {
				cprintf(FORMAT_XFER_MSG,transfer_bytes);
				prev_bytes = transfer_bytes;
			}
		}
		Fclose(fh);
		if (tick)
			cputs(BLANKOUT_XFER_MSG);
		if (rc == E_EOF)
			rc = 0;
	}

	n = TCP_close(data,5,NULL);
	if (rc == 0)
		rc = n;

	transfer_ticks = clock() - start;

	if (rc == 0)
		rc = get_reply(handle);

	message(rc);	/* print server msg before timing */

	if (verbose && ((rc == 226) || (rc == 250)))
		display_transfer_stats();

	if (bell)
		ring_bell();

	return 0L;
}

LONG ftp_mkdir(char *remotedir)
{
char command[MAXCMDLEN];

	if (handle < 0)
		return NOT_CONNECTED;

	sprintf(command,"MKD %s",remotedir);
	return send_command(handle,command);
}

/*
 *	get matching files for mdelete()/mget()
 */
LONG ftp_matching(BUFCTL *bufctl,char *remotefile)
{
	if (handle < 0)
		return NOT_CONNECTED;

	bufctl->count = 0;
	bufctl->size = INITIAL_BUFCTL_SIZE;
	bufctl->start = malloc(bufctl->size);
	bufctl->pos = bufctl->start;
	if (!bufctl->start)
		return MEMORY_ERROR;

	return ftp_directory("NLST",remotefile,NULL,bufctl);
}

LONG ftp_nlist(char *remotedir,char *localfile)
{
	return ftp_directory("NLST",remotedir,localfile,NULL);
}

LONG ftp_put(char *localfile,char *remotefile,int multiple)
{
WORD data;		/* data port handle */
WORD fh;		/* file handle */
WORD rc, n, remaining;
LONG rc2;
char *p;
ULONG start, prev_bytes;
char command[MAXCMDLEN];

	if (handle < 0)
		return NOT_CONNECTED;

	/*
	 *	if this is an mput(), handle prompt
	 */
	if (multiple) {
		switch(prompt_and_reply("mput",localfile)) {
		case -1:
			return USER_INTERRUPT;
		case 0:
			return 0L;
		}
	} else cprintf("local: %s remote: %s\r\n",localfile,remotefile);

	/*
	 *	make sure the type is correct
	 */
	if (last_type_set != transfer_type) {
		rc = (WORD) ftp_type(transfer_type);
		if (rc != 200)
			return rc;
		message(rc);
	}

	/*
	 *	establish a data connection
	 */
	data = ftp_data_connect();
	if (data < 0)
		return data;

	/*
	 *	tell server we want to store a file
	 */
	sprintf(command,"STOR %s",remotefile);
	rc = send_command(handle,command);

	/*
	 *	see if that's ok
	 */
	if ((rc != 125) && (rc != 150)) {
		TCP_close(data,5,NULL);
		return rc;
	}
	message(rc);

#ifdef STIK1_COMPATIBLE
	if (debug > 1) {
		rc = CNbyte_count(handle);
		if (rc < 0)
			cprintf("===> Unexpected TCP state %d\r\n",rc);
	}
#else
	if (debug > 1) {
		TCPIB tcpinfo;
	
		tcpinfo.request = TCPI_state;
		TCP_info(data,&tcpinfo);
		if (tcpinfo.state != TESTABLISH)
			cprintf("===> Unexpected TCP state %d\r\n",tcpinfo.state);
	}
#endif

	start = clock();		/* start timing */
	prev_bytes = transfer_bytes = 0UL;

	/*
	 *	copy file across network
	 */
	rc2 = Fopen(localfile,0);
	if (rc2 >= 0L) {
		fh = (WORD)rc2;
		while(1) {
			rc2 = Fread(fh,IOBUFSIZE,iobuf);
			if (rc2 < 0L) {
				rc = FILE_READ_ERROR;
				break;
			}
			rc = (WORD)rc2;
			if (rc == 0)
				break;
			remaining = rc;
			for (p = iobuf; (remaining > 0) && (rc >= 0); remaining -= n, p += n) {
				n = min(remaining,TCPBUFSIZE);
				do {
					rc = TCP_send(data,p,n);
					if (constat())
						if (user_break())
							rc = abort_transfer();
				} while (rc == E_OBUFFULL);
				transfer_bytes += n;
				if (tick && (transfer_bytes-prev_bytes > XFER_QUANTUM)) {
					cprintf(FORMAT_XFER_MSG,transfer_bytes);
					prev_bytes = transfer_bytes;
				}
			}
		}
		Fclose(fh);
		if (tick)
			cputs(BLANKOUT_XFER_MSG);
	}

	n = TCP_close(data,5,NULL);

	if (rc == 0)
		rc = n;

	transfer_ticks = clock() - start;

	if (rc == 0)
		rc = get_reply(handle);

	message(rc);	/* print server msg before timing */

	if (verbose && ((rc == 226) || (rc == 250)))
		display_transfer_stats();

	if (bell)
		ring_bell();

	return 0L;
}

LONG ftp_pwd(void)
{
	if (handle < 0)
		return NOT_CONNECTED;

	return send_command(handle,"PWD");
}

LONG ftp_rmdir(char *remotedir)
{
char command[MAXCMDLEN];

	if (handle < 0)
		return NOT_CONNECTED;

	sprintf(command,"RMD %s",remotedir);
	return send_command(handle,command);
}

LONG ftp_rename(char *oldname,char *newname)
{
char command[MAXCMDLEN];
LONG rc;

	if (handle < 0)
		return NOT_CONNECTED;

	sprintf(command,"RNFR %s",oldname);
	rc = send_command(handle,command);
	if (rc != 350)
		return rc;
	message(rc);

	sprintf(command,"RNTO %s",newname);
	return send_command(handle,command);
}

LONG ftp_system(void)
{
	if (handle < 0)
		return NOT_CONNECTED;

	return send_command(handle,"SYST");
}

/*
 *	because certain commands require a specific transfer type, we need
 *	to make sure that we track type carefully.  this is what we do:
 *	 1) [in ftpint.c] run_ascii()/run_binary()/run_type() call ftp_type()
 *		and, if successful, set 'transfer_type'
 *	 2) ftp_type() sends the TYPE command and, if successful, remembers
 *		its value in 'last_type_set'
 *	 3) ftp_dir() sets the type to ascii iff 'last_type_set' is not 'A'
 *	 4) ftp_get()/ftp_put() set the type to 'transfer_type' iff
 *		'last_type_set' is not the same as 'transfer_type'
 */
LONG ftp_type(int type)
{
WORD rc;
char command[MAXCMDLEN];

	if (handle < 0)
		return NOT_CONNECTED;

	if (type < 0) {
		cprintf("Using %s mode to transfer files\r\n",
				(transfer_type=='A')?"ascii":"binary");
		return 0L;
	}

	sprintf(command,"TYPE %c",type);
	rc = send_command(handle,command);

	if (rc >= 0)
		last_type_set = type;

	return rc;
}


/* * * * * * * * * * * * * * * * * * * * * *
 *                                         *
 *   I N T E R N A L   F U N C T I O N S   *
 *                                         *
 * * * * * * * * * * * * * * * * * * * * * */

/*
 *	Resolves server into an IP address, then opens a connection to port
 *	of server.
 *	Returns:	>0	connection handle
 *				<0	error code
 *	Updates 'addr' with resolved address (if resolve() worked :-))
 */
PRIVATE WORD open_connection(char *server,int port,ULONG *addr)
{
WORD rc, handle;
CAB cab;

	*addr = 0UL;
	rc = resolve(server,(char **)NULL,addr,1);
	if (rc < 0)
		return rc;

	cab.rhost = *addr;
	cab.rport = port;

#ifdef STIK1_COMPATIBLE
	handle = TCP_open(cab.rhost,cab.rport,0,TCPBUFSIZE);
#else
	cab.lhost = 0;
	cab.lport = 0;
	handle = TCP_open((uint32)&cab,TCP_ACTIVE,0,TCPBUFSIZE);
#endif
	if (handle < 0)
		return handle;

	rc = TCP_wait_state(handle,TESTABLISH,10);	//STiK-style for now FIXME
	if (rc < 0) {
		TCP_close(handle,5,NULL);
		return rc;
	}

	return handle;
}

/*
 *	Expands reply buffer dynamically
 *	Returns:	pointer to first available place in new buffer
 *				NULL means expansion failed
 */
PRIVATE char *expand_buffer(void)
{
char *new;
long new_size;

	new_size = reply_size + REPLY_SIZE_INCR;
	new = malloc(new_size);
	if (new) {
		new[0] = '\0';
		if (reply) {		/* copy then free old buffer if it exists */
			strcpy(new,reply);
			free(reply);
		}
		reply = NULL;
		reply_size = 0L;
	}

	if (!new)				/* couldn't allocate new buffer */
		return NULL;		/* (the old one remains allocated) */

	reply = new;			/* set new buffer values */
	reply_size = new_size;

	if (debug)
		cprintf("***> reply buffer now %ld bytes\r\n",reply_size);

	return reply + strlen(reply);
}

/*
 *	Gets one line of reply from FTP server
 *	Returns:	<0	error code (standard STinG, or our own)
 *				0	last line
 *				>0	not last line
 */
PRIVATE WORD get_one_line(WORD handle,char **replyptr,long maxlen)
{
WORD rc;
char *p = *replyptr;

	/*
	 *	wait until header is available, then read it
	 */
	while(1) {
		if (CNbyte_count(handle) >= HEADER_LEN)
			break;
		if (constat())
			if (user_break())
				return USER_INTERRUPT;
	}

	rc = CNget_block(handle,header,HEADER_LEN);
	if (rc != HEADER_LEN)
		return (rc < 0) ? rc : INTERNAL_ERROR;
	header[HEADER_LEN] = '\0';

	/*
	 *	copy header into reply[], so user will see it
	 */
	strcpy(p,header);
	p += HEADER_LEN;

	/*
	 *	read until end of line
	 *
	 *	NOTE: if all the reply lines won't fit in reply[],
	 *	we attempt to expand it dynamically
	 */
	while(1) {
		if (maxlen > SHRT_MAX)
			maxlen = SHRT_MAX;
		/*
		 * since we insert a newline ourselves below, we must
		 * leave at least one byte free in the buffer!
		 */
		rc = CNgets(handle,p,maxlen-1,'\n');
		if (rc == E_BIGBUF) {		/* out of room */
			p = expand_buffer();	/* so expand buffer */
			if (p) {
				maxlen = reply + reply_size - p;
				continue;			/* retry CNgets() */
			}
			return rc;				/* buffer expansion failed */
		}
		if (rc != E_NODATA)
			break;
		if (constat())
			if (user_break())
				return USER_INTERRUPT;
	}
	if (rc >= 0) {
		p += rc;
		*p++ = '\n';
		*p = '\0';
	}

	*replyptr = p;

	/* set return code according to header indicator */
	if (rc >= 0)
		rc = (header[HEADER_LEN-1] == ' ') ? 0 : 1;

	return rc;
}

/*
 *	Gets reply from FTP server
 *	Returns:	<0	error (standard STinG, or our own)
 *				else reply code
 *
 *	Handles multiline replies
 */
PRIVATE WORD get_reply(WORD handle)
{
WORD rc;
char *p = reply;

	while(1) {
		rc = get_one_line(handle,&p,reply+reply_size-p);
		if (rc < 0)
			break;
		if (rc == 0) {
			rc = atoi(header);
			break;
		}
	}

	return rc;
}

/*
 *	Sends command to FTP server & gets reply
 *	Returns:	<0	error (standard STinG, or our own)
 *				else reply code
 */
PRIVATE WORD send_command(WORD handle,char *command)
{
WORD n, rc = 0;
char cmd[5], *p;

	if (debug) {
		strncpy(cmd,command,4);
		cmd[4] = '\0';
		if (strequal(cmd,"pass"))
			cprintf("---> %s XXXX\r\n",cmd);
		else cprintf("---> %s\r\n",command);
	}

	strcpy(cmdsave,command);
	n = (WORD)strlen(cmdsave);
	p = cmdsave + n;
	*p++ = '\r';
	*p++ = '\n';
	*p = '\0';

	rc = TCP_send(handle,cmdsave,n+2);
	if (rc < 0)
		return rc;

	return get_reply(handle);
}

/*
 *	generate random port within dynamic range
 */
PRIVATE UWORD generate_port(void)
{
	return (UWORD)(FIRST_DYNAMIC_PORT + (lrand48() % NUM_DYNAMIC_PORTS));
}

/*
 *	this is multi-purpose:
 *	1. if localfile is not NULL
 *		if the file can be opened, output is directed there;
 *		otherwise output goes to the console
 *	2. if localfile *is* NULL
 *		if bufctl is NULL, output goes to the console;
 *		otherwise, output is stored in the buffer specified by
 *		bufctl (and the buffer is automagically expanded)
 */
PRIVATE LONG ftp_directory(char *cmd,char *remotedir,char *localfile,BUFCTL *bufctl)
{
WORD data;		/* data port handle */
LONG fh = -1L;	/* file handle */
WORD n, rc;
char command[MAXCMDLEN];

	if (handle < 0)
		return NOT_CONNECTED;

	/*
	 *	make sure the type is ascii
	 */
	if (last_type_set != 'A') {
		rc = (WORD)ftp_type('A');
		if (rc != 200)
			return rc;
		message(rc);
	}

	/*
	 *	establish a data connection
	 */
	data = ftp_data_connect();
	if (data < 0)
		return data;

	/*
	 *	tell server we want a dir list
	 */
	if (remotedir)
		sprintf(command,"%s %s",cmd,remotedir);
	else strcpy(command,cmd);
	rc = send_command(handle,command);

	/*
	 *	see if that's ok
	 */
	if ((rc != 125) && (rc != 150)) {
		TCP_close(data,5,NULL);
		return rc;
	}
	message(rc);

	/*
	 *	if local file specified for dir copy, open it.
	 *	if open fails, treat as if not specified
	 */
	if (localfile)
		fh = Fcreate(localfile,0);

	/*
	 *	copy dir list across network
	 */
	while(1) {
		if (constat()) {
			rc = ((fh >= 0) || bufctl) ? user_break() : user_input();
			if (rc) {
				rc = abort_transfer();
				break;
			}
		}

		rc = CNgets(data,iobuf,IOBUFSIZE,'\n');
		if (rc == E_NODATA)
			continue;
		if (rc < 0)
			break;

		if (fh >= 0) {
			n = (WORD)strlen(iobuf);
			iobuf[n++] = '\n';
			if (Fwrite(fh,n,iobuf) < 0L) {
				rc = FILE_WRITE_ERROR;
				break;
			}
		} else if (bufctl)
			bufstore(bufctl,iobuf);
		else cprintf("%s\n",iobuf);
	}
	if (rc == E_EOF)
		rc = 0;

	if (fh >= 0L)
		Fclose(fh);

	n = TCP_close(data,5,NULL);
	if (rc == 0)
		rc = n;

	if (rc == 0)
		rc = get_reply(handle);

	return rc;
}

PRIVATE void display_transfer_stats(void)
{
ULONG bps, secs, msecs;

		if (transfer_ticks == 0)		/* avoid divide-by-zero */
			transfer_ticks = 1;

		bps = transfer_bytes * CLOCKS_PER_SEC / transfer_ticks;
		secs = transfer_ticks / CLOCKS_PER_SEC;
		msecs = (transfer_ticks - secs*CLOCKS_PER_SEC) * (1000/CLOCKS_PER_SEC);

		cprintf("%ld bytes in %ld.%03ld secs (%ld bps)\r\n",transfer_bytes,secs,msecs,bps);
}

/*
 *	store name in memory buffer
 */
PRIVATE void bufstore(BUFCTL *bufctl,char *name)
{
long newsize, n;
char *newbuf, *p;

	/* get name length (excluding trailing CR which we remove) */
	n = strlen(name) - 1;

	/*
	 *	see if name will fit in existing buffer
	 *	if not, we allocate a larger one and copy the old one over
	 */
	if ((bufctl->pos+n) >= (bufctl->start+bufctl->size)) {
		newsize = bufctl->size * 2;		/* try to allocate a larger buffer */
		newbuf = malloc(newsize);
		if (!newbuf)					/* failed: silently ignore for now */
			return;
		memcpy(newbuf,bufctl->start,bufctl->size);	/* copy buffer */
		free(bufctl->start);
		bufctl->pos = newbuf + (bufctl->pos - bufctl->start);
		bufctl->start = newbuf;
		bufctl->size = newsize;
	}
	bufctl->count++;		/* update count */
	p = bufctl->pos;
	strncpy(p,name,n);
	p += n;
	*p++ = '\0';
	bufctl->pos = p;		/* and current position */
}

/*
 *	extract host ip addr & port from PASV response
 *	("227 xxxxxxxxxxxxx (h1,h2,h3,h4,p1,p2)")
 *
 *	returns -1 iff error in input data format
 */
PRIVATE int extract_hp(CAB *cab)
{
char *p;
IPADDR h;
UWORD h0, h1, h2, h3, p0, p1;

	/*
	 *	look for start of host/port string
	 */
	for (p = reply; *p; )
		if (*p++ == '(')
			break;
	if (!*p)
		return -1;

	if (sscanf(p,"%hd,%hd,%hd,%hd,%hd,%hd)",&h0,&h1,&h2,&h3,&p0,&p1) != 6)
		return -1;

	h.quad[0] = (UBYTE)h0;
	h.quad[1] = (UBYTE)h1;
	h.quad[2] = (UBYTE)h2;
	h.quad[3] = (UBYTE)h3;
	cab->rhost = h.addr;
	cab->rport = (p0 << 8) | p1;

	return 0;
}

/*
 *	prompt user (used for multifile actions)
 *
 *	returns	-1	stop multifile processing
 *			0	skip this file
 *			1	process this file
 */
PRIVATE int prompt_and_reply(char *cmd,char *file)
{
char c;

	if (prompting) {
		cprintf("%s %s (Y/n)? ",cmd,file);
	    c = conin() & 0xff;
   		cputs("\r\n");
   		switch(c) {
   		case CTL_C:
			cprintf("Continue with %s (Y/n)? ",cmd);
			c = conin() & 0xff;
	   		cputs("\r\n");
   			switch(c) {
   			case CTL_C:
   			case 'N':
   			case 'n':
   				return -1;
   			default:
   				return 0;
   			}
   			break;
   		case 'N':
   		case 'n':
   			return 0;
		}
	}

	return 1;
}

/*
 *	abort file transfer
 */
PRIVATE WORD abort_transfer(void)
{
WORD rc;

	rc = send_command(handle,"ABOR");

	/*
	 * if we abort during transfer, we get a 426 msg to
	 * indicate that abort is pending, followed by a 226
	 * msg to indicate transfer aborted.
	 *
	 * if we abort after transfer has completed, we get
	 * the pending 226 msg for transfer ok, followed by
	 * a 226 message to confirm receipt of the abort.
	 */
	message(rc);				/* 426 or 226 */
	message(get_reply(handle));	/* 226 */

	return USER_INTERRUPT;
}

/*
 *  handle control-C
 */
PRIVATE WORD user_break(void)
{
char c;

    c = conin() & 0xff;
    if (c == CTL_C)         /* user wants to interrupt */
        return -1;

    return 0;
}

/*
 *  check for flow control or control-C
 */
PRIVATE WORD user_input(void)
{
char c;

	c = conin() & 0xff;
	if (c == CTL_C)			/* user wants to interrupt */
		return -1;

	if (c == CTL_S) {		/* user wants to pause */
		while(1) {
			c = conin() & 0xff;
			if (c == CTL_C)
				return -1;
			if (c == CTL_Q)
				break;
		}
	}

	return 0;
}

/*
 *	output a message to the console
 */
void message(LONG rc)
{
char buf[25];
const char *p;

	/*
	 *	test for "no message" return codes
	 */
	if ((rc == 0)
	 || (rc == FTP_EXIT)
	 || (rc == NOMESSAGE_ERROR))
		return;

	/*
	 *	handle server return codes
	 */
	if ((rc >= 100) && (rc <= 599)) {
		if (verbose)
			cputs(reply);
		return;
	}

	/*
	 *	handle all other return codes
	 */
	switch(rc) {
	case UNKNOWN_COMMAND:
		p = "Unknown command";
		break;
	case ARGCOUNT_ERROR:
		p = "Wrong number of arguments";
		break;
	case INTERNAL_ERROR:
		p = "Internal error, please notify author";
		break;
	case NOT_CONNECTED:
		p = "Not connected";
		break;
	case UNKNOWN_TYPE:
		p = "Unknown type";
		break;
	case FILE_READ_ERROR:
		p = "Error reading file";
		break;
	case FILE_WRITE_ERROR:
		p = "Error writing file";
		break;
	case FILE_NOT_FOUND:
		p = "File not found";
		break;
	case INVALID_PATH:
		p = "Invalid path";
		break;
	case MEMORY_ERROR:
		p = "Out of memory";
		break;
	case USER_INTERRUPT:
		p = "Interrupted by user";
		break;
	default:
		if (rc >= -E_LASTERROR) {
			p = get_err_text(rc);
			break;
		}
		sprintf(buf,"Unknown error code %ld",rc);
		p = buf;
		break;
	}
	cprintf("%s\r\n",p);
}
