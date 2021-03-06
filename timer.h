/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* Copyright (C) Uppsala University
 *
 * This file is distributed under the terms of the GNU general Public
 * License (GPL), see the file LICENSE
 *
 * Author: Erik Nordström, <erik.nordstrom@gmail.com>
 */
#ifndef _TIMER_H
#define _TIMER_H

#ifdef KERNEL26
#include <linux/jiffies.h>
#include <asm/div64.h>
#endif

typedef unsigned long usecs_t;

#ifdef NS2
#include <stdarg.h>

#include <object.h>
#include <agent.h>
#include <trace.h>
#include <scheduler.h>

class DSRUU;

typedef void (DSRUU::*fct_t) (unsigned long data);

class DSRUUTimer:public TimerHandler {
      public:
	DSRUUTimer(DSRUU * a):TimerHandler() {
		a_ = a;
		name_ = "NoName";
	} DSRUUTimer(DSRUU * a, char *name):TimerHandler() {
		a_ = a;
		name_ = name;
	}
	fct_t function;
	double expires;
	unsigned long data;
	void init(double expires_, fct_t fct_, unsigned long data_) {
		expires = expires_;
		data = data_;
		function = fct_;
	}
	char *get_name() {
		return name_;
	}
      protected:
	virtual void expire(Event * e);
	DSRUU *a_;
	char *name_;
};

static inline void gettime(struct timeval *tv)
{
	double now, usecs;

	/* Timeval is required, timezone is ignored */
	if (!tv)
		return;

	now = Scheduler::instance().clock();

	tv->tv_sec = (long)now;	/* Removes decimal part */
	usecs = (now - tv->tv_sec) * 1000000;
	tv->tv_usec = (long)usecs;
}

#else

#include <linux/timer.h>

typedef struct timer_list DSRUUTimer;

static inline void set_timer(DSRUUTimer * t, struct timeval *expires)
{
	unsigned long exp_jiffies;
#ifdef KERNEL26
	exp_jiffies = timeval_to_jiffies(expires);
#else
	/* Hmm might overlflow? */
	unsigned long tmp;
	tmp = expires->tv_usec * HZ;
	tmp /= 1000000;
	exp_jiffies = expires->tv_sec * HZ + tmp;
#endif
	if (timer_pending(t))
		mod_timer(t, exp_jiffies);
	else {
		t->expires = exp_jiffies;
		add_timer(t);
	}
}

static inline void gettime(struct timeval *tv)
{
	unsigned long now = jiffies;

	if (!tv)
		return;
#ifdef KERNEL26
	jiffies_to_timeval(now, tv);
#else
	tv->tv_sec = now / HZ;

	tv->tv_usec = (now % HZ) * 1000000l / HZ;
#endif
}
#endif				/* NS2 */

static inline char *print_timeval(struct timeval *tv)
{
	static char buf[56][56];
	static int n = 0;

	n = (n + 1) % 2;

	snprintf(buf[n], sizeof(buf), "%ld:%02ld:%03ld", tv->tv_sec / 60,
		 tv->tv_sec % 60, tv->tv_usec / 1000);

	return buf[n];
}

/* These functions may overflow (although unlikely)... Should probably be
 * improtved in the future */
static inline long timeval_diff(struct timeval *tv1, struct timeval *tv2)
{
	if (!tv1 || !tv2)
		return 0;
	else
		return ((tv1->tv_sec - tv2->tv_sec) * 1000000 +
			tv1->tv_usec - tv2->tv_usec);
}

static inline int timeval_add_usecs(struct timeval *tv, usecs_t usecs)
{
	long add;

	if (!tv)
		return -1;

	add = tv->tv_usec + usecs;
	tv->tv_sec += add / 1000000;
	tv->tv_usec = add % 1000000;

	return 0;
}
#endif				/* _TIMER_H */
