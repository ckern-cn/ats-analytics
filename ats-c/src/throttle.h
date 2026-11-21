#ifndef ATS_THROTTLE_H_INCLUDED
#define ATS_THROTTLE_H_INCLUDED

/* Shared-memory token buckets, created in the parent before workers fork. */
int	ats_throttle_init(void);

/*
 * Allow one request for (scope, ip). Returns 1 when allowed, 0 when the
 * bucket is empty. capacity/period define a token bucket refilled linearly.
 */
int	ats_throttle_allow(const char *scope, const char *ip,
	    int capacity, int period);

#endif
