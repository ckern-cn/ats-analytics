/*
 * HTML/JS rendering.
 *
 * Dynamic values are escaped (HTML entities for text and attributes, a
 * single-quoted-JS-string escaper for inline scripts). All user-facing text
 * comes from i18n.c; this file only decides layout.
 */

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "ats.h"
#include "i18n.h"
#include "util.h"
#include "views.h"

static void	pager_nav(struct ats_sb *, struct http_request *,
		    enum ats_locale, int, int);
static void	pager_url(struct ats_sb *, struct http_request *, int);
static void	sb_qenc(struct ats_sb *, const char *);
static void	sb_upper(struct ats_sb *, const char *);
static void	sb_page_head(struct ats_sb *, enum ats_locale, const char *);
static void	sb_page_foot(struct ats_sb *);
static void	sb_flag(struct ats_sb *, const char *);
static int	country_ok(const char *);
static void	sb_chart(struct ats_sb *, const struct ats_dashboard *);
static void	sb_toplist(struct ats_sb *, enum ats_locale,
		    const struct ats_top_item *, int, int);
static void	sb_tracker_snippets(struct ats_sb *, enum ats_locale,
		    const char *);
static void	sb_ua_text(struct ats_sb *, enum ats_locale, const char *);
static void	sb_logout_button(struct ats_sb *, enum ats_locale);
static void	sb_lang_switch(struct ats_sb *, struct http_request *,
		    enum ats_locale, const char *);

void
view_js_msg(struct ats_sb *sb, const char *msg)
{
	ats_sb_adds(sb, "/*console.log(\"");
	ats_sb_js(sb, msg);
	ats_sb_adds(sb, "\");*/\n");
}

void
view_js_api_wru(struct ats_sb *sb, uint64_t id, const char *token)
{
	ats_sb_adds(sb, "!function(){var e=document.createElement(\"script\");"
	    "e.async=!0;e.src=\"");
	ats_sb_adds(sb, ats_base_url());
	ats_sb_addf(sb, "/wru/%llu?ref_url=\"+encodeURIComponent("
	    "document.referrer)+\"&page_url=\"+encodeURIComponent("
	    "window.location.href)+", (unsigned long long)id);
	ats_sb_adds(sb, "\"&t=");
	ats_sb_adds(sb, token);
	ats_sb_adds(sb, "\";var t=document.getElementsByTagName(\"script\")[0];"
	    "t.parentNode.insertBefore(e,t);}();\n");
}

static int
country_ok(const char *c)
{
	if (c == NULL || strlen(c) != 2)
		return (0);

	return (islower((unsigned char)c[0]) && islower((unsigned char)c[1]));
}

static void
sb_ua_text(struct ats_sb *sb, enum ats_locale locale, const char *s)
{
	if (s == NULL || *s == '\0' || !strcmp(s, "Unknown Platform"))
		ats_sb_html(sb, ats_i18n(locale, I18N_UNKNOWN));
	else
		ats_sb_html(sb, s);
}

static void
sb_flag(struct ats_sb *sb, const char *country)
{
	const char	*code;

	code = country_ok(country) ? country : "unknown";

	ats_sb_adds(sb, "<img class=\"flag\" loading=\"lazy\" src=\"/res/flags/");
	ats_sb_html(sb, code);
	ats_sb_adds(sb, ".svg\" alt=\"");
	if (country_ok(country))
		sb_upper(sb, country);
	ats_sb_adds(sb, "\">");
}

void
view_js_res_pv(struct ats_sb *sb, enum ats_locale locale,
    const char *container, long long pv_total,
    const struct ats_country_count *rows, int n, long long others)
{
	int	i;

	ats_sb_adds(sb, "(function(){var root=document.getElementById(\"");
	ats_sb_js(sb, container);
	ats_sb_adds(sb, "\");if(!root)return;var data=[");
	for (i = 0; i < n; i++) {
		if (i > 0)
			ats_sb_adds(sb, ",");
		ats_sb_adds(sb, "[\"");
		ats_sb_js(sb, country_ok(rows[i].country) ? rows[i].country :
		    "unknown");
		ats_sb_addf(sb, "\",%lld]", rows[i].count);
	}
	ats_sb_addf(sb, "];var total=%lld,others=%lld;", pv_total, others);
	ats_sb_adds(sb,
	    "var box=document.createElement('div');"
	    "box.style.cssText='font:13px/1.5 -apple-system,BlinkMacSystemFont,"
	    "\"Segoe UI\",Roboto,Arial,sans-serif;color:#1d2430;display:flex;"
	    "flex-wrap:wrap;align-items:center;gap:10px 18px;"
	    "background:#fff;border:1px solid #e2e6ec;border-radius:10px;"
	    "padding:10px 14px;width:fit-content;max-width:100%;';"
	    "root.innerHTML='';root.appendChild(box);"
	    "function add(code,count,label,totalRow){"
	    "var el=document.createElement('span');"
	    "el.style.cssText='display:inline-flex;align-items:center;gap:6px;"
	    "white-space:nowrap;';"
	    "var img=document.createElement('img');"
	    "img.src='");
	ats_sb_adds(sb, ats_base_url());
	ats_sb_adds(sb,
	    "/res/'+(code==='globe'?'globe.svg':'flags/'+code+'.svg');"
	    "img.alt='';img.loading='lazy';"
	    "img.style.cssText='width:18px;height:12px;object-fit:cover;"
	    "border-radius:1px;';"
	    "var txt=document.createElement('span');"
	    "txt.textContent=label+' '+count;"
	    "if(totalRow){txt.style.fontWeight='700';txt.style.fontSize='15px';"
	    "img.style.width='22px';img.style.height='15px';}"
	    "el.appendChild(img);el.appendChild(txt);box.appendChild(el);}"
	    "add('globe',total,'");
	ats_sb_js(sb, ats_i18n(locale, I18N_PV_VIEWS));
	ats_sb_adds(sb, "',true);for(var i=0;i<data.length;i++){"
	    "add(data[i][0],data[i][1],data[i][0]==='unknown'?'");
	ats_sb_js(sb, ats_i18n(locale, I18N_PV_UNKNOWN));
	ats_sb_adds(sb, "':data[i][0].toUpperCase(),false);}"
	    "if(others>0)add('unknown',others,'");
	ats_sb_js(sb, ats_i18n(locale, I18N_PV_OTHERS));
	ats_sb_adds(sb, "',false);})();\n");
}

