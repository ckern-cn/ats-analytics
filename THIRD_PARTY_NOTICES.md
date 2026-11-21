# Third-party notices

ATS is distributed under the MIT license (see `LICENSE`). This file lists the
third-party components that are included in, or required by, the project.

## Bundled in this repository

### Kore 4.2.3

- Location: `ats-c/vendor/kore-4.2.3/`
- License: ISC-style (see `ats-c/vendor/kore-4.2.3/LICENSE`)
- Copyright (c) 2013-2022 Joris Vink
- Upstream: https://kore.io/

### cJSON 1.7.18

- Location: `ats-c/src/vendor/cJSON.c`, `cJSON.h`
- License: MIT
- Copyright (c) 2009-2017 Dave Gamble and cJSON contributors
- Upstream: https://github.com/DaveGamble/cJSON

### js-sha256 0.11.0

- Location: `public/res/js/sha256.min.js`
- License: MIT (the license header is embedded in the file)
- Copyright (c) 2014-2024 Chen, Yi-Cyuan
- Upstream: https://github.com/emn178/js-sha256

### Flag SVG assets

- Location: `public/res/flags/*.svg`
- The files use the circular flag style of the **circle-flags** project and are
  believed to be MIT-licensed. If you replace or regenerate them, update this
  notice with the actual source and license.
- Upstream: https://github.com/HatScripts/circle-flags

### Favicon and globe icon

- Location: `public/favicon.ico`, `public/res/globe.svg`
- These were carried over from the original deployment. Their original source
  and license are not recorded in this repository. Replace them with your own
  assets, or document their source, before publishing a fork.

## Ported / derived code

### CodeIgniter 4 UserAgent configuration

- The browser / platform / robot matching data in `ats-c/src/ua.c` is derived
  from CodeIgniter 4's `Config/UserAgents.php`.
- License: MIT
- Upstream: https://codeigniter.com/

### yitter/IdGenerator snowflake algorithm

- `ats-c/src/snowflake.c` is a C port of the snowflake ID algorithm used by
  yitter/IdGenerator.
- License: MIT
- Upstream: https://github.com/yitter/IdGenerator

### Original ATS PHP application

- The C service is a rewrite of the original personal ATS application. The
  original code is copyright its author and is not included in this repository.

## Not distributed

### MaxMind GeoLite2

- The GeoLite2 country database is **not** included in this repository.
- Obtain it from MaxMind with your own account and comply with the MaxMind
  GeoLite2 license / attribution requirements.
- The service reads the database from `ATS_GEOIP_DB_PATH`; if it is missing,
  country statistics are simply empty.
- Upstream: https://dev.maxmind.com/geoip/geolite2-free-geolocation-data/

## System libraries

The binary links against the system SQLite, libmaxminddb and PCRE2 libraries.
Those libraries are not vendored and are covered by their own licenses.
