#include	"u.h"
#include	"lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"ureg.h"
#include	"init.h"

/*
 *  args passed by boot process
 */
int _argc; char **_argv; char **_env;

/*
 *  arguments passed to initcode
 */
char argbuf[512];
int argsize;

/*
 *  environment passed to any kernel started by this kernel
 */
char envbuf[64];
char *env[2];

/*
 *  configuration file read by boot program
 */
char confbuf[4*1024];

/*
 *  system name
 */
char sysname[64];

/*
 *  IO board type
 */
int ioid;

void
main(void)
{
	machinit();
	active.exiting = 0;
	active.machs = 1;
	confinit();
	arginit();
	lockinit();
	printinit();
	tlbinit();
	vecinit();
	procinit0();
	pgrpinit();
	chaninit();
	clockinit();
	alarminit();
	ioboardinit();
	chandevreset();
	streaminit();
	pageinit();
	userinit();
	launchinit();
	schedinit();
}

void
machinit(void)
{
	int n;

	icflush(0, 64*1024);
	n = m->machno;
	memset(m, 0, sizeof(Mach));
	m->machno = n;
	m->mmask = 1<<m->machno;
}

void
tlbinit(void)
{
	int i;

	for(i=0; i<NTLB; i++)
		puttlbx(i, KZERO | PTEPID(i), 0);
}

void
vecinit(void)
{
	ulong *p, *q;
	int size;

	p = (ulong*)EXCEPTION;
	q = (ulong*)vector80;
	for(size=0; size<4; size++)
		*p++ = *q++;
	p = (ulong*)UTLBMISS;
	q = (ulong*)vector80;
	for(size=0; size<4; size++)
		*p++ = *q++;
}

/*
 *  We have to program both the IO2 board to generate interrupts
 *  and the SBCC on CPU 0 to accept them.
 */
void
ioboardinit(void)
{
	long i;
	int noforce;
	int maxlevel;

	ioid = *IOID;
	if(ioid >= IO3R1){
		maxlevel = 11;
		noforce = 1;
	} else {
		maxlevel = 8;
		noforce = 0;
	}


	/*
	 *  reset VME bus (MODEREG is on the IO2)
	 */
	MODEREG->resetforce = (1<<1) | noforce;
	for(i=0; i<1000000; i++)
		;
	MODEREG->resetforce = noforce;
	MODEREG->masterslave = (SLAVE<<4) | MASTER;

	/*
	 *  all VME interrupts to the error routine
	 */
	for(i=0; i<256; i++)
		setvmevec(i, novme);

	/*
	 *  tell IO2 to sent all interrupts to CPU 0's SBCC
	 */
	for(i=0; i<maxlevel; i++)
		INTVECREG->i[i].vec = 0<<8;

	/*
	 *  Tell CPU 0's SBCC to map all interrupts from the IO2 to MIPS level 5
	 *
	 *	0x01		level 0
	 *	0x02		level 1
	 *	0x04		level 2
	 *	0x08		level 4
	 *	0x10		level 5
	 */
	SBCCREG->flevel = 0x10;

	/*
	 *  Tell CPU 0's SBCC to enable all interrupts from the IO2.
	 *
	 *  The SBCC 16 bit registers are read/written as ulong, but only
	 *  bits 23-16 and 7-0 are meaningful.
	 */
	SBCCREG->fintenable |= 0xff;	  /* allow all interrupts on the IO2 */
	SBCCREG->idintenable |= 0x800000; /* allow interrupts from the IO2 */

	/*
	 *  enable all interrupts on the IO2
	 */
	*IO2SETMASK = 0xff;
}

void
launchinit(void)
{
	int i;

	for(i=1; i<conf.nmach; i++)
		launch(i);
	for(i=0; i<1000000; i++)
		if(active.machs == (1<<conf.nmach) - 1){
			print("all launched\n");
			return;
		}
	print("launch: active = %x\n", active.machs);
}


void
init0(void)
{
	int i;
	ulong *sp;

	m->proc = u->p;
	u->p->state = Running;
	u->p->mach = m;
	spllo();

	chandevinit();

	u->slash = (*devtab[0].attach)(0);
	u->dot = clone(u->slash, 0);

	sp = (ulong*)(USTKTOP - argsize);

	touser(sp);
}

FPsave	initfp;

