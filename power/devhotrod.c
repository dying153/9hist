#include	"u.h"
#include	"lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"errno.h"
#include	"devtab.h"
#include	"fcall.h"

#include	"io.h"

typedef struct Hotrod	Hotrod;
typedef struct HotQ	HotQ;
typedef struct Device	Device;

enum {
	Vmevec=		0xd2,		/* vme vector for interrupts */
	Intlevel=	5,		/* level to interrupt on */
	Qdir=		0,		/* Qid's */
	Qhotrod=	1,
	NhotQ=		10,		/* size of communication queues */
	Nhotrod=	1,
};

/*
 *  The hotrod fiber interface responds to 1MB
 *  of either user or supervisor accesses at:
 *  	0x30000000 to 0x300FFFFF  in	A32 space
 *  and	  0xB00000 to   0xBFFFFF  in	A24 space.
 *  The second 0x40000 of this space is on-board SRAM.
 */
struct Device {
	ulong	mem[0x100000/sizeof(ulong)];
};
#define HOTROD		VMEA24SUP(Device, 0xB00000)

struct HotQ{
	ulong	i;			/* index into queue */
	Hotmsg	*msg[NhotQ];		/* pointer to command buffer */
	ulong	pad[3];			/* unused; for hotrod prints */
};


struct Hotrod{
	QLock;
	QLock		buflock;
	Lock		busy;
	Device		*addr;		/* address of the device */
	int		vec;		/* vme interrupt vector */
	HotQ		*wq;		/* write this queue to send cmds */
	int		wi;		/* where to write next cmd */
	HotQ		rq;		/* read this queue to receive replies */
	int		ri;		/* where to read next response */
	Rendez		r;
	uchar		buf[MAXFDATA+100];
};

Hotrod hotrod[Nhotrod];

void	hotrodintr(int);

/*
 * Commands
 */
enum{
	RESET=	0,	/* params: Q address, length of queue */
	REBOOT=	1,	/* params: none */
	READ=	2,	/* params: buffer, count, returned count */
	WRITE=	3,	/* params: buffer, count */
};

void
hotsend(Hotrod *h, Hotmsg *m)
{
print("hotsend send %d %lux %lux\n", m->cmd, m, m->param[0]);
	h->wq->msg[h->wi] = (Hotmsg*)MP2VME(m);
	while(h->wq->msg[h->wi])
		;
print("hotsend done\n");
	h->wi++;
	if(h->wi >= NhotQ)
		h->wi = 0;
}

/*
 *  reset all hotrod boards
 */
void
hotrodreset(void)
{
	int i;
	Hotrod *hp;

	for(hp=hotrod,i=0; i<Nhotrod; i++,hp++){
		hp->addr = HOTROD+i;
		/*
		 * Write queue is at end of hotrod memory
		 */
		hp->wq = (HotQ*)((ulong)hp->addr+2*0x40000-sizeof(HotQ));
		hp->vec = Vmevec+i;
		setvmevec(hp->vec, hotrodintr);
	}	
	wbflush();
	delay(20);
}

void
hotrodinit(void)
{
}

/*
 *  enable the device for interrupts, spec is the device number
 */
Chan*
hotrodattach(char *spec)
{
	Hotrod *hp;
	int i;
	Chan *c;

	i = strtoul(spec, 0, 0);
	if(i >= Nhotrod)
		error(Ebadarg);

	c = devattach('H', spec);
	c->dev = i;
	c->qid.path = CHDIR;
	c->qid.vers = 0;
	return c;
}

Chan*
hotrodclone(Chan *c, Chan *nc)
{
	return devclone(c, nc);
}

int	 
hotrodwalk(Chan *c, char *name)
{
	if(c->qid.path != CHDIR)
		return 0;
	if(strncmp(name, "hotrod", 6) == 0){
		c->qid.path = Qhotrod;
		return 1;
	}
	return 0;
}

void	 
hotrodstat(Chan *c, char *dp)
{
	print("hotrodstat\n");
	error(Egreg);
}

