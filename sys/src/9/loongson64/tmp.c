#include "u.h"
#include "../port64/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port64/error.h"
#include "ureg.h"

// archxxx.c
#include "../port64/netif.h"
#include "etherif.h"

int
archether(unsigned ctlrno, Ether *ether)
{
	switch(ctlrno) {
	case 0:
		ether->type = "rtl8139";
		ether->ctlrno = ctlrno;
		ether->irq = ILpci;
		ether->nopt = 0;
		ether->mbps = 100;
		return 1;
	}
	return -1;
}

// fptrap.c
void
fptrap(Ureg*)
{
	return;
}