static void
sb_logout_button(struct ats_sb *sb, enum ats_locale locale)
{
	ats_sb_adds(sb, "<form method=\"post\" action=\"/logout\">"
	    "<input type=\"hidden\" name=\"x\" value=\"1\">"
	    "<button class=\"linkbtn\" type=\"submit\">");
	ats_sb_html(sb, ats_i18n(locale, I18N_NAV_LOGOUT));
	ats_sb_adds(sb, "</button></form>");
}

static void
sb_lang_switch(struct ats_sb *sb, struct http_request *req,
    enum ats_locale locale, const char *ref_override)
{
	struct ats_sb	ref, enc;
	int		i;

	ats_sb_init(&ref);
	if (ref_override != NULL) {
		ats_sb_adds(&ref, ref_override);
	} else if (req != NULL) {
		struct ats_sb	pth;

		ats_sb_init(&pth);
		ats_filter_path(&pth, req->path);
		ats_sb_add(&ref, pth.p, pth.len);
		ats_sb_free(&pth);
		if (ref.len == 0 || ref.p[0] != '/')
			ats_sb_adds(&ref, "/");
		if (req->query_string != NULL && *req->query_string != '\0') {
			ats_sb_adds(&ref, "?");
			ats_sb_adds(&ref, req->query_string);
		}
	} else {
		ats_sb_adds(&ref, "/dv");
	}

	ats_sb_init(&enc);
	ats_php_urlencode(&enc, ats_sb_cstr(&ref));

	ats_sb_adds(sb, "<span class=\"lang-switch\" aria-label=\"");
	ats_sb_html(sb, ats_i18n(locale, I18N_LANG_LABEL));
	ats_sb_adds(sb, "\">");
	for (i = 0; i < ATS_LOCALE_COUNT; i++) {
		if (i > 0)
			ats_sb_adds(sb, " ");
		ats_sb_adds(sb, "<a href=\"/lang/");
		ats_sb_adds(sb, ats_locale_code((enum ats_locale)i));
		ats_sb_adds(sb, "?ref=");
		ats_sb_html(sb, ats_sb_cstr(&enc));
		ats_sb_adds(sb, "\"");
		if (i == (int)locale)
			ats_sb_adds(sb, " class=\"active\"");
		ats_sb_adds(sb, ">");
		ats_sb_html(sb, ats_locale_name((enum ats_locale)i));
		ats_sb_adds(sb, "</a>");
	}
	ats_sb_adds(sb, "</span>");

	ats_sb_free(&ref);
	ats_sb_free(&enc);
}

static void
sb_page_head(struct ats_sb *sb, enum ats_locale locale, const char *title)
{
	ats_sb_adds(sb, "<!DOCTYPE html>\n<html lang=\"");
	ats_sb_adds(sb, ats_locale_code(locale));
	ats_sb_adds(sb, "\">\n<head>\n"
	    "<meta charset=\"utf-8\">\n"
	    "<meta name=\"viewport\" content=\"width=device-width, "
	    "initial-scale=1\">\n"
	    "<meta name=\"robots\" content=\"noindex,nofollow\">\n"
	    "<title>");
	ats_sb_html(sb, title);
	ats_sb_adds(sb, " - ATS</title>\n"
	    "<link rel=\"stylesheet\" href=\"/res/css/ats.css\">\n"
	    "<link rel=\"icon\" href=\"/favicon.ico\" sizes=\"any\">\n"
	    "<link rel=\"icon\" type=\"image/svg+xml\" href=\"/res/icon.svg\">\n"
	    "<link rel=\"apple-touch-icon\" href=\"/res/app-icon-180.png\">\n"
	    "</head>\n<body>\n");
}

static void
sb_page_foot(struct ats_sb *sb)
{
	ats_sb_adds(sb, "</body>\n</html>\n");
}

void
view_error(struct ats_sb *sb, enum ats_locale locale, int status,
    const char *title, const char *message)
{
	sb_page_head(sb, locale, title);
	ats_sb_adds(sb, "<div class=\"wrap\" style=\"padding-top:48px;"
	    "padding-bottom:48px;\"><div class=\"panel\">"
	    "<h1 style=\"margin-top:0;font-size:22px;\">");
	ats_sb_html(sb, title);
	ats_sb_adds(sb, "</h1><p style=\"color:#687385;\">");
	ats_sb_html(sb, message);
	ats_sb_adds(sb, "</p><p><a class=\"btn\" href=\"/dv\">");
	ats_sb_html(sb, ats_i18n(locale, I18N_BACK_DASHBOARD));
	ats_sb_adds(sb, "</a></p></div></div>");
	sb_page_foot(sb);
	(void)status;
}

