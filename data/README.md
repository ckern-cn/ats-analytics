# Runtime data

This directory is reserved for the optional GeoIP database.

The MaxMind GeoLite2 country database is **not** distributed with this
repository. Obtain it with your own MaxMind account, for example:

```sh
geoipupdate -f /etc/GeoIP-ats.conf -d /webapps/ats-c/data
```

or copy a GeoLite2-Country.mmdb you are licensed to use to:

```
data/GeoLite2-Country.mmdb
```

If the file is absent the service still works; country names and flags are
simply empty. See `THIRD_PARTY_NOTICES.md` for the license and attribution
requirements.

SQLite databases, WAL files and JWTs are created next to the installed binary
(for example `/webapps/ats-c/data/`) and are ignored by git.
