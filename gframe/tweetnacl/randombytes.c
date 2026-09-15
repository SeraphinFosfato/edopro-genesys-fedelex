/* randombytes.c — the one symbol TweetNaCl needs that isn't in tweetnacl.c.
 *
 * tweetnacl.c declares `extern void randombytes(u8 *, u64);` and two
 * functions call it: crypto_box_keypair and crypto_sign_keypair. This client
 * never calls either — it only verifies (banlist_verify.cpp calls
 * crypto_sign_open) and never signs or generates a keypair — but
 * tweetnacl.c is linked as a whole translation unit, so the reference has
 * to resolve at link time regardless of whether it is ever reached at
 * runtime. This file exists to satisfy the linker, not to be exercised.
 *
 * Still implemented for real, not stubbed to zeros: "never called in this
 * client" is not a promise a modified or future build has to keep, and a
 * randombytes() that silently handed out zero bytes would be the kind of
 * fake security this project's CLAUDE.md explicitly rules out.
 */
#include "tweetnacl.h"

#if defined(_WIN32)

#include <windows.h>
#include <bcrypt.h>

void randombytes(unsigned char *buf, unsigned long long len) {
	BCryptGenRandom(NULL, buf, (ULONG)len, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
}

#else

#include <stddef.h>
#include <stdio.h>

void randombytes(unsigned char *buf, unsigned long long len) {
	FILE *f = fopen("/dev/urandom", "rb");
	if(!f)
		return;
	unsigned char *p = buf;
	size_t remaining = (size_t)len;
	while(remaining > 0) {
		size_t got = fread(p, 1, remaining, f);
		if(got == 0)
			break;
		p += got;
		remaining -= got;
	}
	fclose(f);
}

#endif
