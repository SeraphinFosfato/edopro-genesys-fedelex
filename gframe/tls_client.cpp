#include "tls_client.h"
// Windows: the platform headers must come before OpenSSL's (wincrypt.h
// macros clash with X509_NAME and friends; openssl/ossl_typ.h undoes them).
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <wincrypt.h>
#else
#include <sys/stat.h>
#endif
#include <memory>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <event2/buffer.h>
#include <event2/util.h>

namespace ygo::tls {
namespace {

struct Ctx {
	SSL_CTX* sctx{ nullptr };
	SSL* ssl{ nullptr };
	bufferevent* filter{ nullptr };
	bufferevent* raw{ nullptr };
	bufferevent_data_cb user_read{ nullptr };
	bufferevent_event_cb user_event{ nullptr };
	void* user_arg{ nullptr };
	std::string hostname;
	void (*log)(const std::string&){ nullptr };
	bool done{ false };   // handshake finished and certificate verified
	bool failed{ false }; // fatal: everything after this is discarded
	bool eof{ false };    // the peer closed the TLS session cleanly
};

void Log(const Ctx* t, const std::string& line) {
	if(t->log)
		t->log(line);
}

std::string DrainErrors() {
	std::string out;
	char buf[256];
	while(auto e = ERR_get_error()) {
		ERR_error_string_n(e, buf, sizeof(buf));
		if(!out.empty())
			out += "; ";
		out += buf;
	}
	return out.empty() ? std::string("nessun dettaglio da OpenSSL") : out;
}

#ifdef _WIN32
// The "ROOT" system store, parsed into OpenSSL's store. Returns how many
// certificates were added (zero means there is nothing to verify against).
int LoadSystemRoots(SSL_CTX* sctx) {
	HCERTSTORE store = CertOpenSystemStoreW(0, L"ROOT");
	if(!store)
		return 0;
	auto* xs = SSL_CTX_get_cert_store(sctx);
	int added = 0;
	for(PCCERT_CONTEXT c = CertEnumCertificatesInStore(store, nullptr); c; c = CertEnumCertificatesInStore(store, c)) {
		const unsigned char* p = c->pbCertEncoded;
		if(X509* x = d2i_X509(nullptr, &p, static_cast<long>(c->cbCertEncoded))) {
			if(X509_STORE_add_cert(xs, x) == 1)
				++added;
			X509_free(x);
		}
	}
	CertCloseStore(store, 0);
	return added;
}
#else
bool FileExists(const char* path) {
	struct stat st;
	return stat(path, &st) == 0;
}
#endif

bool LoadTrust(const Ctx* t, const Options& options) {
	if(!options.ca_file.empty()) {
		if(SSL_CTX_load_verify_locations(t->sctx, options.ca_file.c_str(), nullptr) != 1) {
			Log(t, "TLS: impossibile leggere il file dei certificati '" + options.ca_file + "': " + DrainErrors());
			return false;
		}
		return true;
	}
#ifdef _WIN32
	if(LoadSystemRoots(t->sctx) == 0) {
		Log(t, "TLS: l'archivio dei certificati radice di Windows e' vuoto o non leggibile");
		return false;
	}
#else
	SSL_CTX_set_default_verify_paths(t->sctx);
	// A libssl shipped inside a portable build keeps the default paths of
	// the distro that built it: also try where the common ones keep theirs.
	static const char* const bundles[] = {
		"/etc/ssl/certs/ca-certificates.crt", // Debian, Ubuntu, Arch
		"/etc/pki/tls/certs/ca-bundle.crt",   // Fedora, RHEL
		"/etc/ssl/cert.pem",                  // Alpine, macOS
	};
	for(auto* path : bundles) {
		if(FileExists(path))
			SSL_CTX_load_verify_locations(t->sctx, path, nullptr);
	}
	SSL_CTX_load_verify_locations(t->sctx, nullptr, "/etc/ssl/certs");
	ERR_clear_error(); // a missing optional location above is not an error
#endif
	return true;
}

// Moves whatever OpenSSL wants to send into the socket's output buffer.
void FlushToSocket(Ctx* t) {
	auto* wbio = SSL_get_wbio(t->ssl);
	auto* out = bufferevent_get_output(t->raw);
	char buf[16384];
	int n;
	while((n = BIO_read(wbio, buf, sizeof(buf))) > 0)
		evbuffer_add(out, buf, static_cast<size_t>(n));
}

void Fail(Ctx* t, const std::string& why) {
	if(t->failed)
		return;
	t->failed = true;
	Log(t, why);
	bufferevent_trigger_event(t->filter, BEV_EVENT_ERROR, BEV_TRIG_DEFER_CALLBACKS);
}

void FailHandshake(Ctx* t) {
	const long verify = SSL_get_verify_result(t->ssl);
	if(verify != X509_V_OK) {
		Fail(t, "TLS: certificato non accettato per '" + t->hostname + "': " + X509_verify_cert_error_string(verify));
		ERR_clear_error();
	} else {
		Fail(t, "TLS: handshake con '" + t->hostname + "' non riuscito: " + DrainErrors());
	}
}

// Runs the TLS state as far as it can go without more network input and
// appends decrypted bytes to `plain` (which is null before the handshake
// can have finished).
size_t Pump(Ctx* t, evbuffer* plain) {
	if(t->failed)
		return 0;
	bool just_done = false;
	if(!t->done) {
		const int r = SSL_do_handshake(t->ssl);
		FlushToSocket(t);
		if(r != 1) {
			const int e = SSL_get_error(t->ssl, r);
			if(e != SSL_ERROR_WANT_READ && e != SSL_ERROR_WANT_WRITE)
				FailHandshake(t);
			else
				ERR_clear_error();
			return 0;
		}
		// Belt and braces: with SSL_VERIFY_PEER a bad certificate already
		// failed the handshake above, but nothing may pass as "done" unless
		// the verification result says so.
		if(SSL_get_verify_result(t->ssl) != X509_V_OK) {
			FailHandshake(t);
			return 0;
		}
		t->done = true;
		just_done = true;
	}
	if(!plain)
		return 0; // cannot be reached (the handshake needs server data first); never write to null
	size_t produced = 0;
	char buf[16384];
	for(;;) {
		const int r = SSL_read(t->ssl, buf, sizeof(buf));
		if(r > 0) {
			evbuffer_add(plain, buf, static_cast<size_t>(r));
			produced += static_cast<size_t>(r);
			continue;
		}
		const int e = SSL_get_error(t->ssl, r);
		if(e == SSL_ERROR_ZERO_RETURN) {
			if(!t->eof) {
				t->eof = true;
				bufferevent_trigger_event(t->filter, BEV_EVENT_EOF, BEV_TRIG_DEFER_CALLBACKS);
			}
		} else if(e != SSL_ERROR_WANT_READ && e != SSL_ERROR_WANT_WRITE) {
			Fail(t, "TLS: errore durante la lettura dal server: " + DrainErrors());
		} else {
			ERR_clear_error();
		}
		break;
	}
	FlushToSocket(t);
	if(just_done)
		bufferevent_trigger_event(t->filter, BEV_EVENT_CONNECTED, BEV_TRIG_DEFER_CALLBACKS);
	return produced;
}

bufferevent_filter_result FilterIn(evbuffer* src, evbuffer* dst, ev_ssize_t, bufferevent_flush_mode, void* ctx) {
	auto* t = static_cast<Ctx*>(ctx);
	char buf[16384];
	int n;
	while((n = evbuffer_remove(src, buf, sizeof(buf))) > 0) {
		if(!t->failed)
			BIO_write(SSL_get_rbio(t->ssl), buf, n);
	}
	if(t->failed)
		return BEV_ERROR;
	return Pump(t, dst) ? BEV_OK : BEV_NEED_MORE;
}

bufferevent_filter_result FilterOut(evbuffer* src, evbuffer*, ev_ssize_t, bufferevent_flush_mode, void* ctx) {
	auto* t = static_cast<Ctx*>(ctx);
	if(t->failed) {
		evbuffer_drain(src, evbuffer_get_length(src));
		return BEV_ERROR;
	}
	if(!t->done)
		return BEV_NEED_MORE; // stays queued; nothing is written before the handshake ends
	char buf[16384];
	int n;
	while((n = evbuffer_remove(src, buf, sizeof(buf))) > 0) {
		if(SSL_write(t->ssl, buf, n) != n) {
			Fail(t, "TLS: errore durante la scrittura verso il server: " + DrainErrors());
			return BEV_ERROR;
		}
	}
	FlushToSocket(t);
	return BEV_OK;
}

void ReadTrampoline(bufferevent* bev, void* ctx) {
	auto* t = static_cast<Ctx*>(ctx);
	t->user_read(bev, t->user_arg);
}

void EventTrampoline(bufferevent* bev, short what, void* ctx) {
	auto* t = static_cast<Ctx*>(ctx);
	if(what & BEV_EVENT_CONNECTED) {
		if(!t->done) {
			// Only the TCP connection is up: this is the moment to send the
			// ClientHello. The caller sees CONNECTED later, after the handshake.
			Pump(t, nullptr);
			return;
		}
	} else if(!t->done && !t->failed && (what & (BEV_EVENT_ERROR | BEV_EVENT_EOF | BEV_EVENT_TIMEOUT))) {
		if(what & BEV_EVENT_EOF)
			Log(t, "TLS: il server ha chiuso la connessione prima della fine dell'handshake (la porta parla TLS?)");
		else
			Log(t, std::string("TLS: connessione di rete non riuscita: ") + evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()));
	}
	t->user_event(bev, what, t->user_arg);
}

void FreeCtx(void* ctx) {
	auto* t = static_cast<Ctx*>(ctx);
	if(t->ssl)
		SSL_free(t->ssl);
	if(t->sctx)
		SSL_CTX_free(t->sctx);
	delete t;
}

} // namespace

