/*
 * This file is part of DGD, https://github.com/dworkin/dgd
 * Copyright (C) 1993-2010 Dworkin B.V.
 * Copyright (C) 2010-2019 DGD Authors (see the commit log for details)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

# define INCLUDE_FILE_IO
# include "dgd.h"
# include "str.h"
# include "array.h"
# include "object.h"
# include "xfloat.h"
# include "data.h"
# include "interpret.h"
# include "call_out.h"

# define CYCBUF_SIZE	128		/* cyclic buffer size, power of 2 */
# define CYCBUF_MASK	(CYCBUF_SIZE - 1) /* cyclic buffer mask */
# define SWPERIOD	60		/* swaprate buffer size */

# define count		time
# define last		htime
# define prev		htime
# define next		mtime

static CallOut *cotab;			/* callout table */
static uindex cotabsz;			/* callout table size */
static uindex queuebrk;			/* queue brk */
static uindex cycbrk;			/* cyclic buffer brk */
static uindex flist;			/* free list index */
static uindex nzero;			/* # immediate callouts */
static uindex nshort;			/* # short-term callouts, incl. nzero */
static uindex running;			/* running callouts */
static uindex immediate;		/* immediate callouts */
static uindex cycbuf[CYCBUF_SIZE];	/* cyclic buffer of callout lists */
static Uint timestamp;			/* cycbuf start time */
static Uint timeout;			/* time of first callout in cycbuf */
static Uint timediff;			/* stored/actual time difference */
static Uint cotime;			/* callout time */
static unsigned short comtime;		/* callout millisecond time */
static Uint swaptime;			/* last swap count timestamp */
static Uint swapped1[SWPERIOD];		/* swap info for last minute */
static Uint swapped5[SWPERIOD];		/* swap info for last five minutes */
static Uint swaprate1;			/* swaprate per minute */
static Uint swaprate5;			/* swaprate per 5 minutes */

/*
 * initialize callout handling
 */
bool CallOut::init(unsigned int max)
{
    if (max != 0) {
	/* only if callouts are enabled */
	cotab = ALLOC(CallOut, max + 1);
	cotab[0].time = 0;	/* sentinel for the heap */
	cotab[0].mtime = 0;
	cotab++;
	flist = 0;
	timestamp = timeout = 0;
	timediff = 0;
    }
    running = immediate = 0;
    memset(cycbuf, '\0', sizeof(cycbuf));
    cycbrk = cotabsz = max;
    queuebrk = 0;
    nzero = nshort = 0;
    ::cotime = 0;

    swaptime = P_time();
    memset(swapped1, '\0', sizeof(swapped1));
    memset(swapped5, '\0', sizeof(swapped5));
    ::swaprate1 = ::swaprate5 = 0;

    return TRUE;
}

/*
 * put a callout in the queue
 */
CallOut *CallOut::enqueue(Uint t, unsigned short m)
{
    uindex i, j;
    CallOut *l;

    /*
     * create a free spot in the heap, and sift it upward
     */
# ifdef DEBUG
    if (queuebrk == cycbrk) {
	fatal("callout table overflow");
    }
# endif
    i = ++queuebrk;
    l = cotab - 1;
    for (j = i >> 1; l[j].time > t || (l[j].time == t && l[j].mtime > m);
	 i = j, j >>= 1) {
	l[i] = l[j];
    }

    l = &l[i];
    l->time = t;
    l->mtime = m;
    return l;
}

/*
 * remove a callout from the queue
 */
