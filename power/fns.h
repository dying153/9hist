Alarm	*alarm(int, void (*)(Alarm*), void*);
void	alarminit(void);
Block	*allocb(ulong);
void	append(List**, List*);
void	arginit(void);
void	cancel(Alarm*);
int	canlock(Lock*);
int	canqlock(QLock*);
void	chaninit(void);
void	chandevreset(void);
void	chandevinit(void);
void	clock(ulong, ulong);
void	clockinit(void);
Chan	*clone(Chan*, Chan*);
void	close(Chan*);
void	closemount(Mount*);
void	closepgrp(Pgrp*);
long	clrfpintr(void);
int	compactpte(Orig*, ulong);
ulong	confeval(char*);
void	confprint(void);
void	confinit(void);
void	confread(void);
void	confset(char*);
int	consactive(void);
int	conschar(void);
void	consoff(void);
int	consputc(int);
Env	*copyenv(Env*, int);
int	decref(Ref*);
void	delay(int);
void	delete(List**, List*, List*);
void	delete0(List**, List*);
Chan	*devattach(int, char*);
Chan	*devclone(Chan*, Chan*);
void	devdir(Chan*, long, char*, long, long, Dir*);
long	devdirread(Chan*, char*, long, Dirtab*, int, Devgen*);
Devgen	devgen;
int	devno(int, int);
Chan*	devopen(Chan*, int, Dirtab*, int, Devgen*);
void	devstat(Chan*, char*, Dirtab*, int, Devgen*);
int	devwalk(Chan*, char*, Dirtab*, int, Devgen*);
void	duartinit(void);
void	duartintr(void);
int	duartputc(int);
void	duartputs(char*);
void	duartreset(void);
void	duartxmit(int);
void	dumpregs(Ureg*);
void	dumpstack(void);
int	eqchan(Chan*, Chan*, long);
void	envpgclose(Env*);
void	error(Chan*, int);
void	evenaddr(ulong);
void	exit(void);
void	fault(Ureg*, int, int);
void	fdclose(int);
Chan*	fdtochan(int, int);
void	firmware(void);
void	flowctl(Queue*);
void	flushmmu(void);
void	forkmod(Seg*, Seg*, Proc*);
void	freeb(Block*);
void	freepage(Orig*);
void	freepte(Orig*);
void	freesegs(int);
void	freealarm(Alarm*);
Block	*getb(Blist*);
int	getfields(char*, char**, int, char);
Block	*getq(Queue*);
void	gettlb(int, ulong*);
ulong	gettlbvirt(int);
void	gotolabel(Label*);
void	gotopc(ulong);
void	growpte(Orig*, ulong);
void	*ialloc(ulong, int);
void	icflush(void *, int);
int	incref(Ref*);
void	insert(List**, List*, List*);
void	intr(ulong, ulong);
void	ioboardid(void);
void	ioboardinit(void);
void	isdir(Chan*);
void	kbdchar(int);
void	kproc(char*, void(*)(void*), void*);
void	lance3intr(void);
void	launchinit(void);
void	lancereset(void);
void	lanceinit(void);
void	lanceintr(void);
void	lanceparity(void);
void	lanceinit(void);
void	launch(int);
void	lights(int);
void	lock(Lock*);
void	lockinit(void);
Orig	*lookorig(ulong, ulong, int, Chan*);
void	machinit(void);
void	mapstack(Proc*);
int	mount(Chan*, Chan*, int);
Chan	*namec(char*, int, int, ulong);
void	nexterror(void);
Alarm	*newalarm(void);
Chan	*newchan(void);
PTE	*newmod(void);
Mount	*newmount(void);
Orig	*neworig(ulong, ulong, int, Chan*);
Page	*newpage(int, Orig*, ulong);
Pgrp	*newpgrp(void);
Proc	*newproc(void);
void	newqinfo(Qinfo*);
char	*nextelem(char*, char*);
void	newstart(void);
int	newtlbpid(Proc*);
int	nonetcksum(Block*, int);
void	nonetfreeifc(Noifc*);
Noifc*	nonetnewifc(Queue*, Stream*, int, int, int, void (*)(Noconv*, char*));
void	nonetrcvmsg(Noconv*, Block*);
void	novme(int);
void	nullput(Queue*, Block*);
void	online(void);
int	openmode(ulong);
void	pageinit(void);
void	panic(char*, ...);
void	pexit(char*, int);
void	pgrpcpy(Pgrp*, Pgrp*);
void	pgrpinit(void);
int	postnote(Proc*, int, char*, int);
int	pprint(char*, ...);
Block	*prepend(Block*, int);
void	prflush(void);
void	printinit(void);
void	printslave(void);
void	procinit0(void);
Proc	*proctab(int);
void	purgetlb(int);
Queue*	pushq(Stream*, Qinfo*);
void	putmmu(ulong, ulong);
void	puttlb(ulong, ulong);
void	puttlbx(int, ulong, ulong);
int	putb(Blist*, Block*);
void	putbq(Blist*, Block*);
int	putq(Queue*, Block*);
void	putstrn(char*, long);
ulong	pwait(Waitmsg*);
int	readlog(ulong, char*, ulong);
int	readnum(ulong, char*, ulong, ulong, int);
void	ready(Proc*);
void	rooterrstr(Error*, char*);
void	qlock(QLock*);
void	qunlock(QLock*);
void	restfpregs(FPsave*);
int	return0(void*);
Proc	*runproc(void);
void	savefpregs(FPsave*);
void	sched(void);
void	schedinit(void);
long	seconds(void);
Seg	*seg(Proc*, ulong);
int	segaddr(Seg*, ulong, ulong);
int	setlabel(Label*);
void	setvmevec(int, void (*)(int));
void	sinit(void);
char*	skipslash(char*);
void	sleep(Rendez*, int(*)(void*), void*);
uchar*	smap(int, uchar*);
int	splhi(void);
int	spllo(void);
void	splx(int);
void	sunmap(int, uchar*);
Devgen	streamgen;
void	streamclose(Chan*);
int	streamenter(Stream*);
void	streamexit(Stream*, int);
void	streaminit(void);
long	streamread(Chan*, void*, long);
long	streamwrite(Chan*, void*, long, int);
Stream*	streamnew(Chan*, Qinfo*);
void	streamopen(Chan*, Qinfo*);
int	streamparse(char*, Block*);
void	streamstat(Chan*, char*, char*);
long	stringread(Chan*, void*, long, char*);
long	syscall(Ureg*);
void	sysloginit(void);
void	syslog(char*, int);
void	tlbinit(void);
void	touser(void *);
void	tsleep(Rendez*, int (*)(void*), void*, int);
void	twakeme(Alarm*);
void	unlock(Lock*);
void	unusepage(Page*, int);
void	usepage(Page*, int);
void	userinit(void);
void	urpreset(void);
void	validaddr(ulong, ulong, int);
void	vecinit(void);
void	vector80(void);
void*	vmemchr(void*, int, int);
void	wakeme(Alarm*);
void	wakeup(Rendez*);
void	wbflush(void);

#define	waserror()	setlabel(&u->errlab[u->nerrlab++])
#define	poperror()	u->nerrlab--
