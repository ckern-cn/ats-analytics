#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sqlite3.h>

#include <kore/kore.h>

#include "db.h"
#include "schema_sql.h"

struct stmt_entry {
	char			*sql;
	sqlite3_stmt		*stmt;
	struct stmt_entry	*next;
};

static sqlite3			*dbh;
static struct stmt_entry	*stmts;

static const char *db_pragmas[] = {
	"PRAGMA journal_mode = WAL",
	"PRAGMA foreign_keys = ON",
	"PRAGMA busy_timeout = 5000",
	"PRAGMA synchronous = NORMAL",
	"PRAGMA temp_store = MEMORY",
	"PRAGMA wal_autocheckpoint = 1000",
	NULL
};

int
ats_db_bootstrap(const char *path, char *errbuf, size_t errbuf_len)
{
	sqlite3	*db;
	char	*emsg;
	int	 i, r;

	if (path == NULL || *path == '\0') {
		(void)snprintf(errbuf, errbuf_len, "empty database path");
		return (KORE_RESULT_ERROR);
	}

	db = NULL;
	r = sqlite3_open_v2(path, &db,
	    SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
	if (r != SQLITE_OK) {
		(void)snprintf(errbuf, errbuf_len, "bootstrap open(%s): %s",
		    path, db != NULL ? sqlite3_errmsg(db) : sqlite3_errstr(r));
		if (db != NULL)
			sqlite3_close(db);
		return (KORE_RESULT_ERROR);
	}

	for (i = 0; db_pragmas[i] != NULL; i++) {
		if (sqlite3_exec(db, db_pragmas[i], NULL, NULL, &emsg)
		    != SQLITE_OK) {
			(void)snprintf(errbuf, errbuf_len, "%s: %s",
			    db_pragmas[i], emsg != NULL ? emsg : "error");
			if (emsg != NULL)
				sqlite3_free(emsg);
			sqlite3_close(db);
			return (KORE_RESULT_ERROR);
		}
	}

	if (sqlite3_exec(db, ats_schema_sql, NULL, NULL, &emsg) != SQLITE_OK) {
		(void)snprintf(errbuf, errbuf_len, "schema: %s",
		    emsg != NULL ? emsg : "error");
		if (emsg != NULL)
			sqlite3_free(emsg);
		sqlite3_close(db);
		return (KORE_RESULT_ERROR);
	}

	sqlite3_close(db);

	return (KORE_RESULT_OK);
}

int
ats_db_check(char *errbuf, size_t errbuf_len)
{
	sqlite3_int64	one;

	return (ats_db_scalar("SELECT 1", NULL, 0, &one, errbuf,
	    errbuf_len));
}

sqlite3 *
ats_db_open(char *errbuf, size_t errbuf_len)
{
	const char	*path;
	char		scratch[256];
	int		i, r;

	if (dbh != NULL)
		return (dbh);

	path = getenv("ATS_DB_PATH");
	if (path == NULL) {
		(void)snprintf(errbuf, errbuf_len, "ATS_DB_PATH is not set");
		return (NULL);
	}

	r = sqlite3_open_v2(path, &dbh,
	    SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
	if (r != SQLITE_OK) {
		(void)snprintf(errbuf, errbuf_len, "open(%s): %s", path,
		    dbh != NULL ? sqlite3_errmsg(dbh) : sqlite3_errstr(r));
		ats_db_close();
		return (NULL);
	}

	for (i = 0; db_pragmas[i] != NULL; i++) {
		if (ats_db_exec(db_pragmas[i], scratch, sizeof(scratch))
		    == KORE_RESULT_ERROR) {
			(void)snprintf(errbuf, errbuf_len, "%s -> %s",
			    db_pragmas[i], scratch);
			ats_db_close();
			return (NULL);
		}
	}

	return (dbh);
}

sqlite3 *
ats_db_handle(void)
{
	return (dbh);
}

void
ats_db_close(void)
{
	struct stmt_entry	*e, *next;

	for (e = stmts; e != NULL; e = next) {
		next = e->next;
		if (e->stmt != NULL)
			sqlite3_finalize(e->stmt);
		free(e->sql);
		free(e);
	}

	stmts = NULL;

	if (dbh != NULL) {
		sqlite3_close(dbh);
		dbh = NULL;
	}
}

int
ats_db_prepare(const char *sql, sqlite3_stmt **out,
    char *errbuf, size_t errbuf_len)
{
	struct stmt_entry	*e;
	sqlite3_stmt		*stmt;

	if (dbh == NULL && ats_db_open(errbuf, errbuf_len) == NULL)
		return (KORE_RESULT_ERROR);

	for (e = stmts; e != NULL; e = e->next) {
		if (strcmp(e->sql, sql))
			continue;

		if (sqlite3_reset(e->stmt) != SQLITE_OK) {
			(void)snprintf(errbuf, errbuf_len, "reset: %s",
			    sqlite3_errmsg(dbh));
			return (KORE_RESULT_ERROR);
		}
		sqlite3_clear_bindings(e->stmt);

		*out = e->stmt;
		return (KORE_RESULT_OK);
	}

	if (sqlite3_prepare_v2(dbh, sql, -1, &stmt, NULL) != SQLITE_OK) {
		(void)snprintf(errbuf, errbuf_len, "prepare(%s): %s", sql,
		    sqlite3_errmsg(dbh));
		return (KORE_RESULT_ERROR);
	}

	if ((e = malloc(sizeof(*e))) == NULL)
		fatal("out of memory");

	e->sql = strdup(sql);
	e->stmt = stmt;
	e->next = stmts;
	stmts = e;

	*out = stmt;
	return (KORE_RESULT_OK);
}

void
ats_db_finish(sqlite3_stmt *stmt)
{
	if (stmt != NULL)
		sqlite3_reset(stmt);
}

int
ats_bind_text(sqlite3_stmt *stmt, int idx, const char *v)
{
	if (v == NULL)
		return (ats_bind_null(stmt, idx));

	return (sqlite3_bind_text(stmt, idx, v, -1,
	    SQLITE_TRANSIENT) == SQLITE_OK);
}

int
ats_bind_int64(sqlite3_stmt *stmt, int idx, sqlite3_int64 v)
{
	return (sqlite3_bind_int64(stmt, idx, v) == SQLITE_OK);
}

int
ats_bind_null(sqlite3_stmt *stmt, int idx)
{
	return (sqlite3_bind_null(stmt, idx) == SQLITE_OK);
}

static int
bind_vals(sqlite3_stmt *stmt, const struct ats_val *vals, int nvals)
{
	int	i;

	for (i = 0; i < nvals; i++) {
		switch (vals[i].type) {
		case ATS_VAL_TEXT:
			if (ats_bind_text(stmt, i + 1, vals[i].txt)
			    != KORE_RESULT_OK)
				return (KORE_RESULT_ERROR);
			break;
		case ATS_VAL_INT64:
			if (ats_bind_int64(stmt, i + 1, vals[i].num)
			    != KORE_RESULT_OK)
				return (KORE_RESULT_ERROR);
			break;
		case ATS_VAL_NULL:
			if (ats_bind_null(stmt, i + 1) != KORE_RESULT_OK)
				return (KORE_RESULT_ERROR);
			break;
		default:
			fatal("db: unknown bind type %d", vals[i].type);
		}
	}

	return (KORE_RESULT_OK);
}

int
ats_db_run(const char *sql, const struct ats_val *vals, int nvals,
    char *errbuf, size_t errbuf_len)
{
	sqlite3_stmt	*stmt;
	int		r;

	if (ats_db_prepare(sql, &stmt, errbuf, errbuf_len) == KORE_RESULT_ERROR)
		return (KORE_RESULT_ERROR);

	if (bind_vals(stmt, vals, nvals) != KORE_RESULT_OK) {
		(void)snprintf(errbuf, errbuf_len, "bind: %s",
		    sqlite3_errmsg(dbh));
		sqlite3_reset(stmt);
		return (KORE_RESULT_ERROR);
	}

	for (;;) {
		r = sqlite3_step(stmt);

		if (r == SQLITE_DONE)
			break;
		if (r != SQLITE_ROW) {
			(void)snprintf(errbuf, errbuf_len, "step: %s",
			    sqlite3_errmsg(dbh));
			sqlite3_reset(stmt);
			return (KORE_RESULT_ERROR);
		}
	}

	sqlite3_reset(stmt);

	return (KORE_RESULT_OK);
}

int
ats_db_scalar(const char *sql, const struct ats_val *vals, int nvals,
    sqlite3_int64 *out, char *errbuf, size_t errbuf_len)
{
	sqlite3_stmt	*stmt;
	int		r;

	if (ats_db_prepare(sql, &stmt, errbuf, errbuf_len) == KORE_RESULT_ERROR)
		return (KORE_RESULT_ERROR);

	if (bind_vals(stmt, vals, nvals) != KORE_RESULT_OK) {
		(void)snprintf(errbuf, errbuf_len, "bind: %s",
		    sqlite3_errmsg(dbh));
		sqlite3_reset(stmt);
		return (KORE_RESULT_ERROR);
	}

	r = sqlite3_step(stmt);
	if (r == SQLITE_ROW) {
		if (out != NULL)
			*out = sqlite3_column_int64(stmt, 0);
		sqlite3_reset(stmt);
		return (KORE_RESULT_OK);
	}

	if (r == SQLITE_DONE)
		(void)snprintf(errbuf, errbuf_len, "no row");
	else
		(void)snprintf(errbuf, errbuf_len, "step: %s",
		    sqlite3_errmsg(dbh));

	sqlite3_reset(stmt);

	return (KORE_RESULT_ERROR);
}

int
ats_db_exec(const char *sql, char *errbuf, size_t errbuf_len)
{
	return (ats_db_run(sql, NULL, 0, errbuf, errbuf_len));
}

sqlite3_int64
ats_db_count(const char *sql, char *errbuf, size_t errbuf_len)
{
	sqlite3_int64	out;

	if (ats_db_scalar(sql, NULL, 0, &out, errbuf, errbuf_len)
	    == KORE_RESULT_ERROR)
		return (-1);

	return (out);
}

int
ats_db_changes(void)
{
	if (dbh == NULL)
		return (0);

	return (sqlite3_changes(dbh));
}

cJSON *
ats_row_json(sqlite3_stmt *stmt)
{
	cJSON			*row;
	const unsigned char	*txt;
	const char		*name;
	int			i, n, type;

	row = cJSON_CreateObject();
	if (row == NULL)
		return (NULL);

	n = sqlite3_column_count(stmt);

	for (i = 0; i < n; i++) {
		name = sqlite3_column_name(stmt, i);
		if (name == NULL)
			continue;

		type = sqlite3_column_type(stmt, i);

		switch (type) {
		case SQLITE_INTEGER:
			cJSON_AddNumberToObject(row, name,
			    (double)sqlite3_column_int64(stmt, i));
			break;
		case SQLITE_FLOAT:
			cJSON_AddNumberToObject(row, name,
			    sqlite3_column_double(stmt, i));
			break;
		case SQLITE_NULL:
			cJSON_AddNullToObject(row, name);
			break;
		default:
			txt = sqlite3_column_text(stmt, i);
			cJSON_AddStringToObject(row, name,
			    (txt == NULL) ? "" : (const char *)txt);
			break;
		}
	}

	return (row);
}