void
userinit(void)
{
	Proc *p;
	Seg *s;
	User *up;
	KMap *k;
	int i;
	char **av;

	p = newproc();
	p->pgrp = newpgrp();
	strcpy(p->text, "*init*");
	strcpy(p->pgrp->user, "bootes");
	savefpregs(&initfp);
	p->fpstate = FPinit;

	/*
	 * Kernel Stack
	 */
	p->sched.pc = (ulong)init0;
	p->sched.sp = USERADDR+BY2PG-20;
	p->upage = newpage(0, 0, USERADDR|(p->pid&0xFFFF));

	/*
	 * User
	 */
	k = kmap(p->upage);
	up = (User*)VA(k);
	up->p = p;
	kunmap(k);

	/*
	 * User Stack, pass input arguments to boot process
	 */
	s = &p->seg[SSEG];
	s->proc = p;
	s->o = neworig(USTKTOP-BY2PG, 1, OWRPERM, 0);
	s->o->pte[0].page = newpage(0, 0, USTKTOP-BY2PG);
	memcpy((ulong*)(s->o->pte[0].page->pa|KZERO|(BY2PG-argsize)), 
		argbuf + sizeof(argbuf) - argsize, argsize);
	av = (char **)(s->o->pte[0].page->pa|KZERO|(BY2PG-argsize));
	for(i = 0; i < _argc; i++)
		av[i] += (char *)USTKTOP - (argbuf + sizeof(argbuf));
	s->minva = USTKTOP-BY2PG;
	s->maxva = USTKTOP;

	/*
	 * Text
	 */
	s = &p->seg[TSEG];
	s->proc = p;
	/*
	 * On the mips, init text must be OCACHED to avoid reusing page
	 * and getting in trouble with the hardware instruction cache.
	 */
	s->o = neworig(UTZERO, 1, OCACHED, 0);
	s->o->pte[0].page = newpage(0, 0, UTZERO);
	s->o->npage = 1;
	k = kmap(s->o->pte[0].page);
	memcpy((ulong*)VA(k), initcode, sizeof initcode);
	kunmap(k);
	s->minva = 0x1000;
	s->maxva = 0x2000;

	ready(p);
}

void
lights(int v)
{

	*LED = ~v;
}

typedef struct Beef	Beef;
struct	Beef
{
	long	deadbeef;
	long	sum;
	long	cpuid;
	long	virid;
	long	erno;
	void	(*launch)(void);
	void	(*rend)(void);
	long	junk1[4];
	long	isize;
	long	dsize;
	long	nonbss;
	long	junk2[18];
};

void
launch(int n)
{
	Beef *p;
	long i, s;
	ulong *ptr;

	p = (Beef*) 0xb0000500 + n;
	p->launch = newstart;
	p->sum = 0;
	s = 0;
	ptr = (ulong*)p;
	for (i = 0; i < sizeof(Beef)/sizeof(ulong); i++)
		s += *ptr++;
	p->sum = -(s+1);

	for(i=0; i<3000000; i++)
		if(p->launch == 0)
			break;
}

void
online(void)
{

	machinit();
	lock(&active);
	active.machs |= 1<<m->machno;
	unlock(&active);
	tlbinit();
	clockinit();
	schedinit();
}

void
exit(void)
{
	int i;

	u = 0;
	lock(&active);
	active.machs &= ~(1<<m->machno);
	active.exiting = 1;
	unlock(&active);
	spllo();
	print("cpu %d exiting\n", m->machno);
	while(active.machs || consactive())
		for(i=0; i<1000; i++)
			;
	splhi();
	for(i=0; i<2000000; i++)
		;
	duartreset();
	firmware();
}

/*
 * Insert new into list after where
 */
void
insert(List **head, List *where, List *new)
{
	if(where == 0){
		new->next = *head;
		*head = new;
	}else{
		new->next = where->next;
		where->next = new;
	}
		
}

/*
 * Insert new into list at end
 */
void
append(List **head, List *new)
{
	List *where;

	where = *head;
	if(where == 0)
		*head = new;
	else{
		while(where->next)
			where = where->next;
		where->next = new;
	}
	new->next = 0;
}

/*
 * Delete old from list
 */
void
delete0(List **head, List *old)
{
	List *l;

	l = *head;
	if(l == old){
		*head = old->next;
		return;
	}
	while(l->next != old)
		l = l->next;
	l->next = old->next;
}

/*
 * Delete old from list.  where->next is known to be old.
 */
void
delete(List **head, List *where, List *old)
{
	if(where == 0){
		*head = old->next;
		return;
	}
	where->next = old->next;
}

typedef struct Conftab {
	char *sym;
	ulong *x;
} Conftab;

#include "conf.h"

Conf	conf;

ulong
confeval(char *exp)
{
	char *op;
	Conftab *ct;

	/* crunch leading white */
	while(*exp==' ' || *exp=='\t')
		exp++;

	op = strchr(exp, '+');
	if(op != 0){
		*op++ = 0;
		return confeval(exp) + confeval(op);
	}

	op = strchr(exp, '*');
	if(op != 0){
		*op++ = 0;
		return confeval(exp) * confeval(op);
	}

	if(*exp >= '0' && *exp <= '9')
		return strtoul(exp, 0, 0);

	/* crunch trailing white */
	op = strchr(exp, ' ');
	if(op)
		*op = 0;
	op = strchr(exp, '\t');
	if(op)
		*op = 0;

	/* lookup in symbol table */
	for(ct = conftab; ct->sym; ct++)
		if(strcmp(exp, ct->sym) == 0)
			return *(ct->x);

	return 0;
}

/*
 *  each line of the configuration is of the form `param = expression'.
 */