void CallOut::dequeue(uindex i)
{
    Uint t;
    short m;
    uindex j;
    CallOut *l;

    l = cotab - 1;
    i++;
    t = l[queuebrk].time;
    m = l[queuebrk].mtime;
    if (t < l[i].time) {
	/* sift upward */
	for (j = i >> 1; l[j].time > t || (l[j].time == t && l[j].mtime > m);
	     i = j, j >>= 1) {
	    l[i] = l[j];
	}
    } else if (i <= UINDEX_MAX / 2) {
	/* sift downward */
	for (j = i << 1; j < queuebrk; i = j, j <<= 1) {
	    if (l[j].time > l[j + 1].time ||
		(l[j].time == l[j + 1].time && l[j].mtime > l[j + 1].mtime)) {
		j++;
	    }
	    if (t < l[j].time || (t == l[j].time && m <= l[j].mtime)) {
		break;
	    }
	    l[i] = l[j];
	}
    }
    /* put into place */
    l[i] = l[queuebrk--];
}

/*
 * allocate a new callout for the cyclic buffer
 */
CallOut *CallOut::newcallout(uindex *list, Uint t)
{
    uindex i;
    CallOut *co, *first, *last;

    if (flist != 0) {
	/* get callout from free list */
	i = flist;
	flist = cotab[i].next;
    } else {
	/* allocate new callout */
# ifdef DEBUG
	if (cycbrk == queuebrk || cycbrk == 1) {
	    fatal("callout table overflow");
	}
# endif
	i = --cycbrk;
    }
    nshort++;
    if (t == 0) {
	nzero++;
    }

    co = &cotab[i];
    if (*list == 0) {
	/* first one in list */
	*list = i;
	co->count = 1;

	if (t != 0 && (timeout == 0 || t < timeout)) {
	    timeout = t;
	}
    } else {
	/* add to list */
	first = &cotab[*list];
	last = (first->count == 1) ? first : &cotab[first->last];
	last->next = i;
	first->count++;
	first->last = i;
    }
    co->prev = co->next = 0;

    return co;
}

/*
 * remove a callout from the cyclic buffer
 */
void CallOut::freecallout(uindex *cyc, uindex j, uindex i, Uint t)
{
    CallOut *l, *first;

    --nshort;
    if (t == 0) {
	--nzero;
    }

    l = cotab;
    first = &l[*cyc];
    if (i == j) {
	if (first->count == 1) {
	    *cyc = 0;

	    if (t != 0 && t == timeout) {
		if (nshort != nzero) {
		    while (cycbuf[t & CYCBUF_MASK] == 0) {
			t++;
		    }
		    timeout = t;
		} else {
		    timeout = 0;
		}
	    }
	} else {
	    *cyc = first->next;
	    l[first->next].count = first->count - 1;
	    if (first->count != 2) {
		l[first->next].last = first->last;
	    }
	}
    } else {
	--first->count;
	if (i == first->last) {
	    l[j].prev = l[j].next = 0;
	    if (first->count != 1) {
		first->last = j;
	    }
	} else {
	    l[j].next = l[i].next;
	}
    }

    l += i;
    l->handle = 0;	/* mark as unused */
    if (i == cycbrk) {
	/*
	 * callout at the edge
	 */
	while (++cycbrk != cotabsz && (++l)->handle == 0) {
	    /* followed by free callout */
	    if (cycbrk == flist) {
		/* first in the free list */
		flist = l->next;
	    } else {
		/* connect previous to next */
		cotab[l->prev].next = l->next;
		if (l->next != 0) {
		    /* connect next to previous */
		    cotab[l->next].prev = l->prev;
		}
	    }
	}
    } else {
	/* add to free list */
	if (flist != 0) {
	    /* link next to current */
	    cotab[flist].prev = i;
	}
	/* link to next */
	l->next = flist;
	flist = i;
    }
}

/*
 * get the current (adjusted) time
 */
Uint CallOut::cotime(unsigned short *mtime)
{
    Uint t;

    if (::cotime != 0) {
	*mtime = comtime;
	return ::cotime;
    }

    t = P_mtime(mtime) - timediff;
    if (t < timestamp) {
	/* clock turned back? */
	t = timestamp;
	*mtime = 0;
    } else if (timestamp < t) {
	if (running == 0) {
	    if (timeout == 0 || timeout > t) {
		timestamp = t;
	    } else if (timestamp < timeout) {
		timestamp = timeout - 1;
	    }
	}
	if (t > timestamp + 60) {
	    /* lot of lag? */
	    t = timestamp + 60;
	    *mtime = 0;
	}
    }

    comtime = *mtime;
    return ::cotime = t + timediff;
}