void
view_login(struct ats_sb *sb, enum ats_locale locale, const char *ref,
    const char *error)
{
	sb_page_head(sb, locale, ats_i18n(locale, I18N_LOGIN_TITLE));
	ats_sb_adds(sb, "<div class=\"login-page\"><div class=\"login-card\">\n"
	    "<div class=\"panel\">\n"
	    "<img class=\"login-icon\" src=\"/res/icon.svg\" width=\"64\" "
	    "height=\"64\" alt=\"ATS\">\n"
	    "<h1>ATS</h1>\n<p class=\"sub\">");
	ats_sb_html(sb, ats_i18n(locale, I18N_LOGIN_SUBTITLE));
	ats_sb_adds(sb, "</p>\n");

	if (error != NULL && *error != '\0') {
		ats_sb_adds(sb, "<div class=\"flash error\">");
		ats_sb_html(sb, error);
		ats_sb_adds(sb, "</div>\n");
	}

	ats_sb_adds(sb, "<form method=\"post\" action=\"/do_login\" "
	    "onsubmit=\"return pwd_crypto()\" accept-charset=\"utf-8\">\n"
	    "<div class=\"field\"><label class=\"label\" for=\"username\">");
	ats_sb_html(sb, ats_i18n(locale, I18N_LOGIN_USERNAME));
	ats_sb_adds(sb, "</label><input class=\"input\" id=\"username\" "
	    "name=\"username\" type=\"text\" autocomplete=\"username\" "
	    "autofocus required></div>\n"
	    "<div class=\"field\"><label class=\"label\" for=\"raw_pwd\">");
	ats_sb_html(sb, ats_i18n(locale, I18N_LOGIN_PASSWORD));
	ats_sb_adds(sb, "</label><input class=\"input\" id=\"raw_pwd\" "
	    "type=\"password\" autocomplete=\"current-password\" required>"
	    "<input type=\"hidden\" id=\"encrypted_pwd\" name=\"password\" "
	    "required></div>\n"
	    "<input type=\"hidden\" name=\"ref\" value=\"");
	ats_sb_html(sb, ref);
	ats_sb_adds(sb, "\">\n"
	    "<div class=\"row\"><noscript><span class=\"help\">");
	ats_sb_html(sb, ats_i18n(locale, I18N_LOGIN_NOSCRIPT));
	ats_sb_adds(sb, "</span></noscript>"
	    "<button class=\"btn btn-primary\" type=\"submit\">");
	ats_sb_html(sb, ats_i18n(locale, I18N_LOGIN_SUBMIT));
	ats_sb_adds(sb, "</button></div>\n"
	    "</form>\n</div>\n"
	    "<p class=\"foot\">");
	ats_sb_html(sb, ats_base_url_host());
	ats_sb_adds(sb, "</p>\n<div class=\"login-lang\">");
	sb_lang_switch(sb, NULL, locale, "/login");
	ats_sb_adds(sb, "</div>\n"
	    "</div></div>\n"
	    "<script src=\"/res/js/sha256.min.js\"></script>\n"
	    "<script>\n"
	    "function pwd_crypto(){\n"
	    "  var raw=document.getElementById('raw_pwd');\n"
	    "  var out=document.getElementById('encrypted_pwd');\n"
	    "  if(!raw.value){return false;}\n"
	    "  out.value=sha256(raw.value);return true;\n"
	    "}\n"
	    "</script>\n");
	sb_page_foot(sb);
}

