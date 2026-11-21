/*
 * HTML/JS rendering.
 *
 * The dynamic values that reach the page are escaped: HTML entities for text
 * and attributes, a single-quoted-JS-string escaper for inline scripts.
 */

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "ats.h"
#include "util.h"
#include "views.h"

static void	pager_nav(struct ats_sb *, struct http_request *, int, int);
static void	pager_url(struct ats_sb *, struct http_request *, int);
static void	sb_qenc(struct ats_sb *, const char *);
static void	sb_upper(struct ats_sb *, const char *);
static void	sb_page_head(struct ats_sb *, const char *);
static void	sb_page_foot(struct ats_sb *);
static void	sb_flag(struct ats_sb *, const char *);
static int	country_ok(const char *);
static void	sb_chart(struct ats_sb *, const struct ats_dashboard *);
static void	sb_toplist(struct ats_sb *, const struct ats_top_item *, int,
		    int);
static void	sb_tracker_snippets(struct ats_sb *, const char *);

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
sb_ua_text(struct ats_sb *sb, const char *s)
{
	if (s == NULL || *s == '\0' || !strcmp(s, "Unknown Platform"))
		ats_sb_adds(sb, "未知");
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
	if (country_ok(country)) {
		sb_upper(sb, country);
	}
	ats_sb_adds(sb, "\">");
}

void
view_js_res_pv(struct ats_sb *sb, const char *container, long long pv_total,
    const struct ats_country_count *rows, int n, long long others)
{
	int	i;

	ats_sb_adds(sb, "(function(){var root=document.getElementById(\"");
	ats_sb_js(sb, container);
	ats_sb_adds(sb,
	    "\");if(!root)return;"
	    "var data=[");
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
	    "add('globe',total,'浏览量',true);"
	    "for(var i=0;i<data.length;i++){"
	    "add(data[i][0],data[i][1],"
	    "data[i][0]==='unknown'?'未知':data[i][0].toUpperCase(),false);}"
	    "if(others>0)add('unknown',others,'其他',false);"
	    "})();\n");
}

static void
sb_logout_button(struct ats_sb *sb)
{
	ats_sb_adds(sb, "<form method=\"post\" action=\"/logout\">"
	    "<input type=\"hidden\" name=\"x\" value=\"1\">"
	    "<button class=\"linkbtn\" type=\"submit\">退出</button></form>");
}

static void
sb_page_head(struct ats_sb *sb, const char *title)
{
	ats_sb_adds(sb, "<!DOCTYPE html>\n<html lang=\"zh-CN\">\n<head>\n"
	    "<meta charset=\"utf-8\">\n"
	    "<meta name=\"viewport\" content=\"width=device-width, "
	    "initial-scale=1\">\n"
	    "<meta name=\"robots\" content=\"noindex,nofollow\">\n"
	    "<title>");
	ats_sb_html(sb, title);
	ats_sb_adds(sb, " - ATS</title>\n"
	    "<link rel=\"stylesheet\" href=\"/res/css/ats.css\">\n"
	    "<link rel=\"icon\" href=\"/favicon.ico\">\n"
	    "</head>\n<body>\n");
}

static void
sb_page_foot(struct ats_sb *sb)
{
	ats_sb_adds(sb, "</body>\n</html>\n");
}

void
view_error(struct ats_sb *sb, int status, const char *title,
    const char *message)
{
	sb_page_head(sb, title);
	ats_sb_adds(sb, "<div class=\"wrap\" style=\"padding-top:48px;"
	    "padding-bottom:48px;\"><div class=\"panel\">"
	    "<h1 style=\"margin-top:0;font-size:22px;\">");
	ats_sb_html(sb, title);
	ats_sb_adds(sb, "</h1><p style=\"color:#687385;\">");
	ats_sb_html(sb, message);
	ats_sb_adds(sb, "</p><p><a class=\"btn\" href=\"/dv\">返回仪表盘"
	    "</a></p></div></div>");
	sb_page_foot(sb);
	(void)status;
}