/*
 * check if, and how, a new callout can be added
 */
Uint CallOut::check(unsigned int n, Int delay, unsigned int mdelay, Uint *tp,
		    unsigned short *mp, uindex **qp)
{
    Uint t;
    unsigned short m;

    if (cotabsz == 0) {
	/*
	 * call_outs are disabled
	 */
	*qp = (uindex *) NULL;
	return 0;
    }

    if (queuebrk + (uindex) n == cycbrk || cycbrk - (uindex) n == 1) {
	error("Too many callouts");
    }

    if (delay == 0 && (mdelay == 0 || mdelay == 0xffff)) {
	/*
	 * immediate callout
	 */
	if (nshort == 0 && queuebrk == 0 && n == 0) {
	    cotime(mp);	/* initialize timestamp */
	}
	*qp = &immediate;
	*tp = t = 0;
	*mp = 0xffff;
    } else {
	/*
	 * delayed callout
	 */
	t = cotime(mp) - timediff;
	if (t + delay + 1 <= t) {
	    error("Too long delay");
	}
	t += delay;
	if (mdelay != 0xffff) {
	    m = *mp + mdelay;
	    if (m >= 1000) {
		m -= 1000;
		t++;
	    }
	} else {
	    m = 0xffff;
	}

	if (mdelay == 0xffff && t < timestamp + CYCBUF_SIZE) {
	    /* use cyclic buffer */
	    *qp = &cycbuf[t & CYCBUF_MASK];
	} else {
	    /* use queue */
	    *qp = (uindex *) NULL;
	}
	*tp = t;
	*mp = m;
    }

    return t;
}

/*
 * add a callout
 */
void CallOut::create(unsigned int oindex, unsigned int handle, Uint t,
		     unsigned int m, uindex *q)
{
    CallOut *co;

    if (q != (uindex *) NULL) {
	co = newcallout(q, t);
    } else {
	if (m == 0xffff) {
	    m = 0;
	}
	co = enqueue(t, m);
    }
    co->handle = handle;
    co->oindex = oindex;
}

/*
 * remove a short-term callout
 */
bool CallOut::rmshort(uindex *cyc, uindex i, uindex handle, Uint t)
{
    uindex j, k;
    CallOut *l;

    k = *cyc;
    if (k != 0) {
	/*
	 * this time-slot is in use
	 */
	l = cotab;
	if (l[k].oindex == i && l[k].handle == handle) {
	    /* first element in list */
	    freecallout(cyc, k, k, t);
	    return TRUE;
	}
	if (l[*cyc].count != 1) {
	    /*
	     * list contains more than 1 element
	     */
	    j = k;
	    k = l[j].next;
	    do {
		if (l[k].oindex == i && l[k].handle == handle) {
		    /* found it */
		    freecallout(cyc, j, k, t);
		    return TRUE;
		}
		j = k;
	    } while ((k = l[j].next) != 0);
	}
    }
    return FALSE;
}

/*
 * return the time remaining before a callout expires
 */
Int CallOut::remaining(Uint t, unsigned short *m)
{
    Uint time;
    unsigned short mtime;

    time = cotime(&mtime);

    if (t != 0) {
	t += timediff;
	if (*m == 0xffff) {
	    if (t > time) {
		return t - time;
	    }
	} else if (t == time && *m > mtime) {
	    *m -= mtime;
	} else if (t > time) {
	    if (*m < mtime) {
		--t;
		*m += 1000;
	    }
	    *m -= mtime;
	    return t - time;
	} else {
	    *m = 0xffff;
	}
    }

    return 0;
}

/*
 * remove a callout
 */