void
view_app_form(struct ats_sb *sb, struct http_request *req,
    enum ats_locale locale, const char *title, const char *action,
    const char *id, const char *name, const char *hosts, const char *remark,
    const char *error)
{
	sb_page_head(sb, locale, title);
	ats_sb_adds(sb, "<div class=\"topbar\"><div class=\"wrap\">"
	    "<a class=\"brand\" href=\"/dv\"><img class=\"brand-icon\" "
	    "src=\"/res/icon-mark.svg\" alt=\"\">ATS</a>"
	    "<nav class=\"nav\"><a href=\"/dv\">");
	ats_sb_html(sb, ats_i18n(locale, I18N_NAV_DASHBOARD));
	ats_sb_adds(sb, "</a>");
	{
		char lref[64];
		const char *ref;

		if (!strcmp(action, "/do_create")) {
			ref = "/create";
		} else if (!strcmp(action, "/do_update") &&
		    id != NULL && *id != '\0') {
			(void)snprintf(lref, sizeof(lref), "/edit/%s", id);
			ref = lref;
		} else {
			ref = "/dv";
		}
		sb_lang_switch(sb, req, locale, ref);
	}
	sb_logout_button(sb, locale);
	ats_sb_adds(sb, "</nav></div></div>\n"
	    "<div class=\"wrap\" style=\"padding-top:28px;padding-bottom:48px;\">"
	    "<div class=\"panel\">\n<h1 style=\"margin-top:0;font-size:22px;\">");
	ats_sb_html(sb, title);
	ats_sb_adds(sb, "</h1>\n");

	if (error != NULL && *error != '\0') {
		ats_sb_adds(sb, "<div class=\"flash error\">");
		ats_sb_html(sb, error);
		ats_sb_adds(sb, "</div>\n");
	}

	ats_sb_adds(sb, "<form class=\"form\" method=\"post\" action=\"");
	ats_sb_html(sb, action);
	ats_sb_adds(sb, "\">\n");

	if (id != NULL && *id != '\0') {
		ats_sb_adds(sb, "<input type=\"hidden\" name=\"id\" value=\"");
		ats_sb_html(sb, id);
		ats_sb_adds(sb, "\">\n");
	}

	ats_sb_adds(sb, "<div class=\"field\"><label class=\"label\" "
	    "for=\"app_name\">");
	ats_sb_html(sb, ats_i18n(locale, I18N_APP_NAME));
	ats_sb_adds(sb, "</label><input class=\"input\" id=\"app_name\" "
	    "name=\"app_name\" value=\"");
	ats_sb_html(sb, name);
	ats_sb_adds(sb, "\" placeholder=\"");
	ats_sb_html(sb, ats_i18n(locale, I18N_APP_NAME_PLACEHOLDER));
	ats_sb_adds(sb, "\" required></div>\n"
	    "<div class=\"field\"><label class=\"label\" for=\"app_hosts\">");
	ats_sb_html(sb, ats_i18n(locale, I18N_APP_HOSTS));
	ats_sb_adds(sb, "</label><input class=\"input\" id=\"app_hosts\" "
	    "name=\"app_hosts\" value=\"");
	ats_sb_html(sb, hosts);
	ats_sb_adds(sb, "\" placeholder=\"");
	ats_sb_html(sb, ats_i18n(locale, I18N_APP_HOSTS_PLACEHOLDER));
	ats_sb_adds(sb, "\" required><div class=\"help\">");
	ats_sb_html(sb, ats_i18n(locale, I18N_APP_HOSTS_HELP));
	ats_sb_adds(sb, "</div></div>\n"
	    "<div class=\"field\"><label class=\"label\" for=\"remark\">");
	ats_sb_html(sb, ats_i18n(locale, I18N_APP_NOTE));
	ats_sb_adds(sb, "</label><input class=\"input\" id=\"remark\" "
	    "name=\"remark\" value=\"");
	ats_sb_html(sb, remark);
	ats_sb_adds(sb, "\" placeholder=\"");
	ats_sb_html(sb, ats_i18n(locale, I18N_APP_NOTE_PLACEHOLDER));
	ats_sb_adds(sb, "\"></div>\n"
	    "<div class=\"row\" style=\"display:flex;gap:10px;align-items:center;"
	    "\"><button class=\"btn btn-primary\" type=\"submit\">");
	ats_sb_html(sb, ats_i18n(locale, I18N_APP_SAVE));
	ats_sb_adds(sb, "</button><a class=\"btn\" href=\"/dv\">");
	ats_sb_html(sb, ats_i18n(locale, I18N_APP_CANCEL));
	ats_sb_adds(sb, "</a></div>\n</form>\n");

	if (id != NULL && *id != '\0') {
		ats_sb_adds(sb, "<hr style=\"border:0;border-top:1px solid "
		    "#e2e6ec;margin:26px 0;\">\n"
		    "<form method=\"post\" action=\"/do_delete\" "
		    "data-confirm=\"");
		ats_sb_html(sb, ats_i18n(locale, I18N_APP_DELETE_CONFIRM));
		ats_sb_adds(sb, "\"><input type=\"hidden\" name=\"id\" value=\"");
		ats_sb_html(sb, id);
		ats_sb_adds(sb, "\"><input type=\"hidden\" name=\"confirm\" "
		    "value=\"yes\"><button class=\"btn btn-danger\" "
		    "type=\"submit\">");
		ats_sb_html(sb, ats_i18n(locale, I18N_APP_DELETE));
		ats_sb_adds(sb, "</button>\n</form>\n"
		    "<script>\n"
		    "document.querySelectorAll('form[data-confirm]')"
		    ".forEach(function(f){f.addEventListener('submit',"
		    "function(e){if(!confirm(f.getAttribute('data-confirm')))"
		    "e.preventDefault();});});\n"
		    "</script>\n");
	}

	ats_sb_adds(sb, "</div></div>\n");
	sb_page_foot(sb);
}

static void
sb_tracker_snippets(struct ats_sb *sb, enum ats_locale locale,
    const char *app_id)
{
	ats_sb_adds(sb, "<details class=\"details\"><summary>");
	ats_sb_html(sb, ats_i18n(locale, I18N_INSTALL_SUMMARY));
	ats_sb_adds(sb, "</summary><p class=\"help\">");
	ats_sb_adds(sb, ats_i18n(locale, I18N_INSTALL_HELP));
	ats_sb_adds(sb, "</p><pre><code>&lt;script async src=\"");
	ats_sb_adds(sb, ats_base_url());
	ats_sb_adds(sb, "/rv.js/");
	ats_sb_html(sb, app_id);
	ats_sb_adds(sb, "\"&gt;&lt;/script&gt;</code></pre>"
	    "<p class=\"help\">");
	ats_sb_html(sb, ats_i18n(locale, I18N_INSTALL_BADGE));
	ats_sb_adds(sb, "</p><pre><code>&lt;div id=\"ats-pv\"&gt;&lt;/div&gt;\n"
	    "&lt;script async src=\"");
	ats_sb_adds(sb, ats_base_url());
	ats_sb_adds(sb, "/pv/");
	ats_sb_html(sb, app_id);
	ats_sb_adds(sb, "/3/ats-pv\"&gt;&lt;/script&gt;</code></pre>"
	    "</details>\n");
}

void
view_create_success(struct ats_sb *sb, struct http_request *req,
    enum ats_locale locale, uint64_t id, const char *name, const char *host)
{
	char	idbuf[32], refbuf[64];

	(void)snprintf(idbuf, sizeof(idbuf), "%llu", (unsigned long long)id);
	(void)snprintf(refbuf, sizeof(refbuf), "/dv/%s", idbuf);

