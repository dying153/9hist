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
ulong power_resume[200/4];

extern void sa1100_power_resume(void);
extern int setpowerlabel(void);


GPIOregs savedgpioregs;
Uartregs saveduart3regs;
Uartregs saveduart1regs;
Intrregs savedintrregs;


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
		uart1regs->ctl[0], uart1regs->status[0], uart1regs->status[1], 
		uart3regs->ctl[0], uart3regs->status[0], uart3regs->status[1]); 
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

static  void
uartcpy(Uartregs *to, Uartregs *from)
{
	to->ctl[0] = from->ctl[0];
//	to->ctl[1] = from->ctl[1];
//	to->ctl[2] = from->ctl[2];
	to->ctl[3] = from->ctl[3];

}

static void
gpiocpy(GPIOregs *to, GPIOregs *from)
{
	to->rising = from->rising;		// gpio intrs enabled
	to->falling= from->falling;		// gpio intrs enabled
	to->altfunc = from->altfunc;
	to->direction = from->direction;
}

static void
sa1100_power_off(void)
{
	/* enable wakeup by µcontroller, on/off switch or real-time clock alarm */
	powerregs->pwer =  1 << IRQrtc | 1 << IRQgpio0 | 1 << IRQgpio1;

	/* clear previous reset status */
	resetregs->rcsr =  RCSR_all;

	/* disable internal oscillator, float CS lines */
	powerregs->pcfr = PCFR_opde | PCFR_fp | PCFR_fs;
	powerregs->pgsr = 0;
	/* set resume address. The loader jumps to it */
	powerregs->pspr = (ulong)sa1100_power_resume;
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

void
onoffintr(Ureg* , void*)
{
	static int power_pl;
	int i;
	for (i = 0; i < 100; i++) {
		delay(1);
		if ((gpioregs->level & GPIO_PWR_ON_i) == 0)
			return;	/* bounced */
	}
	power_pl = splhi();
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
		uartcpy(uart3regs,&saveduart3regs);
		uart3regs->status[0] = uart3regs->status[0];
		uartcpy(uart1regs,&saveduart1regs);
		uart1regs->status[0] = uart1regs->status[0];
		µcpower(1);
		screenpower(1);
		iprint("\nresuming execution\n");
//		dumpitall();
		delay(800);
		splx(power_pl);
		return;
	}
	/* Power down */
	iprint("\nentering suspend mode\n");
	gpiocpy(&savedgpioregs, gpioregs);
	intrcpy(&savedintrregs, intrregs);
	uartcpy(&saveduart3regs, uart3regs);
	uartcpy(&saveduart1regs, uart1regs);
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



static int
bitno(ulong x)
{
	int i;

	for(i = 0; i < 8*sizeof(x); i++)
		if((1<<i) & x)
			break;
	return i;
}

void
powerinit(void)
{
	intrenable(GPIOrising, bitno(GPIO_PWR_ON_i), onoffintr, (void*)0x12344321, "on/off");
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

