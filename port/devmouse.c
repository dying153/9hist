#include	"u.h"
#include	"../port/lib.h"
#include	<libg.h>
#include	<gnot.h>
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

#include	"devtab.h"
#include	"screen.h"

/*
 * Some monochrome screens are reversed from what we like:
 * We want 0's bright and 1's dark.
 * Indexed by an Fcode, these compensate for the source bitmap being wrong
 * (exchange S rows) and destination (exchange D columns and invert result)
 */
int flipS[] = {
	0x0, 0x4, 0x8, 0xC, 0x1, 0x5, 0x9, 0xD,
	0x2, 0x6, 0xA, 0xE, 0x3, 0x7, 0xB, 0xF
};

int flipD[] = {
	0xF, 0xD, 0xE, 0xC, 0x7, 0x5, 0x6, 0x4,
	0xB, 0x9, 0xA, 0x8, 0x3, 0x1, 0x2, 0x0, 
};

int flipping;	/* are flip tables being used to transform Fcodes? */

typedef struct Mouseinfo	Mouseinfo;
typedef struct Cursorinfo	Cursorinfo;

struct Mouseinfo
{
	/*
	 * First three fields are known in some l.s's
	 */
	int	dx;
	int	dy;
	int	track;		/* l.s has updated dx & dy */
	Mouse;
	int	redraw;		/* update cursor on screen */
	ulong	counter;	/* increments every update */
	ulong	lastcounter;	/* value when /dev/mouse read */
	Rendez	r;
	Ref;
	QLock;
	int	open;
};

struct Cursorinfo
{
	Cursor;
	Lock;
	int	visible;	/* on screen */
	int	disable;	/* from being used */
	int	frozen;	/* from being used */
	Rectangle r;		/* location */
};

Mouseinfo	mouse;
Cursorinfo	cursor;
int		mouseshifted;
int		mousetype;
int		mouseswap;
int		hwcurs;
Cursor	curs;

Cursor	arrow =
{
	{-1, -1},
	{0xFF, 0xE0, 0xFF, 0xE0, 0xFF, 0xC0, 0xFF, 0x00,
	 0xFF, 0x00, 0xFF, 0x80, 0xFF, 0xC0, 0xFF, 0xE0,
	 0xE7, 0xF0, 0xE3, 0xF8, 0xC1, 0xFC, 0x00, 0xFE,
	 0x00, 0x7F, 0x00, 0x3E, 0x00, 0x1C, 0x00, 0x08,
	},
	{0x00, 0x00, 0x7F, 0xC0, 0x7F, 0x00, 0x7C, 0x00,
	 0x7E, 0x00, 0x7F, 0x00, 0x6F, 0x80, 0x67, 0xC0,
	 0x43, 0xE0, 0x41, 0xF0, 0x00, 0xF8, 0x00, 0x7C,
	 0x00, 0x3E, 0x00, 0x1C, 0x00, 0x08, 0x00, 0x00,
	}
};

ulong setbits[16];
GBitmap	set =
{
	setbits,
	0,
	1,
	0,
	{0, 0, 16, 16},
	{0, 0, 16, 16}
};

ulong clrbits[16];
GBitmap	clr =
{
	clrbits,
	0,
	1,
	0,
	{0, 0, 16, 16},
	{0, 0, 16, 16}
};

ulong cursorbackbits[16*4];
GBitmap cursorback =
{
	cursorbackbits,
	0,
	1,
	0,
	{0, 0, 16, 16},
	{0, 0, 16, 16}
};

ulong cursorworkbits[16*4];
GBitmap cursorwork =
{
	cursorworkbits,
	0,
	1,
	0,
	{0, 0, 16, 16},
	{0, 0, 16, 16}
};

void	Cursortocursor(Cursor*);
int	mousechanged(void*);

extern	void	screenload(Rectangle, uchar*, int, int, int);
extern	void	screenunload(Rectangle, uchar*, int, int, int);

enum{
	Qdir,
	Qcursor,
	Qmouse,
	Qmousectl,
};

Dirtab mousedir[]={
	"cursor",	{Qcursor},	0,			0666,
	"mouse",	{Qmouse},	0,			0666,
	"mousectl",	{Qmousectl},	0,			0220,
};

