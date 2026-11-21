# Security policy

## Reporting a vulnerability

Please **do not** open a public issue for a suspected vulnerability.

Use GitHub's private vulnerability reporting (Security → Report a
vulnerability) for this repository. If that is unavailable, contact the
maintainer through the address listed in the repository profile.

Please include:

- the affected version / commit,
- a minimal reproduction (request, config, or fixture),
- the impact you believe it has,
- whether the issue is already public.

You will get an acknowledgement as soon as possible. Fixes and a coordinated
disclosure date are handled on a best-effort basis.

## Supported versions

Only the current `main` branch and the latest tagged release are supported.
Deployments are expected to run the current binary; old releases do not receive
backports.

## Scope

In scope:

- `ats-c/` (the C service)
- `deploy/` (systemd, nginx, cron, install scripts)
- `tools/` (maintenance and test scripts)
- the tracking endpoints and admin UI

Out of scope:

- the example domain, certificate paths and IP addresses in the repository,
- issues that require an already-compromised server or root access,
- missing hardening that is explicitly documented as a deployment choice
  (for example, exposing the admin UI to the whole internet),
- traffic inflation through a forged `Referer` from a non-browser client:
  client-side analytics cannot authenticate a visitor. Rate limiting and
  retention are the documented mitigations.

## Security-relevant configuration

- Set a long random admin password and keep `/etc/ats-capi.env` at mode 0600.
- Keep the backend bound to `127.0.0.1`; do not expose port 3001 directly.
- Terminate TLS in nginx and keep HSTS enabled.
- Restrict the admin routes to your own networks when possible.
- Do not commit `data/*.db`, `data/jwt.key` or `/etc/ats-capi.env`.
