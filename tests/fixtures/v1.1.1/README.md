The adjacent main.cpp is the unmodified maintenance-code entry point from tag
v1.1.1 (commit 6edaf6f58fcca123369cf7e9bd579b6d10f0729f).
It is MIT-covered project code, not the original QIP client.

The signed E2E test compiles its worker/check-cycle logic with the current,
compatible lower-level headers and a temporary public key. This checks that
the new installer can hand profile preparation to its own new helper while
an older worker holds the installation lock, and that a preparation failure
returns a nonzero exit code even to that older worker. No release key is used.
