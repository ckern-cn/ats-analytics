## Summary

<!-- What does this change and why? -->

## Related issue

<!-- Fixes #... -->

## Test plan

- [ ] `tools/ci-local.sh prod`
- [ ] `tools/ci-local.sh asan` (for code changes)
- [ ] Checked the login page, dashboard, CSV export and language switcher
- [ ] Checked `nginx -t` if deployment files changed

## Checklist

- [ ] The change is focused and does not include unrelated reformatting
- [ ] User-facing text is in `ats-c/src/lang/*.h`
- [ ] Docs are updated for behavior/configuration changes
- [ ] No secrets, database files, GeoIP databases or personal data are included
- [ ] Commits are signed off (`git commit -s`)
- [ ] I did not change the project name, logo or official release channels