void
confset(char *sym)
{
	char *val, *p;
	Conftab *ct;
	ulong x;

	/*
 	 *  parse line
	 */

	/* comment */
	if(p = strchr(sym, '#'))
		*p = 0;

	/* skip white */
	for(p = sym; *p==' ' || *p=='\t'; p++)
		;
	sym = p;

	/* skip sym */
	for(; *p && *p!=' ' && *p!='\t' && *p!='='; p++)
		;
	if(*p)
		*p++ = 0;

	/* skip white */
	for(; *p==' ' || *p=='\t' || *p=='='; p++)
		;
	val = p;

	/*
	 *  lookup value
	 */
	for(ct = conftab; ct->sym; ct++)
		if(strcmp(sym, ct->sym) == 0){
			*(ct->x) = confeval(val);
			return;
		}

	if(strcmp(sym, "sysname")==0){
		p = strchr(val, ' ');
		if(p)
			*p = 0;
		strcpy(sysname, val);
	}
}

/*
 *  read the ascii configuration left by the boot kernel
 */
void
confread(void)
{
	char *line;
	char *end;

	/*
	 *  process configuration file
	 */
	line = confbuf;
	while(end = strchr(line, '\n')){
		*end = 0;
		confset(line);
		line = end+1;
	}
}

void
confprint(void)
{
	Conftab *ct;

	/*
	 *  lookup value
	 */
	for(ct = conftab; ct->sym; ct++)
		print("%s == %d\n", ct->sym, *ct->x);
}

void
confinit(void)
{
	long x, i, j, *l;

	/*
	 *  copy configuration down from high memory
	 */
	strcpy(confbuf, (char *)(0x80000000 + 4*1024*1024 - 4*1024));

	/*
	 *  size memory
	 */
	x = 0x12345678;
	for(i=4; i<128; i+=4){
		l = (long*)(KSEG1|(i*1024L*1024L));
		*l = x;
		wbflush();
		*(ulong*)KSEG1 = *(ulong*)KSEG1;	/* clear latches */
		if(*l != x)
			break;
		x += 0x3141526;
	}
	conf.npage0 = i*1024/4;
	conf.npage = conf.npage0;

	/*
 	 *  clear MP bus error caused by sizing memory
	 */
	i = *SBEADDR;

	/*
	 *  set minimal default values
	 */
	conf.nmach = 1;
	conf.nmod = 2000;
	conf.nalarm = 10000;
	conf.norig = 500;
	conf.nchan = 1000;
	conf.npgenv = 800;
	conf.nmtab = 100;
	conf.nmount = 5000;
	conf.nmntdev = 150;
	conf.nmntbuf = 120;
	conf.nmnthdr = 120;
	conf.nstream = 128;
	conf.nsrv = 32;
	conf.nproc = 386;
	conf.npgrp = 100;
	conf.nnoifc = 1;
	conf.nnoconv = 32;
	conf.nurp = 256;
	conf.nenv = 15*conf.nproc;
	conf.nenvchar = 20 * conf.nenv;
	conf.npte = 4 * conf.npage;
	conf.nqueue = 3 * conf.nstream;
	conf.nblock = 10 * conf.nstream;

	confread();

	if(conf.nmach > MAXMACH)
		panic("confinit");

}

/*
 *  copy arguments passed by the boot kernel (or ROM) into a temporary buffer.
 *  we do this because the arguments are in memory that may be allocated
 *  to processes or kernel buffers.
 */
#define SYSENV "netaddr="
void
arginit(void)
{
	int i, n;
	int nbytes;
	int ssize;
	char *p;
	char **argv;
	char *charp;

	/*
	 *  get the system name from the environment
	 */
	if(*sysname == 0){
		for(argv = _env; *argv; argv++){
			if(strncmp(*argv, SYSENV, sizeof(SYSENV)-1)==0){
				strcpy(sysname, (*argv) + sizeof(SYSENV)-1);
				break;
			}
		}
	}
	strcpy(envbuf, SYSENV);
	strcat(envbuf, sysname);
	env[0] = envbuf;
	env[1] = 0;

	/*
	 *  trim arguments to make them fit in the buffer (argv[0] is sysname)
	 */
	nbytes = 0;
	_argv[0] = sysname;
	for(i = 0; i < _argc; i++){
		n = strlen(_argv[i]) + 1;
		ssize = BY2WD*(i+2) + ((nbytes+n+(BY2WD-1)) & ~(BY2WD-1));
		if(ssize > sizeof(argbuf))
			break;
		nbytes += n;
	}
	_argc = i;
	ssize = BY2WD*(i+1) + ((nbytes+(BY2WD-1)) & ~(BY2WD-1));

	/*
	 *  copy arguments into the buffer
	 */
	argv = (char**)(argbuf + sizeof(argbuf) - ssize);
	charp = (char*)(argbuf + sizeof(argbuf) - nbytes);
	for(i=0; i<_argc; i++){
		argv[i] = charp;
		n = strlen(_argv[i]) + 1;
		memcpy(charp, _argv[i], n);
		charp += n;
	}
	_argv = argv;
	argsize = ssize;
}
