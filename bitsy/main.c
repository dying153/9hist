#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"ureg.h"
#include	"init.h"
#include	"pool.h"

Mach	*m;
Proc	*up;
Conf	conf;
int 	noprint;

static void	gpioinit(void);

void
main(void)
{
	mmuinvalidate();

	/* zero out bss */
	memset(edata, 0, end-edata);

	/* point to Mach structure */
	m = (Mach*)MACHADDR;
	memset(m, 0, sizeof(Mach));
	m->ticks = 1;

	rs232power(1);
	iprint("\nPlan 9 bitsy kernel\n");
	confinit();
	xinit();
	mmuinit();
	gpioinit();
	trapinit();
	sa1100_uartsetup(1);
	rs232power(1);
	screeninit();
	printinit();	/* from here on, print works, before this we need iprint */
	clockinit();
	procinit0();
	initseg();
	chandevreset();
	pageinit();
	swapinit();
	userinit();
	schedinit();
}

/*
 *  exit kernel either on a panic or user request
 */
void
exit(int ispanic)
{
	void (*f)(void);

	USED(ispanic);
	delay(1000);

	iprint("it's a wonderful day to die\n");
	cacheflush();
	mmuinvalidate();
	mmudisable();
	f = nil;
	(*f)();
}

static uchar *sp;

/*
 *  starting place for first process
 */
void
init0(void)
{
	up->nerrlab = 0;

	spllo();

	/*
	 * These are o.k. because rootinit is null.
	 * Then early kproc's will have a root and dot.
	 */
	up->slash = namec("#/", Atodir, 0, 0);
	cnameclose(up->slash->name);
	up->slash->name = newcname("/");
	up->dot = cclone(up->slash, 0);

	chandevinit();

	if(!waserror()){
		ksetenv("terminal", "bitsy");
		ksetenv("cputype", "arm");
		if(cpuserver)
			ksetenv("service", "cpu");
		else
			ksetenv("service", "terminal");
		poperror();
	}
	kproc("alarm", alarmkproc, 0);

	touser(sp);
}

/*
 *  pass boot arguments to /boot
 */
static uchar *
pusharg(char *p)
{
	int n;

	n = strlen(p)+1;
	sp -= n;
	memmove(sp, p, n);
	return sp;
}
static void
bootargs(ulong base)
{
 	int i, ac;
	uchar *av[32];
	uchar *bootpath;
	uchar **lsp;

	/*
 	 *  the sizeof(Sargs) is to make the validaddr check in
	 *  trap.c's syscall() work even when we have less than the
	 *  max number of args.
	 */
	sp = (uchar*)base + BY2PG - sizeof(Sargs);

	bootpath = pusharg("/boot");
	ac = 0;
	av[ac++] = pusharg("boot");

	/* 4 byte word align stack */
	sp = (uchar*)((ulong)sp & ~3);

	/* build argc, argv on stack */
	sp -= (ac+1)*sizeof(sp);
	lsp = (uchar**)sp;
	for(i = 0; i < ac; i++)
		*lsp++ = av[i] + ((USTKTOP - BY2PG) - base);
	*lsp = 0;

	/* push argv onto stack */
	sp -= BY2WD;
	lsp = (uchar**)sp;
	*lsp = sp + BY2WD + ((USTKTOP - BY2PG) - base);

	/* push pointer to "/boot" */
	sp -= BY2WD;
	lsp = (uchar**)sp;
	*lsp = bootpath + ((USTKTOP - BY2PG) - base);

	/* leave space for where the initcode's caller's return PC would normally reside */
	sp -= BY2WD;

	/* relocate stack to user's virtual addresses */
	sp += (USTKTOP - BY2PG) - base;
}

/*
 *  create the first process
 */
void
userinit(void)
{
	Proc *p;
	Segment *s;
	KMap *k;
	Page *pg;

	/* no processes yet */
	up = nil;

	p = newproc();
	p->pgrp = newpgrp();
	p->egrp = smalloc(sizeof(Egrp));
	p->egrp->ref = 1;
	p->fgrp = dupfgrp(nil);
	p->rgrp = newrgrp();
	p->procmode = 0640;

	strcpy(p->text, "*init*");
	strcpy(p->user, eve);

	/*
	 * Kernel Stack
	 */
	p->sched.pc = (ulong)init0;
	p->sched.sp = (ulong)p->kstack+KSTACK-(sizeof(Sargs)+BY2WD);

	/*
	 * User Stack
	 */
	s = newseg(SG_STACK, USTKTOP-USTKSIZE, USTKSIZE/BY2PG);
	p->seg[SSEG] = s;
	pg = newpage(1, 0, USTKTOP-BY2PG);
	segpage(s, pg);
	k = kmap(pg);
	bootargs(VA(k));
	kunmap(k);

	/*
	 * Text
	 */
	s = newseg(SG_TEXT, UTZERO, 1);
	p->seg[TSEG] = s;
	pg = newpage(1, 0, UTZERO);
	memset(pg->cachectl, PG_TXTFLUSH, sizeof(pg->cachectl));
	segpage(s, pg);
	k = kmap(s->map[0]->pages[0]);
	memmove((ulong*)VA(k), initcode, sizeof initcode);
	kunmap(k);

	ready(p);
}

