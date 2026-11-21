#ifndef ATS_GEOIP_H_INCLUDED
#define ATS_GEOIP_H_INCLUDED

#include <stddef.h>

/*
 * Returns a lowercase ISO country code, or "" when the address is not in the
 * database. HK/TW/MO are folded to "cn" by the caller, as AppCtrl does.
 * The returned pointer is to a per-call static buffer.
 */
const char	*ats_geoip_country(const char *ip);

#endif
