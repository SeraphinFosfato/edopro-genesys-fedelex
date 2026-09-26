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
- `update_manifest.json` + `update_manifest.json.sig` — FASE 37, cancello 1
  (design/client-update.md/PHASES.md, "Il primo manifesto"). NOT signed with
  the real update key: signed with the same throwaway `test_key.sec` above,
  by actually running `banlist/scripts/sign_update_manifest.py` (vault repo)
  against this document. This is the one fixture that proves Python and C++
  agree byte-for-byte on what `UPDATE_DOMAIN` ("fedelex-update-v1") means —
  a confirmation a string comparison between the two sources could never
  give, because it exercises the real signing code path on one side and the
  real verification code path on the other.
- `update_manifest.banlist_style.sig` — the SAME document above, signed with
  the SAME test key, but by `sign_banlist.py` instead (raw bytes, no domain
  prefix). Cancello 2: this signature must NOT verify as an update manifest.
  Regenerating either of these two `.sig` files: the seed used is the first
  32 bytes of `test_key.sec`, base64-encoded
  (`chbF2i+SiYLZNSHaUvyqnBHUAJWy4VF1g0cDkKeFWcU=` as of this writing) —
  passed as `UPDATE_SIGNING_KEY` / `BANLIST_SIGNING_KEY` respectively to the
  two scripts in the vault repo, never committed as a real secret anywhere
  (it is a throwaway key with the same custody discipline as `test_key.sec`
  itself).
