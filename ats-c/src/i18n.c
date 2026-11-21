#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <kore/kore.h>
#include <kore/http.h>

#include "i18n.h"
#include "util.h"

#include "lang/zh_cn.h"
#include "lang/en.h"

static enum ats_locale	default_locale = ATS_LOCALE_ZH_CN;

static const char *const *const catalogs[ATS_LOCALE_COUNT] = {
	ats_lang_zh_cn,
	ats_lang_en,
};

static const char *const locale_codes[ATS_LOCALE_COUNT] = {
	"zh-CN",
	"en",
};

static const char *const locale_names[ATS_LOCALE_COUNT] = {
	"中文",
	"English",
};

static enum ats_locale
invalid_locale(void)
{
	return (ATS_LOCALE_INVALID);
}

const char *
ats_locale_code(enum ats_locale locale)
{
	if (locale < 0 || locale >= ATS_LOCALE_COUNT)
		return (locale_codes[ATS_LOCALE_ZH_CN]);

	return (locale_codes[locale]);
}

const char *
ats_locale_name(enum ats_locale locale)
{
	if (locale < 0 || locale >= ATS_LOCALE_COUNT)
		return (locale_names[ATS_LOCALE_ZH_CN]);

	return (locale_names[locale]);
}

enum ats_locale
ats_locale_from_code(const char *code)
{
	char	 buf[64];
	size_t	 i;

	if (code == NULL || *code == '\0')
		return (invalid_locale());

	(void)snprintf(buf, sizeof(buf), "%s", code);
	ats_trim(buf);
	if (*buf == '\0')
		return (invalid_locale());

	for (i = 0; buf[i] != '\0'; i++) {
		if (buf[i] == '_')
			buf[i] = '-';
		else
			buf[i] = (char)tolower((unsigned char)buf[i]);
	}

	if (!strcmp(buf, "zh") || !strncmp(buf, "zh-", 3))
		return (ATS_LOCALE_ZH_CN);
	if (!strcmp(buf, "en") || !strncmp(buf, "en-", 3))
		return (ATS_LOCALE_EN);

	return (invalid_locale());
}

enum ats_locale
ats_locale_default(void)
{
	return (default_locale);
}

enum ats_locale
ats_locale_negotiate(const char *accept_language)
{
	char		 buf[512];
	char		*p, *comma, *semi, *qpos;
	enum ats_locale	 best, loc;
	double		 q, best_q;

	if (accept_language == NULL || *accept_language == '\0')
		return (invalid_locale());

	(void)snprintf(buf, sizeof(buf), "%s", accept_language);

	best = invalid_locale();
	best_q = 0.0;

	for (p = buf; p != NULL && *p != '\0'; p = comma) {
		comma = strchr(p, ',');
		if (comma != NULL)
			*comma = '\0';

		semi = strchr(p, ';');
		q = 1.0;
		if (semi != NULL) {
			*semi = '\0';
			qpos = strstr(semi + 1, "q=");
			if (qpos != NULL) {
				q = strtod(qpos + 2, NULL);
				if (q < 0.0)
					q = 0.0;
				if (q > 1.0)
					q = 1.0;
			}
		}

		ats_trim(p);
		loc = ats_locale_from_code(p);
		if (loc >= 0 && q > best_q) {
			best = loc;
			best_q = q;
		}

		if (comma == NULL)
			break;
	}

	return (best);
}

enum ats_locale
ats_locale_for_request(struct http_request *req)
{
	char		*raw;
	const char	*accept;
	char		 qlang[32];
	enum ats_locale	 loc;

	if (req == NULL)
		return (default_locale);

	/* Explicit ?lang= wins (used by the PV badge and links). */
	if (ats_query_get(req, "lang", qlang, sizeof(qlang)) && *qlang != '\0') {
		loc = ats_locale_from_code(qlang);
		if (loc >= 0)
			return (loc);
	}

	/* The language switch stores a long-lived cookie. */
	http_populate_cookies(req);
	if (http_request_cookie(req, "ats_lang", &raw) == KORE_RESULT_OK) {
		loc = ats_locale_from_code(raw);
		if (loc >= 0)
			return (loc);
	}

	if (http_request_header(req, "accept-language", &accept)
	    == KORE_RESULT_OK) {
		loc = ats_locale_negotiate(accept);
		if (loc >= 0)
			return (loc);
	}

	return (default_locale);
}

const char *
ats_i18n(enum ats_locale locale, enum ats_i18n_key key)
{
	const char	*s;

	if (locale < 0 || locale >= ATS_LOCALE_COUNT)
		locale = ATS_LOCALE_ZH_CN;
	if (key < 0 || key >= I18N_KEY_COUNT)
		return ("");

	s = catalogs[locale][key];
	if (s != NULL)
		return (s);

	/* A missing translation falls back to English. */
	s = ats_lang_en[key];
	if (s != NULL)
		return (s);

	return ("");
}

int
ats_i18n_init(void)
{
	const char	*v;
	enum ats_locale	 loc;
	int		 i;

	if ((v = getenv("ATS_DEFAULT_LOCALE")) != NULL && *v != '\0') {
		loc = ats_locale_from_code(v);
		if (loc >= 0) {
			default_locale = loc;
		} else {
			kore_log(LOG_WARNING,
			    "i18n: ignoring unsupported ATS_DEFAULT_LOCALE=%s", v);
		}
	}

	for (i = 0; i < I18N_KEY_COUNT; i++) {
		if (ats_lang_zh_cn[i] == NULL && ats_lang_en[i] == NULL)
			kore_log(LOG_WARNING, "i18n: key %d has no translation", i);
	}

	kore_log(LOG_INFO, "i18n: default locale %s",
	    ats_locale_code(default_locale));

	return (0);
}
