#ifndef ATS_MD5_H_INCLUDED
#define ATS_MD5_H_INCLUDED

#include <stddef.h>

/* Lowercase hex, NUL terminated. md5($user_agent . $visitor_ip) in PHP. */
void	ats_md5_hex(const void *data, size_t len, char out[33]);

#endif
