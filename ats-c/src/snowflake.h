#ifndef ATS_SNOWFLAKE_H_INCLUDED
#define ATS_SNOWFLAKE_H_INCLUDED

#include <stdint.h>

/*
 * Shared snowflake state. Must be created in kore_parent_configure() before
 * workers fork: PHP's extension kept one state in shared memory too, so IDs
 * stay unique across FPM/Kore workers using the same WorkerId.
 */
int		ats_snowflake_init(void);
uint64_t	ats_next_id(void);

#endif
