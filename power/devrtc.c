#include	"u.h"
#include	"lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"errno.h"
#include	"devtab.h"

#include	"io.h"

typedef struct Rtc	Rtc;

enum {
	Nbcd = 8	/* number of bcd bytes in the clock */
};

struct Rtc
{
	int	sec;
	int	min;
	int	hour;
	int	mday;
	int	mon;
	int	year;
};

QLock rtclock;	/* mutex on clock operations */

static Dirtab rtcdir[]={
	"rtc",		{1, 0},	0,	0600,
};

static uchar pattern[] =
{
	0xc5, 0x3a, 0xa3, 0x5c, 0xc5, 0x3a, 0xa3, 0x5c
};

ulong rtc2sec(Rtc*);
void sec2rtc(ulong, Rtc*);
int *yrsize(int);

/*
 *  issue pattern recognition bits to nv ram to address the
 *  real time clock
 */
rtcpattern(void)
{
	uchar *nv;
	uchar ch;
	int i, j;

	nv = RTC;

	/*
	 *  read the pattern sequence pointer to reset it
	 */
	ch = *nv;

	/*
	 *  stuff the pattern recognition codes one bit at
	 *  a time into *nv.
	 */
	for(i = 0; i < Nbcd; i++){
		ch = pattern[i];
		for (j = 0; j < 8; j++){
			*nv = ch & 0x1;
			ch >>= 1;
		}
	}
}

void
rtcreset(void)
{
}

void
rtcinit(void)
{
}

Chan*
rtcattach(char *spec)
{
	return devattach('r', spec);
}

Chan*
rtcclone(Chan *c, Chan *nc)
{
	return devclone(c, nc);
}

int	 
rtcwalk(Chan *c, char *name)
{
	if(c->qid.path != CHDIR)
		return 0;
	if(strcmp(name, "rtc") == 0){
		c->qid.path = 1;
		return 1;
	}
	return 0;
}

void	 
rtcstat(Chan *c, char *dp)
{
	devstat(c, dp, rtcdir, 1, devgen);
}

Chan*
rtcopen(Chan *c, int omode)
{
	if(c->qid.path==1 && (omode&(OWRITE|OTRUNC))){
		if(strcmp(u->p->pgrp->user, "bootes"))	/* BUG */
			error(Eperm);
	}
	c->mode = openmode(omode);
	c->flag |= COPEN;
	c->offset = 0;
	return c;
}

void	 
rtccreate(Chan *c, char *name, int omode, ulong perm)
{
	error(Eperm);
}

void	 
rtcclose(Chan *c)
{
}

#define GETBCD(o) ((bcdclock[o]&0xf) + 10*(bcdclock[o]>>4))

long	 
rtcread(Chan *c, void *buf, long n)
{
	int i,j;
	uchar ch;
	uchar *nv;
	uchar bcdclock[Nbcd];
	char atime[64];
	Rtc rtc;

	if(c->qid.path & CHDIR)
		return devdirread(c, buf, n, rtcdir, 1, devgen);

	nv = RTC;

	/*
	 *  set up the pattern for the clock
	 */
	qlock(&rtclock);
	rtcpattern();

	/*
	 *  read out the clock one bit at a time
	 */
	for (i = 0; i < Nbcd; i++){
		ch = 0;
		for (j = 0; j < 8; j++)
			ch |= ((*nv & 0x1) << j);
		bcdclock[i] = ch;
	}
	qunlock(&rtclock);

	/*
	 *  see if the clock oscillator is on
	 */
	if(bcdclock[4] & 0x20)
		return 0;		/* nope, time is bogus */

	/*
	 *  convert from BCD
	 */
	rtc.sec = GETBCD(1);
	rtc.min = GETBCD(2);
	rtc.hour = GETBCD(3);
	rtc.mday = GETBCD(5);
	rtc.mon = GETBCD(6);
	rtc.year = GETBCD(7);

	/*
	 *  the world starts jan 1 1970
	 */
	if(rtc.year < 70)
		rtc.year += 2000;
	else
		rtc.year += 1900;

	return readnum(c->offset, buf, n, rtc2sec(&rtc), 12);
}

