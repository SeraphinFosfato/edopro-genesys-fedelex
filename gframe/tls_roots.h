#ifndef TLS_ROOTS_H
#define TLS_ROOTS_H

// FASE 83b: root certificates the encrypted connection (gframe/tls_client.h)
// trusts IN ADDITION to the system store, when no explicit CA file is given.
//
// Why: on Windows OpenSSL reads the "ROOT" store directly (tls_client.cpp,
// LoadSystemRoots), and Windows downloads missing roots on demand only when
// CryptoAPI does the verifying, never when OpenSSL merely reads the store. A
// machine that never visited a Let's Encrypt site can therefore lack ISRG
// Root X1/X2, and the server's certificate would be refused through no fault
// of anyone. They are public certificates, embedded as published; they do not
// replace the system store and change nothing about the name check.
//
// Pure data, no OpenSSL types, so tests/ can check the fingerprints.

#include <cstddef>

namespace ygo::tls {

struct BundledRoot {
	const char* name;
	const char* pem;
	const char* sha256; // lowercase hex of the DER, verified before use (tls_client.cpp)
};

extern const BundledRoot kBundledRoots[];
extern const size_t kBundledRootCount;

}

#endif //TLS_ROOTS_H
