#ifndef ATS_ROUTER_H_INCLUDED
#define ATS_ROUTER_H_INCLUDED

#include <kore/http.h>

/* Kore route handler: CI4 Router::handle() + Filters::run('before'). */
int	ats_dispatch(struct http_request *);

#endif