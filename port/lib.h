/*
 * functions (possibly) linked in, complete, from libc.
 */

/*
 * mem routines
 */
extern	void	*memccpy(void*, void*, int, long);
extern	void	*memset(void*, int, long);
extern	int	memcmp(void*, void*, long);
extern	void	*memmove(void*, void*, long);
extern	void	*memchr(void*, int, long);

/*
 * string routines
 */
extern	char	*strcat(char*, char*);
extern	char	*strchr(char*, char);
extern	char	*strrchr(char*, char);
extern	int	strcmp(char*, char*);
extern	char	*strcpy(char*, char*);
extern	char *strecpy(char*, char*, char*);
extern	char	*strncat(char*, char*, long);
extern	char	*strncpy(char*, char*, long);
extern	int	strncmp(char*, char*, long);
extern	long	strlen(char*);
extern	char*	strstr(char*, char*);
extern	int	atoi(char*);
extern	int	fullrune(char*, int);

enum
{
	UTFmax		= 3,	/* maximum bytes per rune */
	Runesync	= 0x80,	/* cannot represent part of a UTF sequence */
	Runeself	= 0x80,	/* rune and UTF sequences are the same (<) */
	Runeerror	= 0x80,	/* decoding error in UTF */
};

/*
 * rune routines
 */
extern	int	runetochar(char*, Rune*);
extern	int	chartorune(Rune*, char*);
extern	char*	utfrune(char*, long);
extern	int	utflen(char*);
extern	int	runelen(long);

extern	int	abs(int);

/*
 * print routines
 */
typedef
struct
{
	char*	out;		/* pointer to next output */
	char*	eout;		/* pointer to end */
	int	f1;
	int	f2;
	int	f3;
	int	chr;
} Fconv;
extern	void	strconv(char*, Fconv*);
extern	int	numbconv(va_list*, Fconv*);
extern	char	*doprint(char*, char*, char*, va_list);
extern	int	fmtinstall(int, int (*)(va_list*, Fconv*));
extern	int	sprint(char*, char*, ...);
extern	char*	seprint(char*, char*, char*, ...);
extern	int	snprint(char*, int, char*, ...);
extern	int	print(char*, ...);

/*
 * one-of-a-kind
 */
extern	char*	cleanname(char*);
extern	ulong	getcallerpc(void*);
extern	void*	getsp(void);
extern	void*	getlink(void);

extern	long	strtol(char*, char**, int);
extern	ulong	strtoul(char*, char**, int);
extern	vlong	strtoll(char*, char**, int);
extern	uvlong	strtoull(char*, char**, int);
extern	char	etext[];
extern	char	edata[];
extern	char	end[];
extern	int	getfields(char*, char**, int, int, char*);
extern	int	tokenize(char*, char**, int);

/*
 * Syscall data structures
 */
#define	MORDER	0x0003	/* mask for bits defining order of mounting */
#define	MREPL	0x0000	/* mount replaces object */
#define	MBEFORE	0x0001	/* mount goes before others in union directory */
#define	MAFTER	0x0002	/* mount goes after others in union directory */
#define	MCREATE	0x0004	/* permit creation in mounted directory */
#define	MCACHE	0x0010	/* cache some data */
#define	MMASK	0x001F	/* all bits on */

#define	OREAD	0	/* open for read */
#define	OWRITE	1	/* write */
#define	ORDWR	2	/* read and write */
#define	OEXEC	3	/* execute, == read but check execute permission */
#define	OTRUNC	16	/* or'ed in (except for exec), truncate file first */
#define	OCEXEC	32	/* or'ed in, close on exec */
#define	ORCLOSE	64	/* or'ed in, remove on close */
#define OEXCL   0x1000	/* or'ed in, exclusive create */

#define	NCONT	0	/* continue after note */
#define	NDFLT	1	/* terminate after note */
#define	NSAVE	2	/* clear note but hold state */
#define	NRSTR	3	/* restore saved state */

typedef struct Qid	Qid;
typedef struct Dir	Dir;
typedef struct Waitmsg	Waitmsg;

#define	ERRMAX			128	/* max length of error string */
#define	KNAMELEN		28	/* max length of name held in kernel */

/* bits in Qid.type */
#define QTDIR		0x80		/* type bit for directories */
#define QTAPPEND	0x40		/* type bit for append only files */
#define QTEXCL		0x20		/* type bit for exclusive use files */
#define QTMOUNT		0x10		/* type bit for mounted channel */
#define QTFILE		0x00		/* plain file */

/* bits in Dir.mode */
#define DMDIR		0x80000000	/* mode bit for directories */
#define DMAPPEND	0x40000000	/* mode bit for append only files */
#define DMEXCL		0x20000000	/* mode bit for exclusive use files */
#define DMMOUNT		0x10000000	/* mode bit for mounted channel */
#define DMREAD		0x4		/* mode bit for read permission */
#define DMWRITE		0x2		/* mode bit for write permission */
#define DMEXEC		0x1		/* mode bit for execute permission */

struct Qid
{
	vlong	path;
	ulong	vers;
	uchar	type;
};

typedef
struct Dir {
	/* system-modified data */
	ushort	type;	/* server type */
	uint	dev;	/* server subtype */
	/* file data */
	Qid	qid;	/* unique id from server */
	ulong	mode;	/* permissions */
	ulong	atime;	/* last read time */
	ulong	mtime;	/* last write time */
	vlong	length;	/* file length: see <u.h> */
	char	*name;	/* last element of path */
	char	*uid;	/* owner name */
	char	*gid;	/* group name */
	char	*muid;	/* last modifier name */
} Dir;

struct Waitmsg
{
	char	pid[12];	/* of loved one */
	char	time[3*12];	/* of loved one and descendants */
	char	msg[64];	/* compatibility BUG */
};
