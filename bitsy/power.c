#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"ureg.h"
#include	"pool.h"

/* Power management for the bitsy */

/* saved state during power down. 
 * it's only used up to 164/4.
 * it's only used by routines in l.s
 */
ulong	power_resume[200/4];
Rendez	powerr;
ulong	powerflag = 0;	/* set to start power-off sequence */

extern void	sa1100_power_resume(void);
extern int		setpowerlabel(void);
extern void	_start(void);
extern Uart	sa1110uart[];

GPIOregs savedgpioregs;
Intrregs savedintrregs;

#define R(p) ((Uartregs*)((p)->regs))

static void
dumpitall(void)
{
	iprint("intr: icip %lux iclr %lux iccr %lux icmr %lux\n",
		intrregs->icip,
		intrregs->iclr, intrregs->iccr, intrregs->icmr );
	iprint("gpio: lvl %lux dir %lux, re %lux, fe %lux sts %lux alt %lux\n",
		gpioregs->level,
		gpioregs->direction, gpioregs->rising, gpioregs->falling,
		gpioregs->edgestatus, gpioregs->altfunc);
	iprint("uart1: %lux %lux %lux \nuart3: %lux %lux %lux\n", 
		R(&sa1110uart[0])->ctl[0], R(&sa1110uart[0])->status[0], R(&sa1110uart[0])->status[1], 
		R(&sa1110uart[1])->ctl[0], R(&sa1110uart[1])->status[0], R(&sa1110uart[1])->status[1]); 
	iprint("tmr: osmr %lux %lux %lux %lux oscr %lux ossr %lux oier %lux\n",
		timerregs->osmr[0], timerregs->osmr[1],
		timerregs->osmr[2], timerregs->osmr[3],
		timerregs->oscr, timerregs->ossr, timerregs->oier);
	iprint("dram: mdcnfg %lux mdrefr %lux cas %lux %lux %lux %lux %lux %lux\n",
		memconfregs->mdcnfg, memconfregs->mdrefr,
		memconfregs->mdcas00, memconfregs->mdcas01,memconfregs->mdcas02,
		memconfregs->mdcas20, memconfregs->mdcas21,memconfregs->mdcas22); 
	iprint("dram: mdcnfg msc %lux %lux %lux mecr %lux\n",
		memconfregs->msc0, memconfregs->msc1,memconfregs->msc2,
		memconfregs->mecr);

	
}

static void
intrcpy(Intrregs *to, Intrregs *from)
{
	to->iclr = from->iclr;
	to->iccr = from->iccr;
	to->icmr = from->icmr;	// interrupts enabled
}

static void
gpiocpy(GPIOregs *to, GPIOregs *from)
{
	to->rising = from->rising;		// gpio intrs enabled
	to->falling= from->falling;		// gpio intrs enabled
	to->altfunc = from->altfunc;
	to->direction = from->direction;
}

void	(*restart)(void) = nil;

static void
sa1100_power_off(void)
{

	/* enable wakeup by µcontroller, on/off switch
	 * or real-time clock alarm
	 */
	powerregs->pwer =  1 << IRQrtc | 1 << IRQgpio0 | 1 << IRQgpio1;

	/* clear previous reset status */
	resetregs->rcsr =  RCSR_all;

	/* disable internal oscillator, float CS lines */
	powerregs->pcfr = PCFR_opde | PCFR_fp | PCFR_fs;
	powerregs->pgsr = 0;
	/* set resume address. The loader jumps to it */
//	powerregs->pspr = (ulong)sa1100_power_resume;
	powerregs->pspr = 0;
	/* set lowest clock; delay to avoid resume hangs on fast sa1110 */

	delay(90);
	powerregs->ppcr = 0;
	delay(90);

	/* set all GPIOs to input mode  */
	gpioregs->direction = 0;
	delay(100);
	/* enter sleep mode */

	powerregs->pmcr = PCFR_suspend;
	for(;;);
}

static int
bitno(ulong x)
{
	int i;

	for(i = 0; i < 8*sizeof(x); i++)
		if((1<<i) & x)
			break;
	return i;
}

int
powerdown(void *)
{
	return powerflag;
}

void
deepsleep(void) {
	static int power_pl;
	void *xsp, *xlink;

	power_pl = splhi();
xsp = getsp();
xlink = getlink();
iprint("starting power down, sp = %lux, link = %lux\n", xsp, xlink);
delay(1000);
	cachewb();
	delay(500);
	if (setpowerlabel()) {
		mmuinvalidate();
		mmuenable();
		cacheflush();
		trapresume();
		rs232power(1);
		irpower(1);
		audiopower(1);
		clockpower(1);
		gpclkregs->r0 = 1<<0;
		gpiocpy(gpioregs, &savedgpioregs);
		intrcpy(intrregs, &savedintrregs);
		if (intrregs->icip & (1<<IRQgpio0)){
			// don't want to sleep now. clear on/off irq.
			gpioregs->edgestatus = (1<<IRQgpio0);
			intrregs->icip = (1<<IRQgpio0);
		}
		uartpower(1);
		µcpower(1);
		screenpower(1);
		iprint("\nresuming execution\n");
//		dumpitall();
		delay(800);
xsp = getsp();
xlink = getlink();
iprint("power restored, sp = %lux, link = %lux\n", xsp, xlink);
delay(1000);

		splx(power_pl);
		return;
	}
	/* Power down */
	iprint("entering suspend mode\n");
	gpiocpy(&savedgpioregs, gpioregs);
	intrcpy(&savedintrregs, intrregs);
	uartpower(0);
	delay(400);
	clockpower(0);
	irpower(0);
	audiopower(0);
	screenpower(0);
	µcpower(0);
	rs232power(0);
	sa1100_power_off();
	/* no return */
}

void
powerkproc(void*)
{

	for(;;){
		while(powerflag == 0)
			sleep(&powerr, powerdown, 0);

		deepsleep();

		delay(2000);

		powerflag = 0;
	}
}

void
onoffintr(Ureg* , void*)
{
	int i;

	/* Power down interrupt comes on power button release.
	 * Act only after button has been released a full 100 ms
	 */

	if (powerflag)
		return;

	for (i = 0; i < 100; i++) {
		delay(1);
		if ((gpioregs->level & GPIO_PWR_ON_i) == 0)
			return;	/* bounced */
	}

	powerflag = 1;
	wakeup(&powerr);
}

void
powerinit(void)
{
	intrenable(GPIOrising, bitno(GPIO_PWR_ON_i), onoffintr, nil, "on/off");
}

void
idlehands(void)
{
#ifdef notdef
	char *msgb = "idlehands called with splhi\n";
	char *msga = "doze returns with splhi\n";

	if(!islo()){
		serialputs(msga, strlen(msga));
		spllo();
	}
	doze();
	if(!islo()){
		serialputs(msgb, strlen(msgb));
		spllo();
	}
#endif
}

