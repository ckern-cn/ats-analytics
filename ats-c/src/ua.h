#ifndef ATS_UA_H_INCLUDED
#define ATS_UA_H_INCLUDED

struct ats_ua {
	const char	*platform;
	const char	*browser;
	int		is_mobile;
	int		is_robot;
};

/* Compile the browser patterns once, before workers fork. */
int	ats_ua_init(void);

/*
 * agent == NULL means the User-Agent header was absent. CI4's UserAgent
 * constructor then skips compileData(), leaving every field empty.
 */
void	ats_ua_detect(const char *agent, struct ats_ua *out);

#endif
