/*
 * Shared-memory token buckets.
 *
 * The table is MAP_SHARED and created before workers fork, so all workers see
 * one bucket per (scope, client). Lookups are open-addressed with a spinlock;
 * the critical section is a few arithmetic operations.
 */

#include <sys/mman.h>

#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <kore/kore.h>

#include "md5.h"
#include "throttle.h"

#define BUCKETS		8192

struct bucket {
	char		key[64];
	double		tokens;
	double		capacity;
	double		rate;
	long long	saved_at;
	int		used;
};

struct table {
	struct bucket	b[BUCKETS];
	volatile unsigned int lock;
};

static struct table	*tbl;

int
ats_throttle_init(void)
{
	void	*p;

	p = mmap(NULL, sizeof(*tbl), PROT_READ | PROT_WRITE,
	    MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED) {
		kore_log(LOG_ERR, "throttle: mmap failed");
		return (-1);
	}

	tbl = p;
	memset(tbl, 0, sizeof(*tbl));

	return (0);
}

static void
lock(void)
{
	while (__sync_lock_test_and_set(&tbl->lock, 1)) {
		while (tbl->lock != 0)
			sched_yield();
	}
}

static void
unlock(void)
{
	__sync_lock_release(&tbl->lock);
}

static unsigned long
hash_key(const char *s)
{
	unsigned long	h;

	h = 5381;
	for (; *s != '\0'; s++)
		h = ((h << 5) + h) + (unsigned char)*s;

	return (h);
}

static struct bucket *
lookup(const char *key, int *fresh)
{
	unsigned int	 i, step;
	struct bucket	*b;
	unsigned long	 h;

	h = hash_key(key);
	step = 1 + (unsigned int)(h % (BUCKETS - 1));

	for (i = 0; i < BUCKETS; i++) {
		b = &tbl->b[(h + i * step) % BUCKETS];

		if (!b->used)
			return (b);
		if (!strcmp(b->key, key))
			return (b);
	}

	/* Full table: reuse the first probe slot. */
	*fresh = 1;
	return (&tbl->b[h % BUCKETS]);
}

int
ats_throttle_allow(const char *scope, const char *ip, int capacity,
    int period)
{
	char		 raw[256], key[33];
	struct bucket	*b;
	long long	 now;
	int		 fresh = 0, ok;

	if (capacity < 1)
		capacity = 1;
	if (period < 1)
		period = 1;

	(void)snprintf(raw, sizeof(raw), "%s:%s",
	    scope != NULL ? scope : "", ip != NULL ? ip : "");
	ats_md5_hex(raw, strlen(raw), key);

	now = (long long)time(NULL);
	lock();

	b = lookup(key, &fresh);

	if (!b->used || fresh || now - b->saved_at >= period ||
	    b->capacity != (double)capacity) {
		(void)snprintf(b->key, sizeof(b->key), "%s", key);
		b->used = 1;
		b->capacity = (double)capacity;
		b->rate = (double)capacity / (double)period;
		b->tokens = (double)capacity - 1.0;
		b->saved_at = now;
		ok = 1;
	} else {
		b->tokens += b->rate * (double)(now - b->saved_at);
		if (b->tokens > b->capacity)
			b->tokens = b->capacity;

		if (b->tokens >= 1.0) {
			b->tokens -= 1.0;
			b->saved_at = now;
			ok = 1;
		} else {
			ok = 0;
		}
	}

	unlock();

	return (ok);
}