#define PUTBCD(n,o) bcdclock[o] = (n % 10) | (((n / 10) % 10)<<4)

long	 
rtcwrite(Chan *c, void *buf, long n)
{
	Rtc rtc;
	ulong secs;
	int i,j;
	uchar ch;
	uchar bcdclock[Nbcd];
	uchar *nv;
	char *cp, *ep;

	if(c->offset!=0)
		error(Ebadarg);

	/*
	 *  read the time
	 */
	cp = ep = buf;
	ep += n;
	while(cp < ep){
		if(*cp>='0' && *cp<='9')
			break;
		cp++;
	}
	secs = strtoul(cp, 0, 0);

	/*
	 *  convert to bcd
	 */
	sec2rtc(secs, &rtc);
	bcdclock[0] = bcdclock[4] = 0;
	PUTBCD(rtc.sec, 1);
	PUTBCD(rtc.min, 2);
	PUTBCD(rtc.hour, 3);
	PUTBCD(rtc.mday, 5);
	PUTBCD(rtc.mon, 6);
	PUTBCD(rtc.year, 7);

	/*
	 *  set up the pattern for the clock
	 */
	qlock(&rtclock);
	rtcpattern();

	/*
	 *  write the clock one bit at a time
	 */
	nv = RTC;
	for (i = 0; i < Nbcd; i++){
		ch = bcdclock[i];
		for (j = 0; j < 8; j++){
			*nv = ch & 1;
			ch >>= 1;
		}
	}
	qunlock(&rtclock);

	return n;
}

void	 
rtcremove(Chan *c)
{
	error(Eperm);
}

void	 
rtcwstat(Chan *c, char *dp)
{
	error(Eperm);
}

#define SEC2MIN 60L
#define SEC2HOUR (60L*SEC2MIN)
#define SEC2DAY (24L*SEC2HOUR)

/*
 *  days per month plus days/year
 */
static	int	dmsize[] =
{
	365, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};
static	int	ldmsize[] =
{
	366, 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

/*
 *  return the days/month for the given year
 */
int *
yrsize(int yr)
{
	if((yr % 4) == 0)
		return ldmsize;
	else
		return dmsize;
}

/*
 *  compute seconds since Jan 1 1970
 */
ulong
rtc2sec(Rtc *rtc)
{
	ulong secs;
	int i;
	int *d2m;

	secs = 0;

	/*
	 *  seconds per year
	 */
	for(i = 1970; i < rtc->year; i++){
		d2m = yrsize(i);
		secs += d2m[0] * SEC2DAY;
	}

	/*
	 *  seconds per month
	 */
	d2m = yrsize(rtc->year);
	for(i = 1; i < rtc->mon; i++)
		secs += d2m[i] * SEC2DAY;

	secs += (rtc->mday-1) * SEC2DAY;
	secs += rtc->hour * SEC2HOUR;
	secs += rtc->min * SEC2MIN;
	secs += rtc->sec;

	return secs;
}

/*
 *  compute rtc from seconds since Jan 1 1970
 */
void
sec2rtc(ulong secs, Rtc *rtc)
{
	int d;
	long hms, day;
	int *d2m;

	/*
	 * break initial number into days
	 */
	hms = secs % SEC2DAY;
	day = secs / SEC2DAY;
	if(hms < 0) {
		hms += SEC2DAY;
		day -= 1;
	}

	/*
	 * generate hours:minutes:seconds
	 */
	rtc->sec = hms % 60;
	d = hms / 60;
	rtc->min = d % 60;
	d /= 60;
	rtc->hour = d;

	/*
	 * year number
	 */
	if(day >= 0)
		for(d = 1970; day >= *yrsize(d); d++)
			day -= *yrsize(d);
	else
		for (d = 1970; day < 0; d--)
			day += *yrsize(d-1);
	rtc->year = d;

	/*
	 * generate month
	 */
	d2m = yrsize(rtc->year);
	for(d = 1; day >= d2m[d]; d++)
		day -= d2m[d];
	rtc->mday = day + 1;
	rtc->mon = d;

	return;
}
