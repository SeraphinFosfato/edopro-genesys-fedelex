// Finding 52 (audit 2026-10-11): BufferIO's per-thread read limit. What is
// proven: inside a ReadLimit, reads and skips past the end throw Overrun;
// nested guards restore the outer limit; without a guard nothing changes.
// What is NOT: that ClientAnalyze catches it (that needs mainGame).

#include <cstdint>
#include <cstdio>
#include "bufferio.h"

int RunBufferIOTests();

namespace {
int failures = 0;
int checks = 0;
void check(bool condition, const char* what) {
	++checks;
	if(!condition) {
		++failures;
		std::printf("FAIL: %s\n", what);
	}
}
template<typename F>
bool throws_overrun(F&& f) {
	try { f(); } catch(const BufferIO::Overrun&) { return true; }
	return false;
}
}

int RunBufferIOTests() {
	const uint8_t buf[8] = { 1, 0, 0, 0, 2, 0, 0, 0 };
	{ // within the limit
		BufferIO::ReadLimit guard(buf, 8);
		const uint8_t* p = buf;
		check(BufferIO::Read<uint32_t>(p) == 1u && BufferIO::Read<uint32_t>(p) == 2u && p == buf + 8, "reading exactly the whole buffer is fine");
		check(BufferIO::Remaining(p) == 0, "nothing left at the end");
		check(throws_overrun([&] { BufferIO::Read<uint8_t>(p); }), "one byte past the end throws");
		check(p == buf + 8, "a failed read does not move the pointer");
	}
	{ // oversized read / skip, including sizes that would overflow a pointer
		BufferIO::ReadLimit guard(buf, 8);
		const uint8_t* p = buf;
		check(throws_overrun([&] { BufferIO::Skip(p, 9); }), "skip past the end throws");
		check(throws_overrun([&] { BufferIO::Skip(p, SIZE_MAX); }), "skip of SIZE_MAX throws (no pointer overflow)");
		BufferIO::Skip(p, 8);
		check(p == buf + 8, "skip to exactly the end is fine");
		check(throws_overrun([&] { BufferIO::Skip(p, 1); }), "skip at the end throws");
	}
	{ // nesting restores the outer limit
		BufferIO::ReadLimit outer(buf, 8);
		{
			BufferIO::ReadLimit inner(buf, 2);
			const uint8_t* p = buf;
			check(throws_overrun([&] { BufferIO::Read<uint32_t>(p); }), "inner limit of 2 refuses a 4-byte read");
		}
		const uint8_t* p = buf;
		check(BufferIO::Read<uint32_t>(p) == 1u, "outer limit is back after the inner guard dies");
		check(throws_overrun([&] { BufferIO::Skip(p, 5); }), "outer limit still enforced");
	}
	{ // no guard: unchanged behaviour
		const uint8_t* p = buf;
		check(BufferIO::read_end == nullptr, "no limit outside a guard");
		BufferIO::Skip(p, 8);
		check(p == buf + 8 && BufferIO::Remaining(p) == SIZE_MAX, "without a guard nothing is checked");
	}
	std::printf("bufferio_tests: %d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
