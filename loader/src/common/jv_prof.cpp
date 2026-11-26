#include "jv_prof.hpp"

#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_int64_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <string>
#include <thread>
#include <vector>

using namespace godot;

namespace jvm {
namespace prof {

bool g_on = false;
int g_depth = 0;

namespace {
struct Sample { int32_t id; int32_t depth; uint64_t t0, t1; };
std::vector<std::string> &names() { static std::vector<std::string> n; return n; }
std::vector<Sample> g_buf;
size_t g_cap = 0;
uint64_t g_dropped = 0;
std::thread::id g_main;
bool g_main_set = false;
}

bool on_main_thread() { return g_main_set && std::this_thread::get_id() == g_main; }

int stage(const char *name) {
	auto &n = names();
	for (size_t i = 0; i < n.size(); i++) if (n[i] == name) return int(i);
	n.emplace_back(name);
	return int(n.size() - 1);
}

void record(int id, uint64_t t0, uint64_t t1, int depth) {
	if (g_buf.size() >= g_cap) { g_dropped++; return; }
	g_buf.push_back({ int32_t(id), int32_t(depth), t0, t1 });
}

Variant tap(const String &library, const String &cmd, const Variant &arg) {
	if (cmd == "begin") {

		g_main = std::this_thread::get_id();
		g_main_set = true;
		int64_t cap = arg.get_type() == Variant::INT ? int64_t(arg) : 262144;
		if (cap < 1024) cap = 1024;
		if (cap > 4194304) cap = 4194304;
		g_buf.clear();
		g_buf.reserve(size_t(cap));
		g_cap = size_t(cap);
		g_dropped = 0;
		g_depth = 0;
		g_on = true;
		return true;
	}
	if (cmd == "end") { g_on = false; return true; }
	if (cmd == "collect") {
		Dictionary d;
		PackedStringArray nm;
		for (auto &s : names()) nm.push_back(String::utf8(s.c_str()));
		PackedInt32Array st, dp;
		PackedInt64Array a, b;
		st.resize(g_buf.size()); dp.resize(g_buf.size()); a.resize(g_buf.size()); b.resize(g_buf.size());
		for (size_t i = 0; i < g_buf.size(); i++) {
			st.set(i, g_buf[i].id); dp.set(i, g_buf[i].depth);
			a.set(i, int64_t(g_buf[i].t0)); b.set(i, int64_t(g_buf[i].t1));
		}
		d["library"] = library;
		d["names"] = nm;
		d["stage"] = st;
		d["depth"] = dp;
		d["t0_ns"] = a;
		d["t1_ns"] = b;
		d["dropped"] = int64_t(g_dropped);
		d["recorded"] = int64_t(g_buf.size());

		std::vector<Sample>().swap(g_buf);
		g_cap = 0;
		return d;
	}
	if (cmd == "now") return int64_t(now_ns());
	if (cmd == "info") {
		Dictionary d;
		d["library"] = library;
		d["on"] = g_on;
		d["stages"] = int64_t(names().size());
		d["buffered"] = int64_t(g_buf.size());
		return d;
	}
	return Variant();
}

}
}