	sb_page_head(sb, locale, ats_i18n(locale, I18N_CREATED_TITLE));
	ats_sb_adds(sb, "<div class=\"topbar\"><div class=\"wrap\">"
	    "<a class=\"brand\" href=\"/dv\"><img class=\"brand-icon\" "
	    "src=\"/res/icon-mark.svg\" alt=\"\">ATS</a>"
	    "<nav class=\"nav\"><a href=\"/dv\">");
	ats_sb_html(sb, ats_i18n(locale, I18N_NAV_DASHBOARD));
	ats_sb_adds(sb, "</a>");
	sb_lang_switch(sb, req, locale, refbuf);
	sb_logout_button(sb, locale);
	ats_sb_adds(sb, "</nav></div></div>\n"
	    "<div class=\"wrap\" style=\"padding-top:28px;padding-bottom:48px;\">"
	    "<div class=\"panel\">\n<div class=\"flash ok\">");
	ats_sb_html(sb, ats_i18n(locale, I18N_CREATED_FLASH));
	ats_sb_adds(sb, "</div><h1 style=\"margin-top:0;font-size:22px;\">");
	ats_sb_html(sb, name);
	ats_sb_adds(sb, "</h1><p class=\"help\">");
	ats_sb_html(sb, ats_i18n(locale, I18N_CREATED_TRACKING));
	ats_sb_adds(sb, " <span class=\"mono\">");
	ats_sb_html(sb, host);
	ats_sb_adds(sb, "</span>.</p>\n");
	sb_tracker_snippets(sb, locale, idbuf);
	ats_sb_adds(sb, "<p style=\"margin-top:22px;\"><a class=\"btn "
	    "btn-primary\" href=\"/dv/");
	ats_sb_html(sb, idbuf);
	ats_sb_adds(sb, "\">");
	ats_sb_html(sb, ats_i18n(locale, I18N_CREATED_OPEN));
	ats_sb_adds(sb, "</a> <a class=\"btn\" href=\"/create\">");
	ats_sb_html(sb, ats_i18n(locale, I18N_CREATED_ANOTHER));
	ats_sb_adds(sb, "</a></p>\n</div></div>\n");
	sb_page_foot(sb);
}

static void
sb_qenc(struct ats_sb *sb, const char *s)
{
	char	esc[4];

	for (; *s != '\0'; s++) {
		if (isalnum((unsigned char)*s) || *s == '-' || *s == '_' ||
		    *s == '.') {
			ats_sb_add(sb, s, 1);
		} else if (*s == ' ') {
			ats_sb_adds(sb, "+");
		} else {
			(void)snprintf(esc, sizeof(esc), "%%%02X",
			    (unsigned char)*s);
			ats_sb_adds(sb, esc);
		}
	}
}

static void
pager_url(struct ats_sb *sb, struct http_request *req, int page)
{
	const char	*qs, *p, *amp, *eq, *base;
	char		 key[512], val[2048], pbuf[32];
	struct ats_sb	 q, pth;
	int		 replaced = 0, first = 1;
	size_t		 klen, vlen, plen;

	base = ats_base_url();
	plen = strlen(base);
	while (plen > 0 && base[plen - 1] == '/')
		plen--;
	ats_sb_add(sb, base, plen);

	ats_sb_init(&pth);
	ats_filter_path(&pth, req->path);
	if (pth.len == 0 || pth.p[0] != '/')
		ats_sb_adds(sb, "/");
	ats_sb_add(sb, pth.p, pth.len);
	ats_sb_free(&pth);

	qs = req->query_string;
	ats_sb_init(&q);

	for (p = qs; p != NULL && *p != '\0'; ) {
		amp = strchr(p, '&');
		if (amp == NULL)
			amp = p + strlen(p);

		if (amp == p) {
			p = (*amp == '\0') ? amp : amp + 1;
			continue;
		}

		eq = memchr(p, '=', (size_t)(amp - p));
		if (eq == NULL) {
			vlen = 0;
			klen = (size_t)(amp - p);
			val[0] = '\0';
		} else {
			klen = (size_t)(eq - p);
			vlen = (size_t)(amp - eq - 1);
			if (vlen >= sizeof(val))
				vlen = sizeof(val) - 1;
			if (ats_url_decode(eq + 1, vlen, val, sizeof(val)) == -1)
				val[0] = '\0';
		}
		if (klen >= sizeof(key))
			klen = sizeof(key) - 1;
		if (ats_url_decode(p, klen, key, sizeof(key)) == -1)
			key[0] = '\0';

		if (!first)
			ats_sb_adds(&q, "&");
		first = 0;

		if (!strcmp(key, "page")) {
			replaced = 1;
			ats_sb_adds(&q, "page=");
			(void)snprintf(pbuf, sizeof(pbuf), "%d", page);
			ats_sb_adds(&q, pbuf);
		} else {
			sb_qenc(&q, key);
			ats_sb_adds(&q, "=");
			sb_qenc(&q, val);
		}

		if (*amp == '\0')
			break;
		p = amp + 1;
	}

	if (!replaced) {
		(void)snprintf(pbuf, sizeof(pbuf), "page=%d", page);
		if (q.len > 0)
			ats_sb_adds(&q, "&");
		ats_sb_adds(&q, pbuf);
	}

	if (q.len > 0) {
		ats_sb_adds(sb, "?");
		ats_sb_add(sb, q.p, q.len);
	}
	ats_sb_free(&q);
}

static void
pager_nav(struct ats_sb *sb, struct http_request *req,
    enum ats_locale locale, int current, int count)
{
	int	first, last, i;

	if (count <= 1)
		return;

	first = current - 2 > 0 ? current - 2 : 1;
	last = current + 2 <= count ? current + 2 : count;

	ats_sb_adds(sb, "<ul class=\"pagination\">\n");

	if (first > 1) {
		ats_sb_adds(sb, "<li><a href=\"");
		pager_url(sb, req, 1);
		ats_sb_adds(sb, "\">");
		ats_sb_html(sb, ats_i18n(locale, I18N_PAGER_FIRST));
		ats_sb_adds(sb, "</a></li><li><a href=\"");
		pager_url(sb, req, first - 1);
		ats_sb_adds(sb, "\">");
		ats_sb_html(sb, ats_i18n(locale, I18N_PAGER_PREV));
		ats_sb_adds(sb, "</a></li>\n");
	}