void CallOut::del(unsigned int oindex, unsigned int handle, Uint t,
		  unsigned int m)
{
    CallOut *l;

    if (m == 0xffff) {
	/*
	 * try to find the callout in the cyclic buffer
	 */
	if (t > timestamp && t < timestamp + CYCBUF_SIZE &&
	    rmshort(&cycbuf[t & CYCBUF_MASK], oindex, handle, t)) {
	    return;
	}
    }

    if (t <= timestamp) {
	/*
	 * possible immediate callout
	 */
	if (rmshort(&immediate, oindex, handle, 0) ||
	    rmshort(&running, oindex, handle, 0)) {
	    return;
	}
    }

    /*
     * Not found in the cyclic buffer; it <must> be in the queue.
     */
    l = cotab;
    for (;;) {
# ifdef DEBUG
	if (l == cotab + queuebrk) {
	    fatal("failed to remove callout");
	}
# endif
	if (l->oindex == oindex && l->handle == handle) {
	    dequeue(l - cotab);
	    return;
	}
	l++;
    }
}

/*
 * adjust callout delays in array
 */
void CallOut::list(Array *a)
{
    Value *v, *w;
    unsigned short i;
    Uint t;
    unsigned short m;
    Float flt1, flt2;

    for (i = a->size, v = a->elts; i != 0; --i, v++) {
	w = &v->array->elts[2];
	if (w->type == T_INT) {
	    t = w->number;
	    m = 0xffff;
	} else {
	    GET_FLT(w, flt1);
	    t = flt1.low;
	    m = flt1.high;
	}
	t = remaining(t, &m);
	if (m == 0xffff) {
	    PUT_INTVAL(w, t);
	} else {
	    Float::itof(t, &flt1);
	    Float::itof(m, &flt2);
	    flt2.mult(thousandth);
	    flt1.add(flt2);
	    PUT_FLTVAL(w, flt1);
	}
    }
}

/*
 * collect callouts to run next
 */
void CallOut::expire()
{
    CallOut *co, *first, *last;
    uindex handle, oindex, i, *cyc;
    Uint t;
    unsigned short m;

    t = P_mtime(&m) - timediff;
    if ((timeout != 0 && timeout <= t) ||
	(queuebrk != 0 &&
	 (cotab[0].time < t || (cotab[0].time == t && cotab[0].mtime <= m)))) {
	while (timestamp < t) {
	    timestamp++;

	    /*
	     * from queue
	     */
	    while (queuebrk != 0 && cotab[0].time < timestamp) {
		handle = cotab[0].handle;
		oindex = cotab[0].oindex;
		dequeue(0);
		co = newcallout(&immediate, 0);
		co->handle = handle;
		co->oindex = oindex;
	    }

	    /*
	     * from cyclic buffer list
	     */
	    cyc = &cycbuf[timestamp & CYCBUF_MASK];
	    i = *cyc;
	    if (i != 0) {
		*cyc = 0;
		if (immediate == 0) {
		    immediate = i;
		} else {
		    first = &cotab[immediate];
		    last = (first->count == 1) ? first : &cotab[first->last];
		    last->next = i;
		    first->count += cotab[i].count;
		    first->last = (cotab[i].count == 1) ? i : cotab[i].last;
		}
		nzero += cotab[i].count;
	    }
	}

	/*
	 * from queue
	 */
	while (queuebrk != 0 &&
	       (cotab[0].time < t ||
		(cotab[0].time == t && cotab[0].mtime <= m))) {
	    handle = cotab[0].handle;
	    oindex = cotab[0].oindex;
	    dequeue(0);
	    co = newcallout(&immediate, 0);
	    co->handle = handle;
	    co->oindex = oindex;
	}

	if (timeout <= timestamp) {
	    if (nshort != nzero) {
		for (t = timestamp; cycbuf[t & CYCBUF_MASK] == 0; t++) ;
		timeout = t;
	    } else {
		timeout = 0;
	    }
	}
    }

    /* handle swaprate */
    while (swaptime < t) {
	++swaptime;
	::swaprate1 -= swapped1[swaptime % SWPERIOD];
	swapped1[swaptime % SWPERIOD] = 0;
	if (swaptime % 5 == 0) {
	    ::swaprate5 -= swapped5[swaptime % (5 * SWPERIOD) / 5];
	    swapped5[swaptime % (5 * SWPERIOD) / 5] = 0;
	}
    }
}

