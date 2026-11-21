/*
 * CodeIgniter\HTTP\UserAgent with app/Config/UserAgents.php.
 *
 * Matching order is CI4's compileData(): setPlatform(), then setRobot();
 * if that misses setBrowser(); if that misses setMobile().
 * setRobot() and setBrowser() both call setMobile() themselves.
 *
 * platform/robot/mobile keys are matched literally (preg_quote/stripos in the
 * PHP source); browser keys are used as regular expressions.
 */

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <kore/kore.h>

#include "ua.h"

struct pair {
	const char	*key;
	const char	*val;
};

static const struct pair platforms[] = {
	{ "windows nt 10.0", "Windows 10" },
	{ "windows nt 6.3", "Windows 8.1" },
	{ "windows nt 6.2", "Windows 8" },
	{ "windows nt 6.1", "Windows 7" },
	{ "windows nt 6.0", "Windows Vista" },
	{ "windows nt 5.2", "Windows 2003" },
	{ "windows nt 5.1", "Windows XP" },
	{ "windows nt 5.0", "Windows 2000" },
	{ "windows nt 4.0", "Windows NT 4.0" },
	{ "winnt4.0", "Windows NT 4.0" },
	{ "winnt 4.0", "Windows NT" },
	{ "winnt", "Windows NT" },
	{ "windows 98", "Windows 98" },
	{ "win98", "Windows 98" },
	{ "windows 95", "Windows 95" },
	{ "win95", "Windows 95" },
	{ "windows phone", "Windows Phone" },
	{ "windows", "Unknown Windows OS" },
	{ "android", "Android" },
	{ "blackberry", "BlackBerry" },
	{ "iphone", "iOS" },
	{ "ipad", "iOS" },
	{ "ipod", "iOS" },
	{ "os x", "Mac OS X" },
	{ "ppc mac", "Power PC Mac" },
	{ "freebsd", "FreeBSD" },
	{ "ppc", "Macintosh" },
	{ "linux", "Linux" },
	{ "debian", "Debian" },
	{ "sunos", "Sun Solaris" },
	{ "beos", "BeOS" },
	{ "apachebench", "ApacheBench" },
	{ "aix", "AIX" },
	{ "irix", "Irix" },
	{ "osf", "DEC OSF" },
	{ "hp-ux", "HP-UX" },
	{ "netbsd", "NetBSD" },
	{ "bsdi", "BSDi" },
	{ "openbsd", "OpenBSD" },
	{ "gnu", "GNU/Linux" },
	{ "unix", "Unknown Unix OS" },
	{ "symbian", "Symbian OS" },
	{ NULL, NULL }
};

static const struct pair browsers[] = {
	{ "OPR", "Opera" },
	{ "Flock", "Flock" },
	{ "Edge", "Spartan" },
	{ "Edg", "Edge" },
	{ "Chrome", "Chrome" },
	{ "Opera.*?Version", "Opera" },
	{ "Opera", "Opera" },
	{ "MSIE", "Internet Explorer" },
	{ "Internet Explorer", "Internet Explorer" },
	{ "Trident.* rv", "Internet Explorer" },
	{ "Shiira", "Shiira" },
	{ "Firefox", "Firefox" },
	{ "Chimera", "Chimera" },
	{ "Phoenix", "Phoenix" },
	{ "Firebird", "Firebird" },
	{ "Camino", "Camino" },
	{ "Netscape", "Netscape" },
	{ "OmniWeb", "OmniWeb" },
	{ "Safari", "Safari" },
	{ "Mozilla", "Mozilla" },
	{ "Konqueror", "Konqueror" },
	{ "icab", "iCab" },
	{ "Lynx", "Lynx" },
	{ "Links", "Links" },
	{ "hotjava", "HotJava" },
	{ "amaya", "Amaya" },
	{ "IBrowse", "IBrowse" },
	{ "Maxthon", "Maxthon" },
	{ "Ubuntu", "Ubuntu Web Browser" },
	{ "Vivaldi", "Vivaldi" },
	{ NULL, NULL }
};