#define	NMOUSE	(sizeof(mousedir)/sizeof(Dirtab))

extern	GBitmap	gscreen;

void
mousereset(void)
{
	ulong r;

	if(!conf.monitor)
		return;

	getcolor(0, &r, &r, &r);
	if(r == 0)
		flipping = 1;
	flipping = 0;	/* howard, why is this necessary to get a black arrow on carrera? */
	curs = arrow;
	Cursortocursor(&arrow);
}

void
mouseinit(void)
{
	if(!conf.monitor)
		return;
	if(gscreen.ldepth > 3){
		cursorback.ldepth = 0;
		cursorwork.ldepth = 0;
	}else{
		cursorback.ldepth = gscreen.ldepth;
		cursorback.width = ((16 << gscreen.ldepth) + 31) >> 5;
		cursorwork.ldepth = gscreen.ldepth;
		cursorwork.width = ((16 << gscreen.ldepth) + 31) >> 5;
	}
	cursoron(1);
}

Chan*
mouseattach(char *spec)
{
	if(!conf.monitor)
		error(Egreg);
	return devattach('m', spec);
}

Chan*
mouseclone(Chan *c, Chan *nc)
{
	nc = devclone(c, nc);
	if(c->qid.path != CHDIR)
		incref(&mouse);
	return nc;
}

int
mousewalk(Chan *c, char *name)
{
	return devwalk(c, name, mousedir, NMOUSE, devgen);
}

void
mousestat(Chan *c, char *db)
{
	devstat(c, db, mousedir, NMOUSE, devgen);
}

Chan*
mouseopen(Chan *c, int omode)
{
	switch(c->qid.path){
	case CHDIR:
		if(omode != OREAD)
			error(Eperm);
		break;
	case Qmouse:
		lock(&mouse);
		if(mouse.open){
			unlock(&mouse);
			error(Einuse);
		}
		mouse.open = 1;
		mouse.ref++;
		unlock(&mouse);
		break;
	default:
		incref(&mouse);
	}
	c->mode = openmode(omode);
	c->flag |= COPEN;
	c->offset = 0;
	return c;
}

void
mousecreate(Chan *c, char *name, int omode, ulong perm)
{
	if(!conf.monitor)
		error(Egreg);
	USED(c, name, omode, perm);
	error(Eperm);
}

void
mouseremove(Chan *c)
{
	USED(c);
	error(Eperm);
}

void
mousewstat(Chan *c, char *db)
{
	USED(c, db);
	error(Eperm);
}

void
mouseclose(Chan *c)
{
	if(c->qid.path!=CHDIR && (c->flag&COPEN)){
		lock(&mouse);
		if(c->qid.path == Qmouse)
			mouse.open = 0;
		if(--mouse.ref == 0){
			cursoroff(1);
			curs = arrow;
			Cursortocursor(&arrow);
			cursoron(1);
		}
		unlock(&mouse);
	}
}


long
mouseread(Chan *c, void *va, long n, ulong offset)
{
	char buf[4*12+1];
	uchar *p;
	static int map[8] = {0, 4, 2, 6, 1, 5, 3, 7 };

	p = va;
	switch(c->qid.path){
	case CHDIR:
		return devdirread(c, va, n, mousedir, NMOUSE, devgen);

	case Qcursor:
		if(offset != 0)
			return 0;
		if(n < 2*4+2*2*16)
			error(Eshort);
		n = 2*4+2*2*16;
		lock(&cursor);
		BPLONG(p+0, curs.offset.x);
		BPLONG(p+4, curs.offset.y);
		memmove(p+8, curs.clr, 2*16);
		memmove(p+40, curs.set, 2*16);
		unlock(&cursor);
		return n;

	case Qmouse:
		while(mousechanged(0) == 0)
			sleep(&mouse.r, mousechanged, 0);
		lock(&cursor);
		sprint(buf, "%11d %11d %11d %11d",
			mouse.xy.x, mouse.xy.y,
			mouseswap ? map[mouse.buttons&7] : mouse.buttons,
			TK2MS(MACHP(0)->ticks));
		mouse.lastcounter = mouse.counter;
		unlock(&cursor);
		if(n > 4*12)
			n = 4*12;
		memmove(va, buf, n);
		return n;
	}
	return 0;
}

