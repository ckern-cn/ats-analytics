#!/bin/sh
# Generate src/schema_sql.h from db/schema.sql for the single binary.
set -eu

in=${1:?usage: gen-schema.sh schema.sql out.h}
out=${2:?usage: gen-schema.sh schema.sql out.h}

{
	echo '/* generated from db/schema.sql; do not edit */'
	echo 'static const char ats_schema_sql[] ='
	sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' -e 's/^/  "/' -e 's/$/\\n"/' "$in"
	echo '  ;'
} > "$out"