/*
 *  set mach dependent process state for a new process
 */
void
procsetup(Proc *p)
{
	p->fpstate = FPinit;
}

/*
 *  Save the mach dependent part of the process state.
 */
void
procsave(Proc *p)
{
	USED(p);
}

/* place holder */
/*
 *  dummy since rdb is not included 
 */
void
rdb(void)
{
}

/*
 *  probe the last location in a meg of memory, make sure it's not
 *  reflected into something else we've already found.
 */
int
probemem(ulong addr)
{
	ulong *p;
	ulong a;

	addr += OneMeg - sizeof(ulong);
	p = (ulong*)addr;
	*p = addr;
	for(a = conf.base0+OneMeg-sizeof(ulong); a < conf.npage0; a += OneMeg){
		p = (ulong*)a;
		*p = 0;
	}
	for(a = conf.base1+OneMeg-sizeof(ulong); a < conf.npage1; a += OneMeg){
		p = (ulong*)a;
		*p = 0;
	}
	p = (ulong*)addr;
	if(*p != addr)
		return -1;
	return 0;
}

/*
 *  we assume that the kernel is at the beginning of one of the
 *  contiguous chunks of memory.
 */
void
confinit(void)
{
	int i;
	ulong addr;
	ulong ktop;

	/* find first two contiguous sections of available memory */
	addr = PHYSDRAM0;
	conf.base0 = conf.npage0 = addr;
	conf.base1 = conf.npage1 = addr;
	for(i = 0; i < 512; i++){
		if(probemem(addr) == 0)
			break;
		addr += OneMeg;
	}
	for(; i < 512; i++){
		if(probemem(addr) < 0)
			break;
		addr += OneMeg;
		conf.npage0 = addr;
	}

	conf.base1 = conf.npage1 = addr;
	for(; i < 512; i++){
		if(probemem(addr) == 0)
			break;
		addr += OneMeg;
	}
	for(; i < 512; i++){
		if(probemem(addr) < 0)
			break;
		addr += OneMeg;
		conf.npage1 = addr;
	}

	/* take kernel out of allocatable space */
	ktop = PGROUND((ulong)end);
	if(ktop >= conf.base0 && ktop <= conf.npage0)
		conf.base0 = ktop;
	else if(ktop >= conf.base1 && ktop <= conf.npage1)
		conf.base1 = ktop;
	else
		iprint("kernel not in allocatable space\n");

	/* make npage the right thing */
	conf.npage0 = (conf.npage0 - conf.base0)/BY2PG;
	conf.npage1 = (conf.npage1 - conf.base1)/BY2PG;
	conf.npage = conf.npage0+conf.npage1;

	if(conf.npage > 16*MB/BY2PG){
		conf.upages = (conf.npage*60)/100;
		imagmem->minarena = 4*1024*1024;
	}else
		conf.upages = (conf.npage*40)/100;
	conf.ialloc = ((conf.npage-conf.upages)/2)*BY2PG;

	/* only one processor */
	conf.nmach = 1;

	/* set up other configuration parameters */
	conf.nproc = 100;
	conf.nswap = conf.npage*3;
	conf.nswppo = 4096;
	conf.nimage = 200;

	conf.monitor = 1;

	conf.copymode = 0;		/* copy on write */
}

GPIOregs *gpioregs;
ulong *egpioreg = (ulong*)EGPIOREGS;

static void
gpioinit(void)
{
	gpioregs = mapspecial(GPIOREGS, 32);
	gpioregs->direction = 
		GPIO_LDD8_o|GPIO_LDD9_o|GPIO_LDD10_o|GPIO_LDD11_o
		|GPIO_LDD12_o|GPIO_LDD13_o|GPIO_LDD14_o|GPIO_LDD15_o
		|GPIO_CLK_SET0_o|GPIO_CLK_SET1_o
		|GPIO_L3_SDA_io|GPIO_L3_MODE_o|GPIO_L3_SCLK_o
		|GPIO_COM_RTS_o;
	gpioregs->rising = 0;
	gpioregs->falling = 0;

	egpioreg = mapspecial(EGPIOREGS, 4);
}

static ulong egpiosticky;

void
rs232power(int on)
{
	if(on)
		egpiosticky |= EGPIO_rs232_power;
	else
		egpiosticky &= ~EGPIO_rs232_power;
	*egpioreg = egpiosticky;
}

void
irpower(int on)
{
	if(on)
		egpiosticky |= EGPIO_ir_power;
	else
		egpiosticky &= ~EGPIO_ir_power;
	*egpioreg = egpiosticky;
}

void
lcdpower(int on)
{
	if(on)
		egpiosticky |= EGPIO_lcd_3v|EGPIO_lcd_ic_power|EGPIO_lcd_5v|EGPIO_lcd_9v;
	else
		egpiosticky &= ~(EGPIO_lcd_3v|EGPIO_lcd_ic_power|EGPIO_lcd_5v|EGPIO_lcd_9v);
	*egpioreg = egpiosticky;
}