long
mousewrite(Chan *c, void *va, long n, ulong offset)
{
	char *p;
	Point pt;
	char buf[64];

	USED(offset);
	p = va;
	switch(c->qid.path){
	case CHDIR:
		error(Eisdir);

	case Qcursor:
		cursoroff(1);
		if(n < 2*4+2*2*16){
			curs = arrow;
			Cursortocursor(&arrow);
		}else{
			n = 2*4+2*2*16;
			curs.offset.x = BGLONG(p+0);
			curs.offset.y = BGLONG(p+4);
			memmove(curs.clr, p+8, 2*16);
			memmove(curs.set, p+40, 2*16);
			Cursortocursor(&curs);
		}
		qlock(&mouse);
		mouse.redraw = 1;
		mouseclock();
		qunlock(&mouse);
		cursoron(1);
		return n;

	case Qmousectl:
		if(n >= sizeof(buf))
			n = sizeof(buf)-1;
		strncpy(buf, va, n);
		buf[n] = 0;
		mousectl(buf);
		return n;

	case Qmouse:
		if(n > sizeof buf-1)
			n = sizeof buf -1;
		memmove(buf, va, n);
		buf[n] = 0;
		p = 0;
		pt.x = strtoul(buf, &p, 0);
		if(p == 0)
			error(Eshort);
		pt.y = strtoul(p, 0, 0);
		qlock(&mouse);
		if(ptinrect(pt, gscreen.r)){
			mouse.xy = pt;
			mouse.redraw = 1;
			mouse.track = 1;
			mouseclock();
		}
		qunlock(&mouse);
		return n;
	}

	error(Egreg);
	return -1;
}

void
Cursortocursor(Cursor *c)
{
	int i;
	uchar *p;

	lock(&cursor);
	memmove(&cursor, c, sizeof(Cursor));
	for(i=0; i<16; i++){
		p = (uchar*)&setbits[i];
		*p = c->set[2*i];
		*(p+1) = c->set[2*i+1];
		p = (uchar*)&clrbits[i];
		*p = c->clr[2*i];
		*(p+1) = c->clr[2*i+1];
	}
	if(hwcurs)
		hwcursset(set.base, clr.base, cursor.offset.x, cursor.offset.y);
	unlock(&cursor);
}

void
cursorlock(Rectangle r)
{
	if(hwcurs)
		return;
	lock(&cursor);
	if(rectXrect(cursor.r, r)){
		cursoroff(0);
		cursor.frozen = 1;
	}
	cursor.disable++;
	unlock(&cursor);
}

void
cursorunlock(void)
{
	if(hwcurs)
		return;
	lock(&cursor);
	cursor.disable--;
	if(cursor.frozen)
		cursoron(0);
	cursor.frozen = 0;
	unlock(&cursor);
}

void
cursoron(int dolock)
{
	if(cursor.disable)
		return;
	if(dolock)
		lock(&cursor);
	if(cursor.visible++ == 0){
		if(hwcurs)
			hwcursmove(mouse.xy.x, mouse.xy.y);
		else {
			cursor.r.min = mouse.xy;
			cursor.r.max = add(mouse.xy, Pt(16, 16));
			cursor.r = raddp(cursor.r, cursor.offset);
			screenunload(cursor.r, (uchar*)cursorworkbits,
				(16>>3) << gscreen.ldepth, cursorwork.width*BY2WD, 0);
			memmove(cursorbackbits, cursorworkbits, 16*cursorback.width*BY2WD);
			gbitblt(&cursorwork, cursorwork.r.min,
				&clr, Rect(0, 0, 16, 16), flipping? flipD[D&~S] : D&~S);
			gbitblt(&cursorwork, cursorwork.r.min,
				&set, Rect(0, 0, 16, 16), flipping? flipD[S|D] : S|D);
			screenload(cursor.r, (uchar*)cursorworkbits,
				(16>>3) << gscreen.ldepth, cursorwork.width*BY2WD, 0);
		}
	}
	if(dolock)
		unlock(&cursor);
}