static const struct pair mobiles[] = {
	{ "mobileexplorer", "Mobile Explorer" },
	{ "palmsource", "Palm" },
	{ "palmscape", "Palmscape" },
	{ "motorola", "Motorola" },
	{ "nokia", "Nokia" },
	{ "palm", "Palm" },
	{ "iphone", "Apple iPhone" },
	{ "ipad", "iPad" },
	{ "ipod", "Apple iPod Touch" },
	{ "sony", "Sony Ericsson" },
	{ "ericsson", "Sony Ericsson" },
	{ "blackberry", "BlackBerry" },
	{ "cocoon", "O2 Cocoon" },
	{ "blazer", "Treo" },
	{ "lg", "LG" },
	{ "amoi", "Amoi" },
	{ "xda", "XDA" },
	{ "mda", "MDA" },
	{ "vario", "Vario" },
	{ "htc", "HTC" },
	{ "samsung", "Samsung" },
	{ "sharp", "Sharp" },
	{ "sie-", "Siemens" },
	{ "alcatel", "Alcatel" },
	{ "benq", "BenQ" },
	{ "ipaq", "HP iPaq" },
	{ "mot-", "Motorola" },
	{ "playstation portable", "PlayStation Portable" },
	{ "playstation 3", "PlayStation 3" },
	{ "playstation vita", "PlayStation Vita" },
	{ "hiptop", "Danger Hiptop" },
	{ "nec-", "NEC" },
	{ "panasonic", "Panasonic" },
	{ "philips", "Philips" },
	{ "sagem", "Sagem" },
	{ "sanyo", "Sanyo" },
	{ "spv", "SPV" },
	{ "zte", "ZTE" },
	{ "sendo", "Sendo" },
	{ "nintendo dsi", "Nintendo DSi" },
	{ "nintendo ds", "Nintendo DS" },
	{ "nintendo 3ds", "Nintendo 3DS" },
	{ "wii", "Nintendo Wii" },
	{ "open web", "Open Web" },
	{ "openweb", "OpenWeb" },
	{ "android", "Android" },
	{ "symbian", "Symbian" },
	{ "symbianos", "SymbianOS" },
	{ "elaine", "Palm" },
	{ "series60", "Symbian S60" },
	{ "windows ce", "Windows CE" },
	{ "obigo", "Obigo" },
	{ "netfront", "Netfront Browser" },
	{ "openwave", "Openwave Browser" },
	{ "mobilexplorer", "Mobile Explorer" },
	{ "operamini", "Opera Mini" },
	{ "opera mini", "Opera Mini" },
	{ "opera mobi", "Opera Mobile" },
	{ "fennec", "Firefox Mobile" },
	{ "digital paths", "Digital Paths" },
	{ "avantgo", "AvantGo" },
	{ "xiino", "Xiino" },
	{ "novarra", "Novarra Transcoder" },
	{ "vodafone", "Vodafone" },
	{ "docomo", "NTT DoCoMo" },
	{ "o2", "O2" },
	{ "mobile", "Generic Mobile" },
	{ "wireless", "Generic Mobile" },
	{ "j2me", "Generic Mobile" },
	{ "midp", "Generic Mobile" },
	{ "cldc", "Generic Mobile" },
	{ "up.link", "Generic Mobile" },
	{ "up.browser", "Generic Mobile" },
	{ "smartphone", "Generic Mobile" },
	{ "cellphone", "Generic Mobile" },
	{ NULL, NULL }
};

static const struct pair robots[] = {
	{ "googlebot", "Googlebot" },
	{ "msnbot", "MSNBot" },
	{ "baiduspider", "Baiduspider" },
	{ "bingbot", "Bing" },
	{ "slurp", "Inktomi Slurp" },
	{ "yahoo", "Yahoo" },
	{ "ask jeeves", "Ask Jeeves" },
	{ "fastcrawler", "FastCrawler" },
	{ "infoseek", "InfoSeek Robot 1.0" },
	{ "lycos", "Lycos" },
	{ "yandex", "YandexBot" },
	{ "mediapartners-google", "MediaPartners Google" },
	{ "crazywebcrawler", "Crazy Webcrawler" },
	{ "adsbot-google", "AdsBot Google" },
	{ "feedfetcher-google", "Feedfetcher Google" },
	{ "curious george", "Curious George" },
	{ "ia_archiver", "Alexa Crawler" },
	{ "mj12bot", "Majestic-12" },
	{ "uptimebot", "Uptimebot" },
	{ NULL, NULL }
};

#define N_BROWSERS	((int)(sizeof(browsers) / sizeof(browsers[0])) - 1)

static pcre2_code	*browser_re[N_BROWSERS];
static pcre2_match_data	*browser_md;
static int		 compiled;

static const char *
find_literal(const struct pair *list, const char *agent)
{
	const struct pair	*p;

	for (p = list; p->key != NULL; p++) {
		if (strcasestr(agent, p->key) != NULL)
			return (p->val);
	}

	return (NULL);
}

int
ats_ua_init(void)
{
	pcre2_code	*code;
	PCRE2_SIZE	 erroff;
	int		 err, i;
	char		 pattern[256];

	for (i = 0; i < N_BROWSERS; i++) {
		(void)snprintf(pattern, sizeof(pattern), "%s.*?([0-9\\.]+)",
		    browsers[i].key);
		code = pcre2_compile((PCRE2_SPTR)pattern, PCRE2_ZERO_TERMINATED,
		    PCRE2_CASELESS, &err, &erroff, NULL);
		if (code == NULL) {
			kore_log(LOG_ERR, "ua: compile '%s' failed at %zu",
			    pattern, erroff);
			return (-1);
		}
		browser_re[i] = code;
	}

	browser_md = pcre2_match_data_create(4, NULL);
	if (browser_md == NULL) {
		kore_log(LOG_ERR, "ua: match data allocation failed");
		return (-1);
	}

	compiled = 1;

	return (0);
}

static const char *
match_browser(const char *agent)
{
	int	i;

	for (i = 0; i < N_BROWSERS; i++) {
		if (pcre2_match(browser_re[i], (PCRE2_SPTR)agent,
		    PCRE2_ZERO_TERMINATED, 0, 0, browser_md, NULL) >= 0)
			return (browsers[i].val);
	}

	return (NULL);
}

void
ats_ua_detect(const char *agent, struct ats_ua *out)
{
	const char	*v;

	memset(out, 0, sizeof(*out));
	out->platform = "";
	out->browser = "";

	if (agent == NULL)
		return;

	if (!compiled && ats_ua_init() == -1)
		return;

	if ((v = find_literal(platforms, agent)) != NULL)
		out->platform = v;
	else
		out->platform = "Unknown Platform";

	if (find_literal(robots, agent) != NULL) {
		out->is_robot = 1;
		/* setRobot() falls through to setMobile(). */
		if (find_literal(mobiles, agent) != NULL)
			out->is_mobile = 1;
		return;
	}

	if ((v = match_browser(agent)) != NULL) {
		out->browser = v;
		if (find_literal(mobiles, agent) != NULL)
			out->is_mobile = 1;
		return;
	}

	if (find_literal(mobiles, agent) != NULL)
		out->is_mobile = 1;
}
