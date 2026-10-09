#ifndef TLS_CLIENT_H
#define TLS_CLIENT_H

// FASE 83: an encrypted (TLS) client connection for libevent bufferevents.
//
// Why a filter and not bufferevent_openssl_socket_new(): the Windows build
// links the prebuilt libevent from the vcpkg cache, which is compiled
// WITHOUT its OpenSSL support (no libevent_openssl, no
// event2/bufferevent_ssl.h). Rebuilding it would add a dependency we do not
// control, so the TLS state machine lives here, on top of the OpenSSL that
// curl already brings in on every platform, as a bufferevent_filter. One
// code path for Linux and Windows, instead of a libevent-openssl one that
// Windows could never run.
//
// Certificate verification is not optional and has no off switch: the
// certificate must chain to a trusted root AND match `hostname`
// (SNI = hostname). There is deliberately no "verify=none" anywhere in this
// module, not even behind an option.
//
// Trust store:
//   - Options::ca_file set  -> ONLY that file is trusted (same priority as
//     the explicit cacert.pem the curl handles honour, gframe/curl.h).
//   - Windows               -> the system "ROOT" store (D249 point 1: the
//     same store curl is pointed at with CURLSSLOPT_NATIVE_CA).
//   - without ca_file, on every platform, ISRG Root X1 and X2 are trusted too
//     (gframe/tls_roots.h): Windows may not have downloaded them yet, and
//     only CryptoAPI triggers that download, never OpenSSL reading the store.
//     They add to the store above, never replace it, and the name check is
//     untouched.
//   - elsewhere             -> OpenSSL's default paths (which also honour the
//     SSL_CERT_FILE / SSL_CERT_DIR environment variables) plus the usual
//     distro locations, so a portable build running on a distro other than
//     the one that built it still finds a root bundle.
//
// Threading: the filter is created WITHOUT BEV_OPT_THREADSAFE on purpose.
// libevent's own locking would give the filter and the raw socket two locks
// taken in opposite orders by the loop thread (socket, then filter) and by a
// writer thread (filter, then socket). Instead everything is serialised on
// the RAW socket's lock: the event loop already holds it whenever it enters
// the filter, and every writer must hold it too (`*raw_socket` is returned
// so the caller can bufferevent_lock()/unlock() it around bufferevent_write
// on the returned bufferevent; DuelClient::WriteToServer does exactly that).
//
// That includes libevent's own reference counting of the filter: nothing in
// this module uses bufferevent_trigger_event(..., BEV_TRIG_DEFER_CALLBACKS) or
// BEV_OPT_DEFER_CALLBACKS on the filter, because those take and drop
// references on it under its (nonexistent) lock while a writer thread does
// the same under the raw lock. Events that must reach the caller later
// (CONNECTED, ERROR, EOF) are queued with event_base_once() instead, whose
// callback takes the raw lock before calling the user's eventcb.

#include <string>
#include <event2/event.h>
#include <event2/bufferevent.h>

struct sockaddr;

namespace ygo::tls {

struct Options {
	std::string hostname; // SNI, and the name the certificate must be valid for
	std::string ca_file;  // optional, see above
	// One line per failure, saying WHY (certificate, name, network). May be null.
	void (*log)(const std::string&){ nullptr };
};

// Starts connecting to `addr` and returns the bufferevent to use in place of
// a plain socket one, or nullptr (with the reason sent to Options::log) when
// the TLS state could not even be set up.
//   - `readcb`/`eventcb`/`cbarg` are installed on it, with the usual meaning,
//     except that BEV_EVENT_CONNECTED is delivered only once the handshake
//     has completed and the certificate has been verified. A failure at any
//     point (TCP, handshake, certificate, name) is BEV_EVENT_ERROR, and the
//     reason has already gone to Options::log by then.
//   - reading is already enabled; writing works as usual afterwards.
//   - `*raw_socket` receives the underlying socket bufferevent (see the
//     threading note). Freeing the returned bufferevent frees it too.
bufferevent* NewClient(event_base* base, const sockaddr* addr, int addrlen, const Options& options,
					   bufferevent_data_cb readcb, bufferevent_event_cb eventcb, void* cbarg,
					   bufferevent** raw_socket);

}

#endif //TLS_CLIENT_H
