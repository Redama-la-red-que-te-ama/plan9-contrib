#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"

#include	"ureg.h"

enum {
	Cyccntres	= 2, /* counter advances at ½ clock rate */
	Basetickfreq	= 667*Mhz / Cyccntres,
};

enum {
	Instrs		= 10*Mhz,
};

ulong cpufreq;
ulong basetickfreq;

/*
 *  delay for l milliseconds more or less.
 */
void
delay(int l)
{
	while(l-- > 0)
		microdelay(1000);
}

/*
 *  microseconds delay
 */
void
microdelay(int l)
{
	int s;
	ulong x, cyc, cnt, speed;

	speed = m->speed;
	if (speed == 0)
		speed = cpufreq / Mhz;
	cyc = (ulong)l * (speed / Cyccntres);
	s = splhi();
	cnt = rdcount();
	x = cnt + cyc;
	if (x < cnt || x >= ~0ul - cpufreq/Cyccntres) {
		/* counter will wrap between now and x, or x is too near ~0 */
		wrcount(0);			/* somewhat drastic */
		wrcompare(rdcompare() - cnt);	/* match new count */
		x = cyc;
	}
	while(rdcount() < x)
		;
	splx(s);
}

void
clock(Ureg *ureg)
{
	wrcompare(rdcount()+m->maxperiod);	/* side-effect: dismiss intr */
	timerintr(ureg, 0);
}

static long
issue1loop(void)
{
	register regint i;
	long st;

	i = Instrs;
	st = perfticks();
	do {
		--i; --i; --i; --i; --i; --i; --i; --i; --i; --i;
		--i; --i; --i; --i; --i; --i; --i; --i; --i; --i;
		--i; --i; --i; --i; --i; --i; --i; --i; --i; --i;
		--i; --i; --i; --i; --i; --i; --i; --i; --i; --i;
		--i; --i; --i; --i; --i; --i; --i; --i; --i; --i;
		--i; --i; --i; --i; --i; --i; --i; --i; --i; --i;
		--i; --i; --i; --i; --i; --i; --i; --i; --i; --i;
		--i; --i; --i; --i; --i; --i; --i; --i; --i; --i;
		--i; --i; --i; --i; --i; --i; --i; --i; --i; --i;
		--i; --i; --i; --i; --i;
		/* omit 3 (--i) to account for conditional branch, nop & jump */
		i -= 1+3;	 /* --i plus 3 omitted (--i) instructions */
	} while(--i >= 0);
	return perfticks() - st;
}

/* estimate instructions/s. */
static int
guessmips(long (*loop)(void), char *)
{
	int s;
	long cyc;

	do {
		s = splhi();
		cyc = loop();
		splx(s);
		if (cyc < 0)
			iprint("again...");
	} while (cyc < 0);
	/*
	 * Instrs instructions took cyc cycles @ basetickfreq Hz.
	 * round the result.
	 */
	return (((vlong)basetickfreq * Instrs) / cyc + Mhz/2) / Mhz;
}

void
clockinit(void)
{
	int mips;

	/*
	 * calibrate fastclock
	 */
	if(cpufreq != 0)
		basetickfreq = cpufreq / Cyccntres;
	else
		basetickfreq = Basetickfreq;	/* sanity */
	mips = guessmips(issue1loop, "single");

	/*
	 * m->delayloop should be the number of delay loop iterations
	 * needed to consume 1 ms, assuming 2 instr'ns in the delay loop.
	 */
	m->delayloop = mips*Mhz / (1000 * 2);
	if(m->delayloop == 0)
		m->delayloop = 1;

	m->speed = mips;
	m->hz = m->speed*Mhz;

	m->maxperiod = basetickfreq / HZ;
	m->minperiod = basetickfreq / (100*HZ);
	wrcompare(rdcount()+m->maxperiod);

	/*
	 *  desynchronize the processor clocks so that they all don't
	 *  try to resched at the same time.
	 */
	delay(m->machno*2);

	syncclock();
	intron(INTR7);
}

/*
 * Tval is supposed to be in fastticks units.
 * One fasttick unit is 1/Basetickfreq seconds.
 */
void
timerset(Tval next)
{
	int x;
	long period;

	if(next == 0)
		return;
	x = splhi();			/* don't let us get scheduled */
	period = next - fastticks(nil);
	if(period > m->maxperiod - m->minperiod)
		period = m->maxperiod;
	else if(period < m->minperiod)
		period = m->minperiod;
	wrcompare(rdcount()+period);
	splx(x);
}

/*
 *  The rewriting of compare in this routine shouldn't be necessary.
 *  However, we lose clock interrupts if I don't, either a chip bug
 *  or one of ours -- presotto
 */
uvlong
fastticks(uvlong *hz)
{
	int x;
	ulong delta, count;

	if(hz)
		*hz = basetickfreq;

	/* avoid reentry on interrupt or trap, to prevent recursion */
	x = splhi();
	count = rdcount();
	if(rdcompare() - count > m->maxperiod)
		wrcompare(count+m->maxperiod);

	if (count < m->lastcount)		/* wrapped around? */
		delta = count + ((1ull<<32) - m->lastcount);
	else
		delta = count - m->lastcount;
	m->lastcount = count;
	m->fastticks += delta;
	splx(x);
	return m->fastticks;
}

ulong
µs(void)
{
	return fastticks2us(fastticks(nil));
}

/*
 *  performance measurement ticks.  must be low overhead.
 *  doesn't have to count over a second.
 */
ulong
perfticks(void)
{
	return rdcount();
}

long
lcycles(void)
{
	return perfticks();
}

/* should use vlong hw counters ideally; lcycles is inadequate */
void
cycles(uvlong *cycp)
{
	*cycp = fastticks(nil);
}

Lock mpsynclock;

/*
 *  synchronize all clocks with processor 0
 */
void
syncclock(void)
{
	uvlong x;

	if(m->machno == 0){
		m->lastcount = rdcount();
		m->fastticks = 0;
		m->ticks = 0;
		wrcompare(rdcount()+m->maxperiod);
	} else {
		/* wait for processor 0's soft clock to change and then sync ours */
		lock(&mpsynclock);
		x = MACHP(0)->fastticks;
		while(MACHP(0)->fastticks == x)
			;
		m->lastcount = rdcount();
		m->fastticks = MACHP(0)->fastticks;
		m->ticks = MACHP(0)->ticks;
		wrcompare(rdcount()+m->maxperiod);
		unlock(&mpsynclock);
	}
}