	for (i = first; i <= last; i++) {
		ats_sb_adds(sb, "<li");
		if (i == current)
			ats_sb_adds(sb, " class=\"active\"");
		ats_sb_adds(sb, "><a href=\"");
		pager_url(sb, req, i);
		ats_sb_addf(sb, "\">%d</a></li>\n", i);
	}

	if (count > last) {
		ats_sb_adds(sb, "<li><a href=\"");
		pager_url(sb, req, last + 1);
		ats_sb_adds(sb, "\">");
		ats_sb_html(sb, ats_i18n(locale, I18N_PAGER_NEXT));
		ats_sb_adds(sb, "</a></li><li><a href=\"");
		pager_url(sb, req, count);
		ats_sb_adds(sb, "\">");
		ats_sb_html(sb, ats_i18n(locale, I18N_PAGER_LAST));
		ats_sb_adds(sb, "</a></li>\n");
	}

	ats_sb_adds(sb, "</ul>\n");
}

static void
sb_upper(struct ats_sb *sb, const char *s)
{
	char	c;

	for (; s != NULL && *s != '\0'; s++) {
		c = *s;
		if (c >= 'a' && c <= 'z')
			c = (char)(c - 'a' + 'A');
		ats_sb_add(sb, &c, 1);
	}
}

static void
sb_chart(struct ats_sb *sb, const struct ats_dashboard *d)
{
	long long	max = 0;
	int		i, pct;

	for (i = 0; i < d->n_days; i++) {
		if (d->days[i].count > max)
			max = d->days[i].count;
	}

	ats_sb_adds(sb, "<div class=\"chart\">\n");
	for (i = 0; i < d->n_days; i++) {
		pct = max > 0 ? (int)(d->days[i].count * 100 / max) : 0;
		if (d->days[i].count > 0 && pct < 3)
			pct = 3;
		ats_sb_adds(sb, "<div class=\"col\" title=\"");
		ats_sb_html(sb, d->days[i].date);
		ats_sb_addf(sb, ": %lld\"><div class=\"bar%s\" style=\"height:"
		    "%d%%\"></div></div>\n", d->days[i].count,
		    d->days[i].count > 0 ? "" : " zero", pct);
	}
	ats_sb_adds(sb, "</div>\n<div class=\"chart-dates\">\n");
	for (i = 0; i < d->n_days; i++) {
		ats_sb_adds(sb, "<span>");
		ats_sb_html(sb, d->days[i].date + 5);
		ats_sb_adds(sb, "</span>");
	}
	ats_sb_adds(sb, "</div>\n");
}

static void
sb_toplist(struct ats_sb *sb, enum ats_locale locale,
    const struct ats_top_item *items, int n, int flags)
{
	long long	max = 0;
	int		i, pct;

	if (n <= 0) {
		ats_sb_adds(sb, "<p class=\"help\">");
		ats_sb_html(sb, ats_i18n(locale, I18N_NO_DATA));
		ats_sb_adds(sb, "</p>\n");
		return;
	}

	for (i = 0; i < n; i++) {
		if (items[i].count > max)
			max = items[i].count;
	}

	ats_sb_adds(sb, "<ul class=\"toplist\">\n");
	for (i = 0; i < n; i++) {
		pct = max > 0 ? (int)(items[i].count * 100 / max) : 0;
		ats_sb_adds(sb, "<li><div class=\"row\">");
		ats_sb_adds(sb, "<span class=\"label\" title=\"");
		ats_sb_html(sb, items[i].label);
		ats_sb_adds(sb, "\">");
		if (flags) {
			sb_flag(sb, items[i].label);
			if (country_ok(items[i].label))
				sb_upper(sb, items[i].label);
			else
				ats_sb_html(sb, ats_i18n(locale, I18N_UNKNOWN));
		} else {
			ats_sb_html(sb, items[i].label);
		}
		ats_sb_addf(sb, "</span><span class=\"n\">%lld</span></div>"
		    "<div class=\"track\"><div class=\"fill\" style=\"width:%d%%\">"
		    "</div></div></li>\n", items[i].count, pct);
	}
	ats_sb_adds(sb, "</ul>\n");
}

void
view_data_view(struct ats_sb *sb, struct http_request *req,
    enum ats_locale locale, const struct ats_dashboard *d)
{
	const struct ats_app_row	*cur;
	char				 idbuf[32], num[32];
	char				 uv[64];
	int				 i;

	cur = NULL;
	for (i = 0; i < d->n_apps; i++) {
		(void)snprintf(num, sizeof(num), "%lld", d->apps[i].id);
		if (!strcmp(num, d->cur_app_id)) {
			cur = &d->apps[i];
			break;
		}
	}

	sb_page_head(sb, locale, cur != NULL ? cur->name :
	    ats_i18n(locale, I18N_DASH_TITLE));
	ats_sb_adds(sb, "<div class=\"topbar\"><div class=\"wrap\">"
	    "<a class=\"brand\" href=\"/dv\"><img class=\"brand-icon\" "
	    "src=\"/res/icon-mark.svg\" alt=\"\">ATS</a>"
	    "<nav class=\"nav\"><a href=\"/create\">");
	ats_sb_html(sb, ats_i18n(locale, I18N_NAV_NEW_APP));
	ats_sb_adds(sb, "</a>");
	sb_lang_switch(sb, req, locale, NULL);
	sb_logout_button(sb, locale);
	ats_sb_adds(sb, "</nav></div></div>\n"
	    "<div class=\"wrap\"><div class=\"layout\">\n");

