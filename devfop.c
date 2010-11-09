#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"

typedef struct Aux Aux;

enum {
	Qroot,
	Qctl,
	Qevents,
	Nfiles = 3,
	Evsize = 256,
	Nmon = 10,

	/* c->dev types */
	Droot = 0,
	Dmon = -1,

	Debug = 1,
};

struct Aux
{
	union {
		Queue *q;		/* root holds the event queue */
		Chan *c;		/* monitored Chan */
	};
	ushort type;	/* real type for monitored Chan */
};
		
	

Chan *monitored[Nmon];	/* monitored Chans, indexed by monitor Chan dev */

Dirtab fopsdir[] = 
{
	".",		{Qroot,0,QTDIR}, 0,	DMDIR|0500,
	"ctl",		{Qctl}, 0, 0600,
	"events",	{Qevents}, 0, 0600,
};

static void
fopinit(void)
{
}

#define dbg if(Debug)print

void
dbgchan(Chan *c)
{
	Aux *a;

	if(c == nil)
		return;

	dbg("chan @ %p %s dev %lud type %d mchan %s", 
		c, chanpath(c), c->dev, c->type, chanpath(c->mchan));
	if(c->aux){
		a = c->aux;
		dbg(" aux[ c %s type %d]", chanpath(a->c), a->type);
	}
	dbg("\n");
}

/* Every root of the tree has a new event queue */
static Chan*
fopattach(char *spec)
{
	Chan *c;
	Queue *q;
	Aux *a;
	ulong dev;

	dev = Droot;			/* root fop */
	if(spec && *spec)
		dev = atoi(spec)+1;
	print("fopattach: spec %s dev %lud\n", spec, dev);

	c = devattach('O', spec);
	a = malloc(sizeof(Aux));

	if(dev == Droot){
		q = qopen(Evsize, 0, 0, 0); /* event queue */
		if(q == 0){
			free(a);
			exhausted("memory for queue");
		}
		a->q = q;
	}else{
		a->c = monitored[dev];
		print("fopattach: "), dbgchan(a->c);
	}

	mkqid(&c->qid, Qroot, 0, QTDIR);
	c->aux = a;
	c->dev = dev;
	return c;
}

static int
fopgen(Chan *c, char*, Dirtab *tab, int ntab, int i, Dir *dp)
{
	Qid q;

	if(i == DEVDOTDOT){
		dbg("fopgen: dotdot\n");
		devdir(c, c->qid, "#O", 0, eve, DMDIR|0555, dp);
		return 1;
	}

	i++;	/* skip . */
	if(tab == 0 ||  i >= ntab)
		return -1;

	tab += i;
	switch((ulong)tab->qid.path){
	case Qctl:
		dbg("fopgen: ctl\n");
		break;
	case Qevents:
		dbg("fopgen: events\n");
		break;
	default:
		break;
	}
	mkqid(&q, tab->qid.path, 0, QTFILE);
	devdir(c, q, tab->name, 0, eve, tab->perm, dp);
	return 1;
}

static Walkqid*
fopwalk(Chan *c, Chan *nc, char **name, int nname)
{
	Chan *mchan, *tc;
	Walkqid *wq;
	Aux *a;
	int i;

	print(">> fopwalk: "), dbgchan(c);
	for(i=0; i < nname; i++)
		dbg("fopwalk: name[%d] %s\n", i, name[i]);

	switch(c->dev){
	case Droot:	
		/* walk to the device's files */
		return devwalk(c, nc, name, nname, fopsdir, Nfiles, fopgen);
	case Dmon:
		/* walk to the files being monitored */
		a = c->aux;
		dbg("fopwalk: monitored file, dont know what to do\n");

		wq = devtab[a->type]->walk(a->c, nc, name, nname);
		if(wq == nil){
			print("could not walk\n");
			return nil;
		}
		return wq;
	default:
		/*
		 * more os less a transition state, we associate
		 * our device, say #O0, with a dir in the namespace
		 */
		a = c->aux;
		print("fopwalk: aux "), dbgchan(a->c);
		mchan = a->c->mchan;
		tc = a->c;

		wq = devwalk(a->c, nc, name, nname, 0, 0, 0);
		if(wq == nil){
			print("could not walk\n");
			return nil;
		}

		a = malloc(sizeof(Aux));
		if(a == nil)
			exhausted("fopwalk: no memory for Aux");

		wq->clone->aux = a;
		a->type = wq->clone->type;
		a->c = tc;

		/* patch dev for new Chan in order for us to own it */
		wq->clone->dev = Dmon;
		wq->clone->type = c->type;
		wq->clone->mchan = mchan;
		return wq;
	}
	// not reached
	return devwalk(c, nc, name, nname, fopsdir, Nfiles, fopgen);
}

static int
fopstat(Chan *c, uchar *db, int n)
{
	Dir dir;
	print("fopstat\n");
	
	switch((ulong)c->qid.path){
	case Qroot:
		devdir(c, c->qid, ".", 0, eve, DMDIR|0555, &dir);
		break;
	case Qctl:
		devdir(c, c->qid, "ctl", 0, eve, 0600, &dir);
		break;
	case Qevents:
		devdir(c, c->qid, "events", qlen((Queue*)c->aux), eve, 0600, &dir);
		break;
	default:
		panic("fopstat");
	}
	n = convD2M(&dir, db, n);
	if(n < BIT16SZ)
		error(Eshortstat);
	return n;
}

static Chan*
fopopen(Chan *c, int omode)
{
	print("fopopen\n");
	if(c->qid.type == QTDIR){
		if(omode != OREAD)
			error(Eisdir);
		c->mode = omode;
		c->flag |= COPEN;
		c->offset = 0;
		return c;
	}

	c->mode = openmode(omode);
	c->flag |= COPEN;
	c->offset = 0;
	return c;
}

static void
fopclose(Chan*)
{
	print("fopclose\n");
}

static long
fopread(Chan *c, void *va, long n, vlong off)
{
	Chan *cnew;
	print("fopread\n");

	USED(off);
	switch((ulong)c->qid.path){
	case Qroot:
		return devdirread(c, va, n, fopsdir, Nfiles, fopgen);
	case Qctl:
		/* from sysfile.c:/^bindmount */
		monitored[1] = namec("/cfg", Amount, 0, 0);
		cnew = namec("#O0", Abind, 0, 0);
		cmount(&cnew, monitored[1], MREPL, nil); // check errors
		return 0;
	case Qevents:
		return qread((Queue*)c->aux, va, n);
	default:
		panic("fopread");
	}
	return -1; /* not reached */
}

static long
fopwrite(Chan *c, void *va, long n, vlong off)
{
	USED(c); USED(va); USED(n); USED(off);
	print("fopwrite\n");
	switch((ulong)c->qid.path){
	case Qctl:
		return qwrite((Queue*)c->aux, va, n);
	case Qevents:
		return -1;
	default:
		panic("fopwrite");
	}
	return 0;
}


Dev fopdevtab = {
	'O',
	"fop",

	devreset,
	fopinit,
	devshutdown,
	fopattach,
	fopwalk,
	fopstat,
	fopopen,
	devcreate,
	fopclose,
	fopread,
	devbread,
	fopwrite,
	devbwrite,
	devremove,
	devwstat,
};
