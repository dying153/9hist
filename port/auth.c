#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

#include	<auth.h>

char	*eve;
char	evekey[DESKEYLEN];
char	hostdomain[DOMLEN];

/*
 *  return true if current user is eve
 */
int
iseve(void)
{
	return strcmp(eve, up->user) == 0;
}

long
sysfversion(ulong *arg)
{
	Fcall f;	/* two Fcalls should be big enough for reply */
	uchar *msg;
	char *vers;
	uint arglen, n, m, msize;
	Chan *c;
	uvlong oo;

	msize = arg[1];
	vers = (char*)arg[2];
	arglen = arg[3];
	validaddr(arg[2], arglen, 1);
	/* check there's a NUL in the version string */
	if(memchr(vers, 0, arglen) == 0)
		error(Ebadarg);
	c = fdtochan(arg[0], ORDWR, 0, 1);
	if(waserror()){
		cclose(c);
		nexterror();
	}

	if((c->flag&CMSG) && c->version!=nil) /* BUG: insufficient; should check compatibility */
		goto Return;

	f.type = Tversion;
	f.tag = NOTAG;
	if(msize == 0)
		msize = IOHDRSZ+8192;	/* reasonable default */
	f.msize = msize;
	if(vers[0] == '\0')
		vers = VERSION9P;
	f.version = vers;
	msg = smalloc(8192+IOHDRSZ);
	if(waserror()){
		free(msg);
		nexterror();
	}
	n = convS2M(&f, msg, 8192+IOHDRSZ);
	if(n == 0)
		error("bad fversion conversion on send");

	lock(c);
	oo = c->offset;
	c->offset += n;
	unlock(c);

	m = devtab[c->type]->write(c, msg, n, oo);

	if(m < n){
		lock(c);
		c->offset -= n - m;
		unlock(c);
		error("short write in fversion");
	}

	/* message sent; receive and decode reply */
	m = devtab[c->type]->read(c, msg, 8192+IOHDRSZ, c->offset);
	if(m <= 0)
		error("EOF receiving fversion reply");

	lock(c);
	c->offset += m;
	unlock(c);

	n = convM2S(msg, m, &f);
	if(n != m)
		error("bad fversion conversion on reply");
	if(f.type != Rversion)
		error("unexpected reply type in fversion");
	if(f.msize > msize)
		error("server tries to increase msize in fversion");
	if(f.msize<256 || f.msize>1024*1024)
		error("nonsense value of msize in fversion");
	kstrdup(&c->version, f.version);
	c->iounit = f.msize;
	free(msg);
	poperror();

Return:
	m = strlen(c->version);
	if(m > arglen)
		m = arglen;
	memmove((char*)arg[2], c->version, m);

	cclose(c);
	poperror();
	return m;
}

long
sysfsession(ulong *arg)
{
	Fcall f;
	uchar *msg;
	uint authlen, n, m;
	Chan *c;
	uvlong oo;

//BUG print("warning: stub fsession being used\n");
	authlen = arg[2];
	validaddr(arg[1], authlen, 1);
	c = fdtochan(arg[0], ORDWR, 0, 1);
	if(waserror()){
		cclose(c);
		nexterror();
	}

	if(c->flag & CMSG){
//BUG what to do?
		((uchar*)arg[1])[0] = 0;
		poperror();
		cclose(c);
		return 0;
	}

	f.type = Tsession;
	f.tag = NOTAG;
	f.nchal = 0;
	f.chal = (uchar*)"";
	msg = smalloc(8192+IOHDRSZ);
	if(waserror()){
		free(msg);
		nexterror();
	}
	n = convS2M(&f, msg, 8192+IOHDRSZ);
	if(n == 0)
		error("bad fsession conversion on send");

	lock(c);
	oo = c->offset;
	c->offset += n;
	unlock(c);

	m = devtab[c->type]->write(c, msg, n, oo);

	if(m < n){
		lock(c);
		c->offset -= n - m;
		unlock(c);
		error("short write in fsession");
	}

	/* message sent; receive and decode reply */
	m = devtab[c->type]->read(c, msg, 8192+IOHDRSZ, c->offset);
	if(m <= 0)
		error("EOF receiving fsession reply");

	lock(c);
	c->offset += m;
	unlock(c);

	n = convM2S(msg, m, &f);
	if(n != m)
		error("bad fsession conversion on reply");
	if(f.type != Rsession)
		error("unexpected reply type in fsession");
	m = f.nchal;
	if(m > authlen)
		error(Eshort);
//BUG print("auth stuff ignored; noauth by default\n");
	((uchar*)arg[1])[0] = 0;

	free(msg);
	poperror();
	poperror();
	cclose(c);
	return m;
}

long
sysfauth(ulong *)
{
	error("sysfauth unimplemented");
	return -1;
}

/*
 *  called by devcons() for key device
 */
long
keyread(char *a, int n, long offset)
{
	if(n<DESKEYLEN || offset != 0)
		error(Ebadarg);
	if(!cpuserver || !iseve())
		error(Eperm);
	memmove(a, evekey, DESKEYLEN);
	return DESKEYLEN;
}

long
keywrite(char *a, int n)
{
	if(n != DESKEYLEN)
		error(Ebadarg);
	if(!iseve())
		error(Eperm);
	memmove(evekey, a, DESKEYLEN);
	return DESKEYLEN;
}

/*
 *  called by devcons() for user device
 *
 *  anyone can become none
 */
long
userwrite(char *a, int n)
{
	if(!iseve() || strcmp(a, "none") != 0)
		error(Eperm);
	kstrdup(&up->user, "none");
	up->basepri = PriNormal;
	return n;
}

/*
 *  called by devcons() for host owner/domain
 *
 *  writing hostowner also sets user
 */
long
hostownerwrite(char *a, int n)
{
	char buf[128];

	if(!iseve())
		error(Eperm);
	if(n >= sizeof buf)
		error(Ebadarg);
	strncpy(buf, a, n+1);
	if(buf[0] == '\0')
		error(Ebadarg);

	renameuser(eve, buf);
	kstrdup(&eve, buf);
	kstrdup(&up->user, buf);
	up->basepri = PriNormal;
	return n;
}

long
hostdomainwrite(char *a, int n)
{
	char buf[DOMLEN];

	if(!iseve())
		error(Eperm);
	if(n >= DOMLEN)
		error(Ebadarg);
	memset(buf, 0, DOMLEN);
	strncpy(buf, a, n);
	if(buf[0] == 0)
		error(Ebadarg);
	memmove(hostdomain, buf, DOMLEN);
	return n;
}
