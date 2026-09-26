#ifndef CLIENT_UPDATE_VERSION_H
#define CLIENT_UPDATE_VERSION_H

// This client's OWN, dedicated build number for the update manifest's
// min_supported check (design/client-update.md §9, FASE 36). Deliberately
// separate from the other two numbers already in this codebase that look
// similar but answer a different question:
//
//   - config.h's CLIENT_VERSION is the network handshake number. It travels
//     byte-for-byte with HostInfo (network.h) and must stay identical to
//     upstream EDOPro — it says whether two clients can duel each other,
//     not which one is fresher.
//   - EDOPRO_VERSION_MAJOR/MINOR/PATCH is Project Ignis' own upstream
//     numbering. We follow it, we do not govern it.
//
// CLIENT_UPDATE_VERSION is ours: monotonically increasing, bumped once per
// release of THIS fork, with no arithmetic relationship to either number
// above. It is what update::IsClientSupported() (update_verify.h) compares
// against a manifest's min_supported, and what a future release's manifest
// publishes as its own `version`.
//
// Kept in a header of its own (not update_verify.h) so bumping it at release
// time touches exactly one file and never the verification module itself.

namespace ygo::update {

// v0.0.4-alpha (git tag) is build 4. Bump by exactly one at every release of
// this fork that should be distinguishable to a manifest's min_supported.
inline constexpr int CLIENT_UPDATE_VERSION = 4;

}

#endif //CLIENT_UPDATE_VERSION_H
