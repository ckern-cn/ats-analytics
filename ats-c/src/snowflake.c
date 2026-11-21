/*
 * Port of yitter/IdGenerator's snowflake.c as configured on the production
 * host (/etc/php.d/30-snowdrift.ini):
 *
 *	snowdrift.Method		1
 *	snowdrift.BaseTime		1722484800000
 *	snowdrift.WorkerId		1
 *	snowdrift.WorkerIdBitLength	6
 *	snowdrift.SeqBitLength		6
 *	snowdrift.MinSeqNumber		5
 *	snowdrift.TopOverCostCount	2000
 *
 * Only the parts reachable with Method=1 are kept. The lock is the same
 * spinlock idea, implemented with GCC atomics on a MAP_SHARED page so both
 * Kore workers contend on one counter.
 */

#include <sys/mman.h>
#include <sys/time.h>

#include <sched.h>
#include <time.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <kore/kore.h>

#include "snowflake.h"

struct snowflake {
	uint8_t		Method;
	uint64_t	BaseTime;
	uint16_t	WorkerId;
	uint8_t		WorkerIdBitLength;
	uint8_t		SeqBitLength;
	uint32_t	MaxSeqNumber;
	uint32_t	MinSeqNumber;
	uint32_t	TopOverCostCount;

	uint8_t		TimestampShift;
	uint32_t	CurrentSeqNumber;
	int64_t		LastTimeTick;
	int64_t		TurnBackTimeTick;
	uint8_t		TurnBackIndex;
	uint8_t		IsOverCost;
	uint32_t	OverCostCountInOneTerm;

	volatile unsigned int Lock;
};

static struct snowflake	*sf;

static uint64_t
sys_time_ms(void)
{
	struct timeval	tv;

	gettimeofday(&tv, NULL);

	return ((uint64_t)tv.tv_sec * 1000 + (uint64_t)tv.tv_usec / 1000);
}

static uint64_t
current_tick(void)
{
	return (sys_time_ms() - sf->BaseTime);
}

static void
lock(void)
{
	while (__sync_lock_test_and_set(&sf->Lock, 1)) {
		while (sf->Lock != 0)
			sched_yield();
	}
}

static void
unlock(void)
{
	__sync_lock_release(&sf->Lock);
}

static uint64_t
calc_id(void)
{
	uint64_t	result;

	result = ((uint64_t)sf->LastTimeTick << sf->TimestampShift) +
	    ((uint64_t)sf->WorkerId << sf->SeqBitLength) +
	    (uint64_t)sf->CurrentSeqNumber;
	sf->CurrentSeqNumber++;

	return (result);
}

static uint64_t
calc_turn_back_id(void)
{
	uint64_t	result;

	result = ((uint64_t)sf->LastTimeTick << sf->TimestampShift) +
	    ((uint64_t)sf->WorkerId << sf->SeqBitLength) +
	    (uint64_t)sf->TurnBackIndex;
	sf->TurnBackTimeTick--;

	return (result);
}

static uint64_t
next_time_tick(void)
{
	uint64_t	tick;
	struct timespec	delay;

	delay.tv_sec = 0;
	delay.tv_nsec = 500000;

	for (;;) {
		tick = current_tick();
		if (tick > (uint64_t)sf->LastTimeTick)
			break;
		nanosleep(&delay, NULL);
	}

	return (tick);
}

static uint64_t
next_over_cost_id(void)
{
	uint64_t	tick;

	tick = current_tick();

	if (tick > (uint64_t)sf->LastTimeTick) {
		sf->LastTimeTick = (int64_t)tick;
		sf->CurrentSeqNumber = sf->MinSeqNumber;
		sf->IsOverCost = 0;
		sf->OverCostCountInOneTerm = 0;
		return (calc_id());
	}

	if (sf->OverCostCountInOneTerm > sf->TopOverCostCount) {
		sf->LastTimeTick = (int64_t)next_time_tick();
		sf->CurrentSeqNumber = sf->MinSeqNumber;
		sf->IsOverCost = 0;
		sf->OverCostCountInOneTerm = 0;
		return (calc_id());
	}

	if (sf->CurrentSeqNumber > sf->MaxSeqNumber) {
		sf->LastTimeTick++;
		sf->CurrentSeqNumber = sf->MinSeqNumber;
		sf->IsOverCost = 1;
		sf->OverCostCountInOneTerm++;
		return (calc_id());
	}

	return (calc_id());
}

static uint64_t
next_normal_id(void)
{
	uint64_t	tick;

	tick = current_tick();

	if (tick < (uint64_t)sf->LastTimeTick) {
		if (sf->TurnBackTimeTick < 1) {
			sf->TurnBackTimeTick = sf->LastTimeTick - 1;
			sf->TurnBackIndex++;
			if (sf->TurnBackIndex > 4)
				sf->TurnBackIndex = 1;
		}

		return (calc_turn_back_id());
	}

	if (sf->TurnBackTimeTick > 0)
		sf->TurnBackTimeTick = 0;

	if (tick > (uint64_t)sf->LastTimeTick) {
		sf->LastTimeTick = (int64_t)tick;
		sf->CurrentSeqNumber = sf->MinSeqNumber;
		return (calc_id());
	}

	if (sf->CurrentSeqNumber > sf->MaxSeqNumber) {
		sf->LastTimeTick++;
		sf->CurrentSeqNumber = sf->MinSeqNumber;
		sf->IsOverCost = 1;
		sf->OverCostCountInOneTerm = 1;
		return (calc_id());
	}

	return (calc_id());
}

int
ats_snowflake_init(void)
{
	void	*p;

	p = mmap(NULL, sizeof(*sf), PROT_READ | PROT_WRITE,
	    MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED) {
		kore_log(LOG_ERR, "snowflake: mmap failed");
		return (-1);
	}

	sf = p;
	memset(sf, 0, sizeof(*sf));
	sf->Method = 1;
	sf->BaseTime = 1722484800000ULL;
	sf->WorkerId = 1;
	sf->WorkerIdBitLength = 6;
	sf->SeqBitLength = 6;
	sf->MaxSeqNumber = (1U << 6) - 1;	/* 63 */
	sf->MinSeqNumber = 5;
	sf->TopOverCostCount = 2000;
	sf->TimestampShift = 6 + 6;
	sf->CurrentSeqNumber = sf->MinSeqNumber;

	return (0);
}

uint64_t
ats_next_id(void)
{
	uint64_t	id;

	lock();
	if (sf->IsOverCost != 0)
		id = next_over_cost_id();
	else
		id = next_normal_id();
	unlock();

	return (id);
}