	/* Sidebar. */
	ats_sb_adds(sb, "<aside class=\"sidebar\">\n<h2>");
	ats_sb_html(sb, ats_i18n(locale, I18N_SIDEBAR_APPS));
	ats_sb_adds(sb, "</h2>\n<ul class=\"app-list\">\n");
	for (i = 0; i < d->n_apps; i++) {
		(void)snprintf(idbuf, sizeof(idbuf), "%lld", d->apps[i].id);
		ats_sb_adds(sb, "<li><a href=\"/dv/");
		ats_sb_html(sb, idbuf);
		ats_sb_adds(sb, "\"");
		if (cur != NULL && !strcmp(idbuf, d->cur_app_id))
			ats_sb_adds(sb, " class=\"active\"");
		ats_sb_adds(sb, ">");
		ats_sb_html(sb, d->apps[i].name);
		ats_sb_adds(sb, "<span class=\"host\">");
		ats_sb_html(sb, d->apps[i].host);
		ats_sb_adds(sb, "</span></a></li>\n");
	}
	ats_sb_adds(sb, "</ul>\n<a class=\"btn\" style=\"width:100%;\" "
	    "href=\"/create\">");
	ats_sb_html(sb, ats_i18n(locale, I18N_SIDEBAR_NEW));
	ats_sb_adds(sb, "</a>\n</aside>\n");

	/* Main. */
	ats_sb_adds(sb, "<main class=\"main\">\n");

	if (d->n_apps == 0) {
		ats_sb_adds(sb, "<div class=\"panel empty\"><h2>");
		ats_sb_html(sb, ats_i18n(locale, I18N_WELCOME_TITLE));
		ats_sb_adds(sb, "</h2><p>");
		ats_sb_html(sb, ats_i18n(locale, I18N_WELCOME_TEXT));
		ats_sb_adds(sb, "</p><a class=\"btn btn-primary\" "
		    "href=\"/create\">");
		ats_sb_html(sb, ats_i18n(locale, I18N_WELCOME_CTA));
		ats_sb_adds(sb, "</a></div>\n</main></div></div>\n");
		sb_page_foot(sb);
		return;
	}

	ats_sb_adds(sb, "<div class=\"page-head\"><div><h1>");
	ats_sb_html(sb, cur != NULL ? cur->name :
	    ats_i18n(locale, I18N_UNKNOWN_APP));
	ats_sb_adds(sb, "</h1><p>");
	ats_sb_html(sb, cur != NULL ? cur->host : d->cur_app_id);
	ats_sb_adds(sb, "</p></div><div style=\"display:flex;gap:8px;\">");
	if (cur != NULL) {
		ats_sb_adds(sb, "<a class=\"btn\" href=\"/export/");
		ats_sb_html(sb, d->cur_app_id);
		ats_sb_adds(sb, "\">");
		ats_sb_html(sb, ats_i18n(locale, I18N_EXPORT_CSV));
		ats_sb_adds(sb, "</a><a class=\"btn\" href=\"/edit/");
		ats_sb_html(sb, d->cur_app_id);
		ats_sb_adds(sb, "\">");
		ats_sb_html(sb, ats_i18n(locale, I18N_EDIT_APP));
		ats_sb_adds(sb, "</a>");
	}
	ats_sb_adds(sb, "</div></div>\n");

	/* Summary cards. */
	ats_sb_adds(sb, "<div class=\"cards\">\n");
	ats_sb_adds(sb, "<div class=\"card stat\"><div class=\"k\">");
	ats_sb_html(sb, ats_i18n(locale, I18N_STAT_VISITORS));
	ats_sb_addf(sb, "</div><div class=\"v\">%lld</div></div>\n",
	    d->s.visitors);
	ats_sb_adds(sb, "<div class=\"card stat\"><div class=\"k\">");
	ats_sb_html(sb, ats_i18n(locale, I18N_STAT_PAGE_VIEWS));
	ats_sb_addf(sb, "</div><div class=\"v\">%lld</div></div>\n",
	    d->s.page_views);
	ats_sb_adds(sb, "<div class=\"card stat\"><div class=\"k\">");
	ats_sb_html(sb, ats_i18n(locale, I18N_STAT_TODAY));
	ats_sb_addf(sb, "</div><div class=\"v\">%lld</div><div class=\"sub\">",
	    d->s.s_today);
	(void)snprintf(uv, sizeof(uv), ats_i18n(locale, I18N_TODAY_UV_FMT),
	    d->s.uv_today);
	ats_sb_html(sb, uv);
	ats_sb_adds(sb, "</div></div>\n");
	ats_sb_adds(sb, "<div class=\"card stat\"><div class=\"k\">");
	ats_sb_html(sb, ats_i18n(locale, I18N_STAT_YESTERDAY));
	ats_sb_addf(sb, "</div><div class=\"v\">%lld</div></div>\n",
	    d->s.s_yesterday);
	ats_sb_adds(sb, "<div class=\"card stat\"><div class=\"k\">");
	ats_sb_html(sb, ats_i18n(locale, I18N_STAT_LAST_7));
	ats_sb_addf(sb, "</div><div class=\"v\">%lld</div></div>\n",
	    d->s.s_last_7_days);
	ats_sb_adds(sb, "<div class=\"card stat\"><div class=\"k\">");
	ats_sb_html(sb, ats_i18n(locale, I18N_STAT_LAST_30));
	ats_sb_addf(sb, "</div><div class=\"v\">%lld</div></div>\n",
	    d->s.s_last_30_days);
	ats_sb_adds(sb, "</div>\n");

