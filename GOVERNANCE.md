# Governance

ATS is a personal, self-hosted traffic analytics project. The governance model
is intentionally simple: free functionality, voluntary donations, low-barrier
contributions, and maintainer-controlled brand and official releases.

## Maintainer

- ywliu — GitHub [@ckern-cn](https://github.com/ckern-cn),
  Gitee [@mywliu](https://gitee.com/mywliu)

The maintainer is the copyright holder, release manager, and final decision
maker for the project.

## Principles

1. **Free functionality.** All features are available in the open-source code.
   There is no paid tier, feature flag, time limit, or license key.
2. **Voluntary donations.** Donations are optional and do not buy features,
   priority, roadmap influence, or official support. See `DONATE.md`.
3. **Low-barrier contributions.** Issues, pull requests, documentation and
   translations are welcome. There is no CLA; contributions use the DCO
   sign-off and the MIT License.
4. **Maintainer-controlled brand and releases.** Official binaries and the ATS
   brand are controlled by the maintainer. Forks are welcome but must rebrand.
   See `TRADEMARK.md`.

## Decisions

- Day-to-day changes use lazy consensus in issues and pull requests.
- The maintainer has the final say on scope, architecture, security fixes,
  releases and brand use.
- Security reports are handled privately per `SECURITY.md`.
- Large changes should start with an issue or discussion before a big pull
  request.

## Contributions and licensing

- Contributions are accepted under the MIT License (inbound = outbound).
- Contributors sign off commits using the Developer Certificate of Origin
  (`git commit -s`). No separate CLA is required.
- By contributing, you confirm you have the right to submit the work under
  these terms.
- See `CONTRIBUTING.md` for the practical workflow.

## Releases

- Only the maintainer publishes official releases, from the canonical GitHub
  and Gitee repositories.
- Releases follow Semantic Versioning where practical and are recorded in
  `CHANGELOG.md`.
- Official artifacts are built from a tagged commit, tested, checksummed and
  (optionally) signed. See `RELEASING.md`.
- Forks may publish their own builds under a different name and identity.

## Donations

Donations are handled separately from development and do not create a
commercial relationship or service-level agreement. They do not affect
feature availability or release decisions.

## Changes to this document

The maintainer may update this document as the project evolves. Material
changes will be noted in `CHANGELOG.md`.
