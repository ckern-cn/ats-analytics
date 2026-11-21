#ifndef ATS_VIEWS_H_INCLUDED
#define ATS_VIEWS_H_INCLUDED

#include <stdint.h>

#include <kore/http.h>

#include "i18n.h"
#include "util.h"

struct ats_app_row {
	long long	id;
	const char	*name;
	const char	*host;
};

struct ats_visit_row {
	const char	*page_url;
	const char	*referring_url;
	const char	*visitor_ip;
	const char	*country;
	const char	*browser;
	const char	*operating_system;
	const char	*visit_time;	/* already shifted to local time */
};

struct ats_summary {
	long long	visitors;
	long long	page_views;
	long long	s_today;
	long long	s_yesterday;
	long long	s_last_7_days;
	long long	s_last_30_days;
	long long	uv_today;
};

struct ats_day_count {
	char		date[11];	/* YYYY-MM-DD, local */
	long long	count;
};

struct ats_top_item {
	const char	*label;
	long long	count;
};

struct ats_country_count {
	const char	*country;
	long long	count;
};

struct ats_dashboard {
	const struct ats_app_row	*apps;
	int				 n_apps;
	const char			*cur_app_id;

	struct ats_summary		 s;

	const struct ats_day_count	*days;
	int				 n_days;
	const struct ats_top_item	*pages;
	int				 n_pages;
	const struct ats_top_item	*refs;
	int				 n_refs;
	const struct ats_top_item	*countries;
	int				 n_countries;

	const struct ats_visit_row	*rows;
	int				 n_rows;
	int				 current_page;
	int				 page_count;
};

void	view_error(struct ats_sb *, enum ats_locale, int status,
	    const char *title, const char *message);
void	view_js_msg(struct ats_sb *, const char *msg);
void	view_js_api_wru(struct ats_sb *, uint64_t id, const char *token);
void	view_js_res_pv(struct ats_sb *, enum ats_locale, const char *container,
	    long long pv_total, const struct ats_country_count *rows, int n,
	    long long others);
void	view_login(struct ats_sb *, enum ats_locale, const char *ref,
	    const char *error);
void	view_app_form(struct ats_sb *, struct http_request *,
	    enum ats_locale, const char *title, const char *action,
	    const char *id, const char *name, const char *hosts,
	    const char *remark, const char *error);
void	view_create_success(struct ats_sb *, struct http_request *,
	    enum ats_locale, uint64_t id, const char *name, const char *host);
void	view_data_view(struct ats_sb *, struct http_request *,
	    enum ats_locale, const struct ats_dashboard *);

#endif