void
view_login(struct ats_sb *sb, const char *ref, const char *error)
{
	sb_page_head(sb, "登录");
	ats_sb_adds(sb, "<div class=\"login-page\"><div class=\"login-card\">\n"
	    "<div class=\"panel\">\n"
	    "<h1>ATS</h1>\n"
	    "<p class=\"sub\">私有流量统计</p>\n");

	if (error != NULL && *error != '\0') {
		ats_sb_adds(sb, "<div class=\"flash error\">");
		ats_sb_html(sb, error);
		ats_sb_adds(sb, "</div>\n");
	}

	ats_sb_adds(sb, "<form method=\"post\" action=\"/do_login\" "
	    "onsubmit=\"return pwd_crypto()\" accept-charset=\"utf-8\">\n"
	    "<div class=\"field\"><label class=\"label\" for=\"username\">"
	    "用户名</label><input class=\"input\" id=\"username\" "
	    "name=\"username\" type=\"text\" autocomplete=\"username\" "
	    "autofocus required></div>\n"
	    "<div class=\"field\"><label class=\"label\" for=\"raw_pwd\">"
	    "密码</label><input class=\"input\" id=\"raw_pwd\" "
	    "type=\"password\" autocomplete=\"current-password\" required>"
	    "<input type=\"hidden\" id=\"encrypted_pwd\" name=\"password\" "
	    "required></div>\n"
	    "<input type=\"hidden\" name=\"ref\" value=\"");
	ats_sb_html(sb, ref);
	ats_sb_adds(sb, "\">\n"
	    "<div class=\"row\"><noscript><span class=\"help\">需要启用 JavaScript 才能登录。</span></noscript>"
	    "<button class=\"btn btn-primary\" type=\"submit\">登录</button></div>\n"
	    "</form>\n</div>\n"
	    "<p class=\"foot\">");
	ats_sb_html(sb, ats_base_url_host());
	ats_sb_adds(sb, "</p>\n"
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
view_app_form(struct ats_sb *sb, const char *title, const char *action,
    const char *id, const char *name, const char *hosts, const char *remark,
    const char *error)
{
	sb_page_head(sb, title);
	ats_sb_adds(sb, "<div class=\"topbar\"><div class=\"wrap\">"
	    "<a class=\"brand\" href=\"/dv\">ATS</a>"
	    "<nav class=\"nav\"><a href=\"/dv\">仪表盘</a>");
	sb_logout_button(sb);
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
	    "for=\"app_name\">名称</label><input class=\"input\" id=\"app_name\" "
	    "name=\"app_name\" value=\"");
	ats_sb_html(sb, name);
	ats_sb_adds(sb, "\" placeholder=\"我的网站\" required></div>\n"
	    "<div class=\"field\"><label class=\"label\" for=\"app_hosts\">"
	    "域名</label><input class=\"input\" id=\"app_hosts\" "
	    "name=\"app_hosts\" value=\"");
	ats_sb_html(sb, hosts);
	ats_sb_adds(sb, "\" placeholder=\"example.com, www.example.com\" "
	    "required><div class=\"help\">最多 3 个域名，用逗号分隔；也可以直接粘贴完整 URL，会自动转换为域名。</div></div>\n"
	    "<div class=\"field\"><label class=\"label\" for=\"remark\">"
	    "备注</label><input class=\"input\" id=\"remark\" name=\"remark\" "
	    "value=\"");
	ats_sb_html(sb, remark);
	ats_sb_adds(sb, "\" placeholder=\"可选\"></div>\n"
	    "<div class=\"row\" style=\"display:flex;gap:10px;align-items:center;"
	    "\"><button class=\"btn btn-primary\" type=\"submit\">保存</button>"
	    "<a class=\"btn\" href=\"/dv\">取消</a></div>\n</form>\n");

	if (id != NULL && *id != '\0') {
		ats_sb_adds(sb, "<hr style=\"border:0;border-top:1px solid "
		    "#e2e6ec;margin:26px 0;\">\n"
		    "<form method=\"post\" action=\"/do_delete\" "
		    "onsubmit=\"return confirm('确定删除该应用及其全部流量数据吗？')\">\n"
		    "<input type=\"hidden\" name=\"id\" value=\"");
		ats_sb_html(sb, id);
		ats_sb_adds(sb, "\"><input type=\"hidden\" name=\"confirm\" "
		    "value=\"yes\"><button class=\"btn btn-danger\" "
		    "type=\"submit\">删除应用</button>\n</form>\n");
	}

	ats_sb_adds(sb, "</div></div>\n");
	sb_page_foot(sb);
}

static void
sb_tracker_snippets(struct ats_sb *sb, const char *app_id)
{
	ats_sb_adds(sb, "<details class=\"details\"><summary>安装埋点</summary><p class=\"help\">把代码放到需要统计的页面 &lt;/body&gt; 之前。埋点依赖浏览器的 Referer；如果站点设置了 <span class=\"mono\">Referrer-Policy: no-referrer</span>，将无法回填页面 URL。</p>"
	    "<pre><code>&lt;script async src=\"");
	ats_sb_adds(sb, ats_base_url());
	ats_sb_adds(sb, "/rv.js/");
	ats_sb_html(sb, app_id);
	ats_sb_adds(sb, "\"&gt;&lt;/script&gt;</code></pre>"
	    "<p class=\"help\">可选的国家 / 浏览量徽章：</p>"
	    "<pre><code>&lt;div id=\"ats-pv\"&gt;&lt;/div&gt;\n"
	    "&lt;script async src=\"");
	ats_sb_adds(sb, ats_base_url());
	ats_sb_adds(sb, "/pv/");
	ats_sb_html(sb, app_id);
	ats_sb_adds(sb, "/3/ats-pv\"&gt;&lt;/script&gt;</code></pre>"
	    "</details>\n");
}

void
view_create_success(struct ats_sb *sb, uint64_t id, const char *name,
    const char *host)
{
	char	idbuf[32];

	(void)snprintf(idbuf, sizeof(idbuf), "%llu", (unsigned long long)id);

	sb_page_head(sb, "应用已创建");
	ats_sb_adds(sb, "<div class=\"topbar\"><div class=\"wrap\">"
	    "<a class=\"brand\" href=\"/dv\">ATS</a>"
	    "<nav class=\"nav\"><a href=\"/dv\">仪表盘</a>");
	sb_logout_button(sb);
	ats_sb_adds(sb, "</nav></div></div>\n"
	    "<div class=\"wrap\" style=\"padding-top:28px;padding-bottom:48px;\">"
	    "<div class=\"panel\">\n<div class=\"flash ok\">应用已创建。</div>"
	    "<h1 style=\"margin-top:0;font-size:22px;\">");
	ats_sb_html(sb, name);
	ats_sb_adds(sb, "</h1><p class=\"help\">正在统计 <span class=\"mono\">");
	ats_sb_html(sb, host);
	ats_sb_adds(sb, "</span>.</p>\n");
	sb_tracker_snippets(sb, idbuf);
	ats_sb_adds(sb, "<p style=\"margin-top:22px;\"><a class=\"btn "
	    "btn-primary\" href=\"/dv/");
	ats_sb_html(sb, idbuf);
	ats_sb_adds(sb, "\">打开仪表盘</a> <a class=\"btn\" "
	    "href=\"/create\">再建一个</a></p>\n</div></div>\n");
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
pager_nav(struct ats_sb *sb, struct http_request *req, int current, int count)
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
		ats_sb_adds(sb, "\">首页</a></li><li><a href=\"");
		pager_url(sb, req, first - 1);
		ats_sb_adds(sb, "\">上一页</a></li>\n");
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
		ats_sb_adds(sb, "\">下一页</a></li><li><a href=\"");
		pager_url(sb, req, count);
		ats_sb_adds(sb, "\">末页</a></li>\n");
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
sb_toplist(struct ats_sb *sb, const struct ats_top_item *items, int n,
    int flags)
{
	long long	max = 0;
	int		i, pct;

	if (n <= 0) {
		ats_sb_adds(sb, "<p class=\"help\">暂无数据。</p>\n");
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
				ats_sb_adds(sb, "未知");
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
    const struct ats_dashboard *d)
{
	const struct ats_app_row	*cur;
	char				 idbuf[32], num[32];
	int				 i;

	cur = NULL;
	for (i = 0; i < d->n_apps; i++) {
		(void)snprintf(num, sizeof(num), "%lld", d->apps[i].id);
		if (!strcmp(num, d->cur_app_id)) {
			cur = &d->apps[i];
			break;
		}
	}

	sb_page_head(sb, cur != NULL ? cur->name : "仪表盘");
	ats_sb_adds(sb, "<div class=\"topbar\"><div class=\"wrap\">"
	    "<a class=\"brand\" href=\"/dv\">ATS</a>"
	    "<nav class=\"nav\"><a href=\"/create\">新建应用</a>");
	sb_logout_button(sb);
	ats_sb_adds(sb, "</nav></div></div>\n"
	    "<div class=\"wrap\"><div class=\"layout\">\n");

	/* Sidebar. */
	ats_sb_adds(sb, "<aside class=\"sidebar\">\n<h2>应用</h2>\n"
	    "<ul class=\"app-list\">\n");
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
	    "href=\"/create\">+ 新建应用</a>\n</aside>\n");

	/* Main. */
	ats_sb_adds(sb, "<main class=\"main\">\n");

	if (d->n_apps == 0) {
		ats_sb_adds(sb, "<div class=\"panel empty\"><h2>欢迎使用 ATS</h2><p>创建第一个应用，开始收集页面浏览量和访客数据。创建后会得到一段可直接粘贴到网站的脚本。</p><a class=\"btn btn-primary\" href=\"/create\">创建应用</a></div>\n</main></div></div>\n");
		sb_page_foot(sb);
		return;
	}

	ats_sb_adds(sb, "<div class=\"page-head\"><div><h1>");
	ats_sb_html(sb, cur != NULL ? cur->name : "未知应用");
	ats_sb_adds(sb, "</h1><p>");
	ats_sb_html(sb, cur != NULL ? cur->host : d->cur_app_id);
	ats_sb_adds(sb, "</p></div><div style=\"display:flex;gap:8px;\">");
	if (cur != NULL) {
		ats_sb_adds(sb, "<a class=\"btn\" href=\"/export/");
		ats_sb_html(sb, d->cur_app_id);
		ats_sb_adds(sb, "\">导出 CSV</a>"
		    "<a class=\"btn\" href=\"/edit/");
		ats_sb_html(sb, d->cur_app_id);
		ats_sb_adds(sb, "\">编辑应用</a>");
	}
	ats_sb_adds(sb, "</div></div>\n");

	/* Summary cards. */
	ats_sb_adds(sb, "<div class=\"cards\">\n");
	ats_sb_addf(sb, "<div class=\"card stat\"><div class=\"k\">访客"
	    "</div><div class=\"v\">%lld</div></div>\n", d->s.visitors);
	ats_sb_addf(sb, "<div class=\"card stat\"><div class=\"k\">浏览量"
	    "</div><div class=\"v\">%lld</div></div>\n", d->s.page_views);
	ats_sb_addf(sb, "<div class=\"card stat\"><div class=\"k\">今日"
	    "</div><div class=\"v\">%lld</div><div class=\"sub\">%lld "
	    "访客</div></div>\n", d->s.s_today, d->s.uv_today);
	ats_sb_addf(sb, "<div class=\"card stat\"><div class=\"k\">昨日"
	    "</div><div class=\"v\">%lld</div></div>\n", d->s.s_yesterday);
	ats_sb_addf(sb, "<div class=\"card stat\"><div class=\"k\">近 7 天"
	    "</div><div class=\"v\">%lld</div></div>\n", d->s.s_last_7_days);
	ats_sb_addf(sb, "<div class=\"card stat\"><div class=\"k\">近 30 天"
	    "</div><div class=\"v\">%lld</div></div>\n", d->s.s_last_30_days);
	ats_sb_adds(sb, "</div>\n");

	/* Trend. */
	ats_sb_adds(sb, "<section class=\"panel\"><h2>近 14 天"
	    "<span class=\"hint\">每日浏览量</span></h2>\n");
	sb_chart(sb, d);
	ats_sb_adds(sb, "</section>\n");

	/* Top lists. */
	ats_sb_adds(sb, "<section class=\"cols3\">\n"
	    "<div class=\"panel\"><h2>热门页面<span class=\"hint\">30 天"
	    "</span></h2>\n");
	sb_toplist(sb, d->pages, d->n_pages, 0);
	ats_sb_adds(sb, "</div>\n<div class=\"panel\"><h2>来源<span class=\"hint\">30 天</span></h2>\n");
	sb_toplist(sb, d->refs, d->n_refs, 0);
	ats_sb_adds(sb, "</div>\n<div class=\"panel\"><h2>国家/地区<span class=\"hint\">30 天</span></h2>\n");
	sb_toplist(sb, d->countries, d->n_countries, 1);
	ats_sb_adds(sb, "</div>\n</section>\n");

	/* Recent visits. */
	ats_sb_adds(sb, "<section class=\"panel\"><h2>最近访问<span class=\"hint\">最新在前</span></h2>\n");

	if (d->n_rows == 0) {
		ats_sb_adds(sb, "<p class=\"help\">暂无访问记录。安装埋点后打开你的网站。</p>\n");
	} else {
		ats_sb_adds(sb, "<div style=\"overflow-x:auto;\"><table "
		    "class=\"visits\"><thead><tr><th>页面</th><th>来源</th><th>IP</th><th>国家</th><th>浏览器</th><th>系统</th><th>时间</th></tr></thead><tbody>\n");
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
				ats_sb_adds(sb, "未知");
			}
			ats_sb_adds(sb, "</td><td>");
			sb_ua_text(sb, d->rows[i].browser);
			ats_sb_adds(sb, "</td><td>");
			sb_ua_text(sb, d->rows[i].operating_system);
			ats_sb_adds(sb, "</td><td class=\"mono\">");
			ats_sb_html(sb, d->rows[i].visit_time);
			ats_sb_adds(sb, "</td></tr>\n");
		}
		ats_sb_adds(sb, "</tbody></table></div>\n");
		pager_nav(sb, req, d->current_page, d->page_count);
	}

	sb_tracker_snippets(sb, d->cur_app_id);
	ats_sb_adds(sb, "</section>\n</main>\n</div></div>\n");
	sb_page_foot(sb);
}
