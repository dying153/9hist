#include	"u.h"
#include	"lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"

#include	"ureg.h"

void
delay(int ms)
{
	ulong t, *p;
	int i;

	ms *= 2000;	/* experimentally determined */
	for(i=0; i<ms; i++)
		;
}

typedef struct Ctr Ctr;
struct Ctr
{
	ulong	ctr0;
	ulong	lim0;
	ulong	ctr1;
	ulong	lim1;
};
Ctr	*ctr;

void
clockinit(void)
{
	KMap *k;

	k = kmappa(CLOCK, PTENOCACHE|PTEIO);
	ctr = (Ctr*)k->va;
	ctr->lim1 = (CLOCKFREQ/HZ)<<10;
}

void
clock(Ureg *ur)
{
	Proc *p;
	ulong i, ss, nrun = 0;
	Segment *s;

	i = ctr->lim1;	/* clear interrupt */
	USED(i);
	m->ticks++;
	p = m->proc;
	if(p){
		nrun = 1;
		p->pc = ur->pc;
		if (p->state==Running)
			p->time[p->insyscall]++;
	}
	nrun = (nrdy+nrun)*1000;
	MACHP(0)->load = (MACHP(0)->load*19+nrun)/20;

	checkalarms();
	kbdclock();
	mouseclock();
	sccclock();
	kproftimer(ur->pc);
	if((ur->psr&SPL(0xF))==0 && p && p->state==Running){
		if(anyready()){
			if(p->hasspin)
				p->hasspin = 0;
			else
				sched();
		}
		if((ur->psr&PSRPSUPER) == 0){
			ss = spllo();				/* Low because we may fault */
			*(ulong*)(USTKTOP-BY2WD) += TK2MS(1);
			notify(ur);
			splx(ss);				/* return hi for restore */
		}
	}
}