Chan*
hotrodopen(Chan *c, int omode)
{
	Device *dp;
	Hotrod *hp;
	Hotmsg *mp;

	if(c->qid.path == CHDIR){
		if(omode != OREAD)
			error(Eperm);
	}else if(c->qid.path == Qhotrod){
		hp = &hotrod[c->dev];
		if(!canlock(&hp->busy))
			error(Einuse);
		/*
		 * Clear communications region
		 */
		memset(hp->wq->msg, 0, sizeof(hp->wq->msg));
		hp->wq->i = 0;

		/*
		 * Issue reset
		 */
		hp->wi = 0;
		hp->ri = 0;
		mp = &u->khot;
		mp->cmd = RESET;
		mp->param[0] = MP2VME(&hp->rq);
		mp->param[1] = NhotQ;
		hotsend(hp, &((User*)(u->p->upage->pa|KZERO))->khot);
	}
	c->mode = openmode(omode);
	c->flag |= COPEN;
	c->offset = 0;
	return c;
}

void	 
hotrodcreate(Chan *c, char *name, int omode, ulong perm)
{
	error(Eperm);
}

void	 
hotrodclose(Chan *c)
{
	Hotrod *hp;

	hp = &hotrod[c->dev];
	if(c->qid.path != CHDIR){
		u->khot.cmd = REBOOT;
		hotsend(hp, &((User*)(u->p->upage->pa|KZERO))->khot);
		unlock(&hp->busy);
	}
}

/*
 * Read and write use physical addresses if they can, which they usually can.
 * Most I/O is from devmnt, which has local buffers.  Therefore just check
 * that buf is in KSEG0 and is at an even address.
 */

long	 
hotrodread(Chan *c, void *buf, long n)
{
	Hotrod *hp;
	Hotmsg *mp;

	hp = &hotrod[c->dev];
	switch(c->qid.path){
	case Qhotrod:
		if(n > sizeof hp->buf)
			error(Egreg);
		if((((ulong)buf)&(KSEGM|3)) == KSEG0){
			/*
			 *  use supplied buffer, no need to lock for reply
			 */
			mp = &u->khot;
			mp->param[2] = 0;	/* reply count */
			qlock(hp);
			mp->cmd = READ;
			mp->param[0] = MP2VME(buf);
			mp->param[1] = n;
			hotsend(hp, &((User*)(u->p->upage->pa|KZERO))->khot);
			qunlock(hp);
			do
				n = mp->param[2];
			while(n == 0);
		}else{
			/*
			 *  use hotrod buffer.  lock the buffer till the reply
			 */
			mp = &u->uhot;
			mp->param[2] = 0;	/* reply count */
			qlock(&hp->buflock);
			qlock(hp);
			mp->cmd = READ;
			mp->param[0] = MP2VME(hp->buf);
			mp->param[1] = n;
			hotsend(hp, &((User*)(u->p->upage->pa|KZERO))->uhot);
			qunlock(hp);
			do
				n = mp->param[2];
			while(n == 0);
			memcpy(buf, hp->buf, n);
			qunlock(&hp->buflock);
		}
		return n;
	}
	error(Egreg);
	return 0;
}

/*
 *  write hotrod memory
 */
long	 
hotrodwrite(Chan *c, void *buf, long n)
{
	Hotrod *hp;
	Hotmsg *mp;

	hp = &hotrod[c->dev];
	switch(c->qid.path){
	case 1:
		if(n > sizeof hp->buf)
			error(Egreg);
		if((((ulong)buf)&(KSEGM|3)) == KSEG0){
			/*
			 *  use supplied buffer, no need to lock for reply
			 */
			mp = &u->khot;
			qlock(hp);
			mp->cmd = WRITE;
			mp->param[0] = MP2VME(buf);
			mp->param[1] = n;
			hotsend(hp, &((User*)(u->p->upage->pa|KZERO))->khot);
			qunlock(hp);
		}else{
			/*
			 *  use hotrod buffer.  lock the buffer till the reply
			 */
			mp = &u->uhot;
			qlock(&hp->buflock);
			qlock(hp);
			memcpy(hp->buf, buf, n);
			mp->cmd = WRITE;
			mp->param[0] = MP2VME(hp->buf);
			mp->param[1] = n;
			hotsend(hp, &((User*)(u->p->upage->pa|KZERO))->uhot);
			qunlock(hp);
			qunlock(&hp->buflock);
		}
		return n;
	}
	error(Egreg);
	return 0;
}

void	 
hotrodremove(Chan *c)
{
	error(Eperm);
}

void	 
hotrodwstat(Chan *c, char *dp)
{
	error(Eperm);
}

void
hotrodintr(int vec)
{
	Hotrod *hp;

	print("hotrod%d interrupt\n", vec - Vmevec);
	hp = &hotrod[vec - Vmevec];
	if(hp < hotrod || hp > &hotrod[Nhotrod]){
		print("bad hotrod vec\n");
		return;
	}
}