bufferevent* NewClient(event_base* base, const sockaddr* addr, int addrlen, const Options& options,
					   bufferevent_data_cb readcb, bufferevent_event_cb eventcb, void* cbarg,
					   bufferevent** raw_socket) {
	std::unique_ptr<Ctx, void(*)(void*)> t(new Ctx, FreeCtx);
	t->hostname = options.hostname;
	t->log = options.log;
	t->user_read = readcb;
	t->user_event = eventcb;
	t->user_arg = cbarg;

	t->sctx = SSL_CTX_new(TLS_client_method());
	if(!t->sctx) {
		Log(t.get(), "TLS: impossibile creare il contesto OpenSSL: " + DrainErrors());
		return nullptr;
	}
	SSL_CTX_set_min_proto_version(t->sctx, TLS1_2_VERSION);
	SSL_CTX_set_verify(t->sctx, SSL_VERIFY_PEER, nullptr);
	if(!LoadTrust(t.get(), options))
		return nullptr;

	t->ssl = SSL_new(t->sctx);
	auto* rbio = BIO_new(BIO_s_mem());
	auto* wbio = BIO_new(BIO_s_mem());
	if(!t->ssl || !rbio || !wbio) {
		BIO_free(rbio);
		BIO_free(wbio);
		Log(t.get(), "TLS: impossibile preparare la sessione OpenSSL: " + DrainErrors());
		return nullptr;
	}
	// An empty memory BIO must read as "try again", not as end of stream.
	BIO_set_mem_eof_return(rbio, -1);
	SSL_set_bio(t->ssl, rbio, wbio); // the SSL owns both from here
	if(auto* ip = a2i_IPADDRESS(options.hostname.c_str())) {
		ASN1_OCTET_STRING_free(ip);
		X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(t->ssl), options.hostname.c_str()); // no SNI for a literal
	} else {
		SSL_set_tlsext_host_name(t->ssl, options.hostname.c_str());
		SSL_set_hostflags(t->ssl, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
		if(SSL_set1_host(t->ssl, options.hostname.c_str()) != 1) {
			Log(t.get(), "TLS: nome del server non valido: '" + options.hostname + "'");
			return nullptr;
		}
	}
	SSL_set_connect_state(t->ssl);

	auto* raw = bufferevent_socket_new(base, -1, BEV_OPT_CLOSE_ON_FREE | BEV_OPT_THREADSAFE);
	if(!raw)
		return nullptr;
	auto* filter = bufferevent_filter_new(raw, FilterIn, FilterOut, BEV_OPT_CLOSE_ON_FREE, FreeCtx, t.get());
	if(!filter) {
		bufferevent_free(raw);
		return nullptr;
	}
	auto* ctx = t.release(); // the filter owns it now (FreeCtx)
	ctx->filter = filter;
	ctx->raw = raw;
	bufferevent_setcb(filter, ReadTrampoline, nullptr, EventTrampoline, ctx);
	bufferevent_enable(filter, EV_READ);
	if(bufferevent_socket_connect(raw, addr, addrlen) < 0) {
		Log(ctx, "TLS: impossibile avviare la connessione di rete");
		bufferevent_free(filter);
		return nullptr;
	}
	*raw_socket = raw;
	return filter;
}

}
