/*
 * ftputil.c: pftp utility routines
 *
 * Copyright (c) 2013, 2018 Roger Burrows
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See LICENSE.TXT for details.
 */
#include "ftp.h"
#include <stdio.h>
#include <stdarg.h>

char prbuf[300];

/*
 *	input a character
 */
int cgetc(void)
{
int c;

	c = (char)conin();
	conout(c);

	return c;
}

/*
 *	input a string
 */
void cgets(char *buf)
{
char c;

	while(1) {
		c = (char)conin();
		conout(c);
		if (c == '\r')
			break;
		*buf++ = c;
	}
	*buf = '\0';
	conout('\n');
}

/*
 *	input a string with no echo
 */
void cgets_noecho(char *buf)
{
char c;

	while(1) {
		c = (char)conin();
		if (c == '\r')
			break;
		*buf++ = c;
	}
	*buf = '\0';
	conout('\r');
	conout('\n');
}

/*
 *	output a string to the console
 */
void cputs(const char *buf)
{
	while(*buf)
		conout(*buf++);
}

/*
 *	output a formatted string to the console
 */
int cprintf(const char *fmt, ...)
{
va_list args;
int rc;

	va_start(args,fmt);
	rc = vsprintf(prbuf,fmt,args);
	cputs(prbuf);

	return rc;
}

/*
 *	output a 2-byte escape sequence to console
 */
void escape(char c)
{
	conout(ESC);
	conout(c);
}

/*
 *	compare strings for equality, ignoring case
 *
 *	returns 1 iff strings equal
 */
WORD strequal(const char *s1,const char *s2)
{
const char *p, *q;
char c1, c2;

	for (p = s1, q = s2; *p; ) {
		c1 = *p++;
		if ((c1 >= 'A') && (c1 <= 'Z'))
			c1 |= 0x20;
		c2 = *q++;
		if ((c2 >= 'A') && (c2 <= 'Z'))
			c2 |= 0x20;
		if (c1 != c2)
			return 0;
	}
	if (*q)
		return 0;

	return 1;
}

/*
 *  display_date_time - display date/time in format derived from _IDT cookie
 */
void display_date_time(UWORD date,UWORD time)
{
WORD year, month, day, hour, minute, second;
char date_sep, ampm;

	date_sep = idt_value & 0xff;			/* date separator */
	if ((date_sep < 0x20) || (date_sep > 0x7f))
		date_sep = DEFAULT_DT_SEPARATOR;	/* default if all else fails */

	year = 1980 + (date>>9);
	month = (date>>5) & 0x0f;
	day = date & 0x1f;

	switch((idt_value>>8)&0x03) {
	case _IDT_MDY:
		cprintf("%02d%c%02d%c%04d  ",month,date_sep,day,date_sep,year);
		break;
	case _IDT_DMY:
		cprintf("%02d%c%02d%c%04d  ",day,date_sep,month,date_sep,year);
		break;
	case _IDT_YDM:
		cprintf("%04d%c%02d%c%02d  ",year,date_sep,day,date_sep,month);
		break;
	default:						/* i.e. _IDT_YMD or some kind of bug ... */
		cprintf("%04d%c%02d%c%02d  ",year,date_sep,month,date_sep,day);
		break;
	}

	hour = time >> 11;
	minute = (time>>5) & 0x3f;
	second = (time&0x1f) << 1;

	switch((idt_value>>12)&0x01) {
	case _IDT_12H:
		if (hour < 12)				/* figure out am/pm */
			ampm = 'a';
		else ampm = 'p';
		if (hour > 12)				/* figure out noon/midnight */
			hour -= 12;
		else if (hour == 0)
			hour = 12;
		cprintf("%02d:%02d:%02d%cm",hour,minute,second,ampm);
		break;
	default:						/* i.e. _IDT_24H or some kind of bug ... */
		cprintf("%02d:%02d:%02d  ",hour,minute,second);
		break;
	}
}

/*
 *	get_basename - return pointer to first byte of 'basename',
 *	i.e. bypass any path prefix present
 */
char *get_basename(char *fullname)
{
char *p, *start;

	for (p = start = fullname; *p; p++)
		if (*p == '\\')
			start = p+1;

	return start;
}
