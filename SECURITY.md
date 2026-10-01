# Security policy

Report vulnerabilities privately through GitHub:
**Security → Report a vulnerability** on this repository. Do not open a public issue.

In scope: the E0 trainer and UWP worker (`src/cpp/e0/`, `uwp/`) — notably job-file
and asset handling in `LocalState/inbox` ([docs/e0/job-protocol.md](docs/e0/job-protocol.md)) —
the build scripts and the CI workflows. Device Portal credentials, signing
certificates and console addresses must never be committed; report any found in
history.

Only the latest `main` is supported. Expect an acknowledgement within a week.
