#include	"u.h"
#include	"lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"

/*
 * Called splhi, not in Running state
 */
void
mapstack(Proc *p)
{
	short tp;
	ulong tlbvirt, tlbphys;

	tp = p->pidonmach[m->machno];
	if(tp == 0){
		tp = newtlbpid(p);
		p->pidonmach[m->machno] = tp;
	}
/*	if(p->upage->va != (USERADDR|(p->pid&0xFFFF)))
		panic("mapstack %d 0x%lux 0x%lux", p->pid, p->upage->pa, p->upage->va);
*/
	/* don't set m->pidhere[*tp] because we're only writing entry 0 */
	tlbvirt = USERADDR | PTEPID(tp);
	tlbphys = p->upage->pa | PTEWRITE | PTEVALID | PTEGLOBL;
	puttlbx(0, tlbvirt, tlbphys);
	putstlb(tlbvirt, tlbphys);
	u = (User*)USERADDR;
}

/*
 * Process must be non-interruptible
 */
int
newtlbpid(Proc *p)
{
	int i, s;
	Proc *sp;
	char *h;

	s = m->lastpid;
	if(s >= NTLBPID)
		s = 1;
	i = s;
	h = m->pidhere;
	do{
		i++;
		if(i >= NTLBPID)
			i = 1;
	}while(h[i] && i != s);
	
	if(i == s){
		i++;
		if(i >= NTLBPID)
			i = 1;
	}
	sp = m->pidproc[i];
	if(sp){
		if(sp->pidonmach[m->machno] == i)
			sp->pidonmach[m->machno] = 0;
		purgetlb(i);
	}
	m->lastpid = i;
	m->pidproc[i] = p;
	return i;
}

void
putmmu(ulong tlbvirt, ulong tlbphys)
{
	short tp;
	Proc *p;

	splhi();
	p = u->p;
/*	if(p->state != Running)
		panic("putmmu state %lux %lux %s\n", u, p, statename[p->state]);
*/
	p->state = MMUing;
	tp = p->pidonmach[m->machno];
	if(tp == 0){
		tp = newtlbpid(p);
		p->pidonmach[m->machno] = tp;
	}
	tlbvirt |= PTEPID(tp);
	putstlb(tlbvirt, tlbphys);
	puttlb(tlbvirt, tlbphys);
	m->pidhere[tp] = 1;
	p->state = Running;
	spllo();
}

void
purgetlb(int pid)
{
	Softtlb *entry, *etab;
	char *p;
	Proc *sp;
	int i, rpid;

	if(m->pidhere[pid] == 0)
		return;

	m->tlbpurge++;
	p = m->pidhere;
	memset(m->pidhere, 0, sizeof m->pidhere);
	for(i=TLBROFF; i<NTLB; i++)
		if(TLBPID(gettlbvirt(i)) == pid)
			puttlbx(i, KZERO | PTEPID(i), 0);
	entry = m->stb;
	etab = &entry[STLBSIZE];
	for(; entry < etab; entry++){
		rpid = TLBPID(entry->virt);
		if(rpid == pid){
			entry->phys = 0;
			entry->virt = 0;
		}else
			p[rpid] = 1;
	}
}

void
flushmmu(void)
{
	splhi();
	/* easiest is to forget what pid we had.... */
	memset(u->p->pidonmach, 0, sizeof u->p->pidonmach);
	/* ....then get a new one by trying to map our stack */
	mapstack(u->p);
	spllo();
}

void
clearmmucache(void)
{
}

void
invalidateu(void)
{
	puttlbx(0, KZERO | PTEPID(0), 0);
	putstlb(KZERO | PTEPID(0), 0);
}

void
putstlb(ulong tlbvirt, ulong tlbphys)
{
	Softtlb *entry;

	entry = &m->stb[((tlbvirt<<1) ^ (tlbvirt>>12)) & (STLBSIZE-1)];
	entry->phys = tlbphys;
	entry->virt = tlbvirt;
	if(tlbphys == 0)
		entry->virt = 0;
}
