#ifndef SERVER_TLS_H
#define SERVER_TLS_H

// FASE 83: pure helpers for servers that declare `"tls": true` in
// configs.json (see design/launcher.md, "Stato dell'implementazione (FASE
// 83)"). No I/O, no network, no gframe dependency beyond nlohmann::json_fwd
// — same discipline as deep_link.h, so tests/ can link it standalone.
//
// A server that declares `tls` is identified by its NAME, not by the
// address that name resolves to: a name behind a CDN-ish front can resolve
// to several addresses that change, and the thing that proves "this is the
// server I meant" for an encrypted connection is the certificate verified
// against that very name (gframe/tls_client.h), not an IP list.

#include <cstdint>
#include <string>
#include <nlohmann/json_fwd.hpp>

namespace ygo::server_tls {

enum class TlsField {
	Absent,   // no "tls" key: the server behaves exactly as before FASE 83
	Enabled,  // "tls": true
	Disabled, // "tls": false (same as Absent, spelled out)
	Invalid,  // "tls" present but not a JSON boolean
};

// `entry` is one element of configs.json's "servers" array. A non-boolean
// value is Invalid on purpose and the caller must reject the whole entry:
// reading `"tls": "true"` or `"tls": 1` as "plain" would silently send
// seat passwords in clear to a server the operator meant to encrypt.
TlsField ParseTlsField(const nlohmann::json& entry);

// DNS names compare ASCII case-insensitively and a single trailing '.' (the
// root label) is ignored on either side. Empty never matches anything.
bool SameServerName(const std::string& a, const std::string& b);

// True when a link naming `link_name`:`link_port` points at the TLS server
// configured as `configured_name`:`configured_port`. Name AND port: the
// same name on another port is a different service.
bool TlsServerMatches(const std::string& configured_name, uint16_t configured_port,
					  const std::string& link_name, uint16_t link_port);

}

#endif //SERVER_TLS_H