/*
 * call expired callouts
 */
void CallOut::call(Frame *f)
{
    uindex i, handle;
    Object *obj;
    String *str;
    int nargs;

    if (running == 0) {
	expire();
	running = immediate;
	immediate = 0;
    }

    if (running != 0) {
	/*
	 * callouts to do
	 */
	while ((i=running) != 0) {
	    handle = cotab[i].handle;
	    obj = OBJ(cotab[i].oindex);
	    freecallout(&running, i, i, 0);

	    try {
		ErrorContext::push((ErrorContext::Handler) errhandler);
		str = obj->dataspace()->callOut(handle, f, &nargs);
		if (f->call(obj, (Array *) NULL, str->text, str->len, TRUE,
			    nargs)) {
		    /* function exists */
		    (f->sp++)->del();
		}
		(f->sp++)->string->del();
		ErrorContext::pop();
	    } catch (...) { }
	    endtask();
	}
    }
}

/*
 * give information about callouts
 */
void CallOut::info(uindex *n1, uindex *n2)
{
    *n1 = nshort;
    *n2 = queuebrk;
}

/*
 * return the time until the next timeout
 */
Uint CallOut::delay(Uint rtime, unsigned int rmtime, unsigned short *mtime)
{
    Uint t;
    unsigned short m;

    if (nzero != 0) {
	/* immediate */
	*mtime = 0;
	return 0;
    }
    if ((rtime | timeout | queuebrk) == 0) {
	/* infinite */
	*mtime = 0xffff;
	return 0;
    }
    if (rtime != 0) {
	rtime -= timediff;
    }
    if (timeout != 0 && (rtime == 0 || timeout <= rtime)) {
	rtime = timeout;
	rmtime = 0;
    }
    if (queuebrk != 0 &&
	(rtime == 0 || cotab[0].time < rtime ||
	 (cotab[0].time == rtime && cotab[0].mtime <= rmtime))) {
	rtime = cotab[0].time;
	rmtime = cotab[0].mtime;
    }
    if (rtime != 0) {
	rtime += timediff;
    }

    t = cotime(&m);
    ::cotime = 0;
    if (t > rtime || (t == rtime && m >= rmtime)) {
	/* immediate */
	*mtime = 0;
	return 0;
    }
    if (m > rmtime) {
	m -= 1000;
	t++;
    }
    *mtime = rmtime - m;
    return rtime - t;
}

/*
 * keep track of the number of objects swapped out
 */
void CallOut::swapcount(unsigned int count)
{
    ::swaprate1 += count;
    ::swaprate5 += count;
    swapped1[swaptime % SWPERIOD] += count;
    swapped5[swaptime % (SWPERIOD * 5) / 5] += count;
    ::cotime = 0;
}

/*
 * return the number of objects swapped out per minute
 */
long CallOut::swaprate1()
{
    return ::swaprate1;
}

/*
 * return the number of objects swapped out per 5 minutes
 */
long CallOut::swaprate5()
{
    return ::swaprate5;
}


struct CallOutHeader {
    uindex cotabsz;		/* callout table size */
    uindex queuebrk;		/* queue brk */
    uindex cycbrk;		/* cyclic buffer brk */
    uindex flist;		/* free list index */
    uindex nshort;		/* # of short-term callouts */
    uindex running;		/* running callouts */
    uindex immediate;		/* immediate callouts list */
    unsigned short hstamp;	/* timestamp high word */
    unsigned short hdiff;	/* timediff high word */
    Uint timestamp;		/* time the last alarm came */
    Uint timediff;		/* accumulated time difference */
};

