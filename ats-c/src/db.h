#ifndef ATS_DB_H_INCLUDED
#define ATS_DB_H_INCLUDED

#include <stddef.h>

#include <sqlite3.h>

#include "cJSON.h"

struct sqlite3	*ats_db_open(char *errbuf, size_t errbuf_len);
void		 ats_db_close(void);
sqlite3		*ats_db_handle(void);

/* Create tables/indexes in a fresh database. Runs in the parent, pre-fork. */
int	ats_db_bootstrap(const char *path, char *errbuf, size_t errbuf_len);

/* Cheap liveness probe for /__health. */
int	ats_db_check(char *errbuf, size_t errbuf_len);

int	ats_db_prepare(const char *sql, sqlite3_stmt **out,
	    char *errbuf, size_t errbuf_len);
void	ats_db_finish(sqlite3_stmt *stmt);

int	ats_bind_text(sqlite3_stmt *stmt, int idx, const char *v);
int	ats_bind_int64(sqlite3_stmt *stmt, int idx, sqlite3_int64 v);
int	ats_bind_null(sqlite3_stmt *stmt, int idx);

enum ats_val_type {
	ATS_VAL_TEXT,
	ATS_VAL_INT64,
	ATS_VAL_NULL
};

struct ats_val {
	enum ats_val_type	type;
	const char		*txt;
	sqlite3_int64		num;
};

#define ATS_TXT(s)	((struct ats_val){ ATS_VAL_TEXT, (s), 0 })
#define ATS_NUM(n)	((struct ats_val){ ATS_VAL_INT64, NULL, (n) })
#define ATS_NULLV	((struct ats_val){ ATS_VAL_NULL, NULL, 0 })

int	ats_db_run(const char *sql, const struct ats_val *vals, int nvals,
	    char *errbuf, size_t errbuf_len);
int	ats_db_scalar(const char *sql, const struct ats_val *vals, int nvals,
	    sqlite3_int64 *out, char *errbuf, size_t errbuf_len);
int	ats_db_exec(const char *sql, char *errbuf, size_t errbuf_len);
sqlite3_int64 ats_db_count(const char *sql, char *errbuf, size_t errbuf_len);
int	ats_db_changes(void);

#endif
