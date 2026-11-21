#ifndef ATS_WRU_H_INCLUDED
#define ATS_WRU_H_INCLUDED

/*
 * The /wru/ endpoint accepts a record id from a cross-origin script. A bare
 * Snowflake id is guessable, so the server emits an HMAC over the id in the
 * tracking snippet and verifies it before updating the row.
 */
void	ats_wru_sign(const char *id, char out[33]);
int	ats_wru_verify(const char *id, const char *token);

#endif
