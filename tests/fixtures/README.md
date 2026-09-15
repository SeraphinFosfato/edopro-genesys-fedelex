# Fixtures

Byte-exact inputs for `banlist_tests`. They are data, not code: nothing here is
generated at test time, because a test that generates its own input also
generates its own blind spots.

Required:

- `banlist.json` + `banlist.json.sig` — **the real published artifact**, fetched
  from the public endpoint. It is the only fixture signed by the actual
  operational key, and therefore the only one that catches a divergence between
  what the publisher signs and what this client accepts.
- `test_key.pub` / `test_key.sec` — a throwaway keypair, generated once and
  committed. Used through `VerifySignatureWith` for the cases the real key
  cannot cover (wrong key, reserve key, tampered bytes, crafted versions).
  It is worth nothing and protects nothing; the real private keys have never
  existed on this machine and never will.
- crafted payloads for the schema cases: a non-integer `format_version`, an
  unknown `macro`, a `reason` present but empty, a duplicate `id`.
