






#pragma once
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <chrono>
#include <cstdint>

namespace jvm {
namespace prof {

extern bool g_on;

inline uint64_t now_ns() {
	return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
int stage(const char *name);
void record(int id, uint64_t t0_ns, uint64_t t1_ns, int depth);
extern int g_depth;
bool on_main_thread();

struct Scope {
	int id;
	uint64_t t0 = 0;
	int depth = 0;
	explicit Scope(int i) : id(i) {
		if (g_on && on_main_thread()) { depth = g_depth++; t0 = now_ns(); }
	}
	~Scope() {
		if (t0) { g_depth = depth; if (g_on) record(id, t0, now_ns(), depth); }
	}
	Scope(const Scope &) = delete;
	Scope &operator=(const Scope &) = delete;
};



godot::Variant tap(const godot::String &library, const godot::String &cmd, const godot::Variant &arg);

}
}

#define JVM_PROF_CAT2(a, b) a##b
#define JVM_PROF_CAT(a, b) JVM_PROF_CAT2(a, b)

#define JVM_PROF(name)                                                                    \
	static const int JVM_PROF_CAT(jvp_id_, __LINE__) = ::jvm::prof::stage(name);          \
	::jvm::prof::Scope JVM_PROF_CAT(jvp_scope_, __LINE__)(JVM_PROF_CAT(jvp_id_, __LINE__))


#define JVM_PROF_TAP_DECL(lib) \
	godot::Variant _jv_dev_tap(const godot::String &cmd, const godot::Variant &arg) { return ::jvm::prof::tap(lib, cmd, arg); }
#define JVM_PROF_TAP_BIND(Class) \
	godot::ClassDB::bind_method(godot::D_METHOD("_jv_dev_tap", "cmd", "arg"), &Class::_jv_dev_tap)