static char dh_layout[] = "uuuuuuussii";

/*
 * dump callout table
 */
bool CallOut::save(int fd)
{
    CallOutHeader dh;
    unsigned short m;

    /* update timestamp */
    cotime(&m);
    ::cotime = 0;

    /* fill in header */
    dh.cotabsz = cotabsz;
    dh.queuebrk = queuebrk;
    dh.cycbrk = cycbrk;
    dh.flist = flist;
    dh.nshort = nshort;
    dh.running = running;
    dh.immediate = immediate;
    dh.hstamp = 0;
    dh.hdiff = 0;
    dh.timestamp = timestamp;
    dh.timediff = timediff;

    /* write header and callouts */
    return (Swap::write(fd, &dh, sizeof(CallOutHeader)) &&
	    (queuebrk == 0 ||
	     Swap::write(fd, cotab, queuebrk * sizeof(CallOut))) &&
	    (cycbrk == cotabsz ||
	     Swap::write(fd, cotab + cycbrk,
			 (cotabsz - cycbrk) * sizeof(CallOut))) &&
	    Swap::write(fd, cycbuf, CYCBUF_SIZE * sizeof(uindex)));
}

/*
 * restore callout table
 */
void CallOut::restore(int fd, Uint t)
{
    CallOutHeader dh;
    uindex n, i, offset;
    CallOut *co;
    uindex *cb;
    uindex buffer[CYCBUF_SIZE];

    /* read and check header */
    timediff = t;

    conf_dread(fd, (char *) &dh, dh_layout, (Uint) 1);
    queuebrk = dh.queuebrk;
    offset = cotabsz - dh.cotabsz;
    cycbrk = dh.cycbrk + offset;
    flist = dh.flist;
    nshort = dh.nshort;
    running = dh.running;
    immediate = dh.immediate;
    timestamp = dh.timestamp;
    t = 0;

    timestamp += t;
    timediff -= timestamp;
    if (queuebrk > cycbrk || cycbrk == 0) {
	error("Restored too many callouts");
    }

    /* read tables */
    n = queuebrk + cotabsz - cycbrk;
    if (n != 0) {
	conf_dread(fd, (char *) cotab, CO_LAYOUT, (Uint) queuebrk);
	conf_dread(fd, (char *) (cotab + cycbrk), CO_LAYOUT,
		   (Uint) (cotabsz - cycbrk));

	for (co = cotab, i = queuebrk; i != 0; co++, --i) {
	    co->time += t;
	}
    }
    conf_dread(fd, (char *) buffer, "u", (Uint) CYCBUF_SIZE);

    /* cycle around cyclic buffer */
    t &= CYCBUF_MASK;
    memcpy(cycbuf + t, buffer,
	   (unsigned int) (CYCBUF_SIZE - t) * sizeof(uindex));
    memcpy(cycbuf, buffer + CYCBUF_SIZE - t, (unsigned int) t * sizeof(uindex));

    nzero = 0;
    if (running != 0) {
	running += offset;
	nzero += cotab[running].count;
    }
    if (immediate != 0) {
	immediate += offset;
	nzero += cotab[immediate].count;
    }

    if (offset != 0) {
	/* patch callout references */
	if (flist != 0) {
	    flist += offset;
	}
	for (i = CYCBUF_SIZE, cb = cycbuf; i > 0; --i, cb++) {
	    if (*cb != 0) {
		*cb += offset;
	    }
	}
	for (i = cotabsz - cycbrk, co = cotab + cycbrk; i > 0; --i, co++) {
	    if (co->prev != 0) {
		co->prev += offset;
	    }
	    if (co->next != 0) {
		co->next += offset;
	    }
	}
    }

    /* restart callouts */
    if (nshort != nzero) {
	for (t = timestamp; cycbuf[t & CYCBUF_MASK] == 0; t++) ;
	timeout = t;
    }
}
