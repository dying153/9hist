#include "u.h"
#include "lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"

void
lock(Lock *l)
{
	Lock *ll = l;
	int i;
	ulong pc;

	pc = getcallerpc(((uchar*)&l) - sizeof(l));

	for(i = 0; i < 1000000; i++){
    		if (tas(&ll->key) == 0){
			if(u)
				u->p->hasspin = 1;
			ll->pc = pc;
			return;
		}
		if(u && u->p->state == Running)
			sched();
	}
	i = l->key;
	l->key = 0;

	panic("lock loop 0x%lux key 0x%lux pc 0x%lux held by pc 0x%lux\n", l, i,
		pc, l->pc);
}

int
canlock(Lock *l)
{
	if(tas(&l->key))
		return 0;
	l->pc = getcallerpc(((uchar*)&l) - sizeof(l));
	if(u && u->p)
		u->p->hasspin = 1;
	return 1;
}

void
unlock(Lock *l)
{
	l->pc = 0;
	l->key = 0;
	if(u && u->p)
		u->p->hasspin = 0;
}