void
cursoroff(int dolock)
{
	if(cursor.disable)
		return;
	if(dolock)
		lock(&cursor);
	if(--cursor.visible == 0) {
		if(!hwcurs)
			screenload(cursor.r, (uchar*)cursorbackbits,
				(16>>3) << gscreen.ldepth, cursorback.width*BY2WD, 0);
	}
	if(dolock)
		unlock(&cursor);
}

/*
 *  called by the clock routine to redraw the cursor
 */
void
mouseclock(void)
{
	if(mouse.track){
		mousetrack(mouse.buttons, mouse.dx, mouse.dy);
		mouse.track = 0;
		mouse.dx = 0;
		mouse.dy = 0;
	}
	if(mouse.redraw && canlock(&cursor)){
		mouse.redraw = 0;
		cursoroff(0);
		cursoron(0);
		unlock(&cursor);
	}
}

/*
 *  called at interrupt level to update the structure and
 *  awaken any waiting procs.
 */
void
mousetrack(int b, int dx, int dy)
{
	int x, y;

	x = mouse.xy.x + dx;
	if(x < gscreen.r.min.x)
		x = gscreen.r.min.x;
	if(x >= gscreen.r.max.x)
		x = gscreen.r.max.x;
	y = mouse.xy.y + dy;
	if(y < gscreen.r.min.y)
		y = gscreen.r.min.y;
	if(y >= gscreen.r.max.y)
		y = gscreen.r.max.y;
	mouse.counter++;
	mouse.xy = Pt(x, y);
	mouse.buttons = b;
	mouse.redraw = 1;
	wakeup(&mouse.r);
}

/*
 *  microsoft 3 button, 7 bit bytes
 *
 *	byte 0 -	1  L  R Y7 Y6 X7 X6
 *	byte 1 -	0 X5 X4 X3 X2 X1 X0
 *	byte 2 -	0 Y5 Y4 Y3 Y2 Y1 Y0
 *	byte 3 -	0  M  x  x  x  x  x	(optional)
 *
 *  shift & right button is the same as middle button (for 2 button mice)
 */
int
m3mouseputc(int c)
{
	static uchar msg[3];
	static int nb;
	static int middle;
	static uchar b[] = { 0, 4, 1, 5, 0, 2, 1, 5 };
	short x;
	int dx, dy, newbuttons;

	/* 
	 *  check bit 6 for consistency
	 */
	if(nb==0){
		if((c&0x40) == 0){
			/* an extra byte gets sent for the middle button */
			middle = (c&0x20) ? 2 : 0;
			newbuttons = (mouse.buttons & ~2) | middle;
			mousetrack(newbuttons, 0, 0);
			return 0;
		}
	}
	msg[nb] = c;
	if(++nb == 3){
		nb = 0;
		newbuttons = middle | b[(msg[0]>>4)&3 | (mouseshifted ? 4 : 0)];
		x = (msg[0]&0x3)<<14;
		dx = (x>>8) | msg[1];
		x = (msg[0]&0xc)<<12;
		dy = (x>>8) | msg[2];
		mousetrack(newbuttons, dx, dy);
	}
	return 0;
}

/*
 *  Logitech 5 byte packed binary mouse format, 8 bit bytes
 *
 *  shift & right button is the same as middle button (for 2 button mice)
 */
int
mouseputc(int c)
{
	static short msg[5];
	static int nb;
	static uchar b[] = {0, 4, 2, 6, 1, 5, 3, 7, 0, 2, 2, 6, 1, 5, 3, 7};
	int dx, dy, newbuttons;

	if((c&0xF0) == 0x80)
		nb=0;
	msg[nb] = c;
	if(c & 0x80)
		msg[nb] |= ~0xFF;	/* sign extend */
	if(++nb == 5){
		newbuttons = b[((msg[0]&7)^7) | (mouseshifted ? 8 : 0)];
		dx = msg[1]+msg[3];
		dy = -(msg[2]+msg[4]);
		mousetrack(newbuttons, dx, dy);
		nb = 0;
	}
	return 0;
}

int
mousechanged(void *m)
{
	USED(m);
	return mouse.lastcounter - mouse.counter;
}
