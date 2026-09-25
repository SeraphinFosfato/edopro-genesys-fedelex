#ifndef SHA256_H
#define SHA256_H

// Minimal, dependency-free SHA-256 (FIPS 180-4). Vendored for the same
// reason tweetnacl/ is vendored (design/banlist-distribution.md, "Crypto"):
// the project builds for five platforms and the Linux path deliberately
// avoids vcpkg, so a small self-contained implementation beats a new
// external dependency. Used by update_verify.h to check each downloaded
// file's SHA-256 against the value the signed manifest commits to
// (design/client-update.md, "SHA-256 per file").
//
// Public domain (based on the reference FIPS 180-4 algorithm description;
// no third-party code copied in).

#include <cstdint>
#include <string>

namespace ygo {

// Returns the SHA-256 of `data` as 64 lowercase hex characters.
std::string Sha256Hex(const std::string& data);

}

#endif //SHA256_H
