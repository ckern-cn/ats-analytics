#ifndef ATS_HANDLERS_H_INCLUDED
#define ATS_HANDLERS_H_INCLUDED

#include <stddef.h>

#include <kore/http.h>

#include "util.h"

int	ats_h_wru(struct http_request *);
int	ats_h_rv(struct http_request *);
int	ats_h_pv(struct http_request *);
int	ats_h_dv(struct http_request *);
int	ats_h_create(struct http_request *);
int	ats_h_do_create(struct http_request *);
int	ats_h_edit(struct http_request *);
int	ats_h_do_update(struct http_request *);
int	ats_h_do_delete(struct http_request *);
int	ats_h_export(struct http_request *);
int	ats_h_login(struct http_request *);
int	ats_h_do_login(struct http_request *);
int	ats_h_logout(struct http_request *);
int	ats_h_lang(struct http_request *);
int	ats_h_health(struct http_request *);

/* Shared response helpers (also used by the router). */
void	ats_send_html(struct http_request *, struct ats_sb *);
void	ats_send_js(struct http_request *, struct ats_sb *);
void	ats_send_redirect(struct http_request *, int, const char *);
void	ats_send_status(struct http_request *, int);
int	ats_require_auth(struct http_request *);

/*
 * 1-based path segment, still percent-encoded. Returns a kore_free()-able
 * copy ("" when out of range).
 */
char	*ats_segment(struct http_request *, int);

/* nginx X-Real-IP when it is a valid address, otherwise the peer address. */
void	ats_client_ip(struct http_request *, char *, size_t);

#endif