	/* Trend. */
	ats_sb_adds(sb, "<section class=\"panel\"><h2>");
	ats_sb_html(sb, ats_i18n(locale, I18N_TREND_TITLE));
	ats_sb_adds(sb, "<span class=\"hint\">");
	ats_sb_html(sb, ats_i18n(locale, I18N_TREND_HINT));
	ats_sb_adds(sb, "</span></h2>\n");
	sb_chart(sb, d);
	ats_sb_adds(sb, "</section>\n");

	/* Top lists. */
	ats_sb_adds(sb, "<section class=\"cols3\">\n"
	    "<div class=\"panel\"><h2>");
	ats_sb_html(sb, ats_i18n(locale, I18N_TOP_PAGES));
	ats_sb_adds(sb, "<span class=\"hint\">");
	ats_sb_html(sb, ats_i18n(locale, I18N_PERIOD_30));
	ats_sb_adds(sb, "</span></h2>\n");
	sb_toplist(sb, locale, d->pages, d->n_pages, 0);
	ats_sb_adds(sb, "</div>\n<div class=\"panel\"><h2>");
	ats_sb_html(sb, ats_i18n(locale, I18N_TOP_REFS));
	ats_sb_adds(sb, "<span class=\"hint\">");
	ats_sb_html(sb, ats_i18n(locale, I18N_PERIOD_30));
	ats_sb_adds(sb, "</span></h2>\n");
	sb_toplist(sb, locale, d->refs, d->n_refs, 0);
	ats_sb_adds(sb, "</div>\n<div class=\"panel\"><h2>");
	ats_sb_html(sb, ats_i18n(locale, I18N_TOP_COUNTRIES));
	ats_sb_adds(sb, "<span class=\"hint\">");
	ats_sb_html(sb, ats_i18n(locale, I18N_PERIOD_30));
	ats_sb_adds(sb, "</span></h2>\n");
	sb_toplist(sb, locale, d->countries, d->n_countries, 1);
	ats_sb_adds(sb, "</div>\n</section>\n");

	/* Recent visits. */
	ats_sb_adds(sb, "<section class=\"panel\"><h2>");
	ats_sb_html(sb, ats_i18n(locale, I18N_RECENT_TITLE));
	ats_sb_adds(sb, "<span class=\"hint\">");
	ats_sb_html(sb, ats_i18n(locale, I18N_RECENT_HINT));
	ats_sb_adds(sb, "</span></h2>\n");

	if (d->n_rows == 0) {
		ats_sb_adds(sb, "<p class=\"help\">");
		ats_sb_html(sb, ats_i18n(locale, I18N_NO_VISITS));
		ats_sb_adds(sb, "</p>\n");
	} else {
		ats_sb_adds(sb, "<div style=\"overflow-x:auto;\"><table "
		    "class=\"visits\"><thead><tr><th>");
		ats_sb_html(sb, ats_i18n(locale, I18N_TH_PAGE));
		ats_sb_adds(sb, "</th><th>");
		ats_sb_html(sb, ats_i18n(locale, I18N_TH_REFERRER));
		ats_sb_adds(sb, "</th><th>");
		ats_sb_html(sb, ats_i18n(locale, I18N_TH_IP));
		ats_sb_adds(sb, "</th><th>");
		ats_sb_html(sb, ats_i18n(locale, I18N_TH_COUNTRY));
		ats_sb_adds(sb, "</th><th>");
		ats_sb_html(sb, ats_i18n(locale, I18N_TH_BROWSER));
		ats_sb_adds(sb, "</th><th>");
		ats_sb_html(sb, ats_i18n(locale, I18N_TH_OS));
		ats_sb_adds(sb, "</th><th>");
		ats_sb_html(sb, ats_i18n(locale, I18N_TH_TIME));
		ats_sb_adds(sb, "</th></tr></thead><tbody>\n");
		for (i = 0; i < d->n_rows; i++) {
			ats_sb_adds(sb, "<tr><td class=\"url\"><span class=\"ell\" "
			    "title=\"");
			ats_sb_html(sb, d->rows[i].page_url);
			ats_sb_adds(sb, "\">");
			ats_sb_html(sb, d->rows[i].page_url);
			ats_sb_adds(sb, "</span></td><td class=\"url\"><span "
			    "class=\"ell\" title=\"");
			ats_sb_html(sb, d->rows[i].referring_url);
			ats_sb_adds(sb, "\">");
			ats_sb_html(sb, d->rows[i].referring_url);
			ats_sb_adds(sb, "</span></td><td class=\"mono\">");
			ats_sb_html(sb, d->rows[i].visitor_ip);
			ats_sb_adds(sb, "</td><td>");
			if (country_ok(d->rows[i].country)) {
				sb_flag(sb, d->rows[i].country);
				ats_sb_adds(sb, " ");
				sb_upper(sb, d->rows[i].country);
			} else {
				ats_sb_html(sb, ats_i18n(locale, I18N_UNKNOWN));
			}
			ats_sb_adds(sb, "</td><td>");
			sb_ua_text(sb, locale, d->rows[i].browser);
			ats_sb_adds(sb, "</td><td>");
			sb_ua_text(sb, locale, d->rows[i].operating_system);
			ats_sb_adds(sb, "</td><td class=\"mono\">");
			ats_sb_html(sb, d->rows[i].visit_time);
			ats_sb_adds(sb, "</td></tr>\n");
		}
		ats_sb_adds(sb, "</tbody></table></div>\n");
		pager_nav(sb, req, locale, d->current_page, d->page_count);
	}

	sb_tracker_snippets(sb, locale, d->cur_app_id);
	ats_sb_adds(sb, "</section>\n</main>\n</div></div>\n");
	sb_page_foot(sb);
}
