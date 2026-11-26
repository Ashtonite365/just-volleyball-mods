


#pragma once
#include "jvm_common.hpp"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include <utility>
#include <vector>

namespace jvm {


inline godot::Object *find_api(godot::Node *from) {
	godot::Engine *e = godot::Engine::get_singleton();
	static const godot::StringName kSingleton("JVModAPI");
	if (e->has_singleton(kSingleton)) return e->get_singleton(kSingleton);
	if (!from || !from->is_inside_tree()) return nullptr;
	godot::Node *n = from->get_tree()->get_root()->get_node_or_null(godot::NodePath("JVModGate/JVModAPI"));
	return n && n->has_method("register_mod") ? n : nullptr;
}




template <class Derived>
struct ApiFollower {
	const char *mod = "";
	const char *access = "gameplay";
	const char *api_version = "1.0";
	godot::PackedStringArray caps;
	bool applied = false;
	bool connected = false;
	bool registered = false;
	godot::String last_reason = "mod API not found (core not loaded)";

	godot::Object *api(godot::Node *self) const { return find_api(self); }
	void sync(godot::Node *self) {
		godot::Object *a = find_api(self);
		if (a && !registered) {
			godot::Dictionary r = a->call("register_mod", mod, api_version, caps);
			registered = bool(r.get("ok", false));
			if (!registered) last_reason = "API refused registration: " + godot::String(r.get("reason", "?"));
		}
		if (a && registered && !connected) {
			a->connect("access_changed", godot::Callable(self, "_on_access_changed"));
			connected = true;
		}
		bool want = false;
		if (a && registered) {
			static const godot::StringName kIsOpen("is_open"), kGetReason("get_reason");
			want = bool(a->call(kIsOpen, access));
			last_reason = a->call(kGetReason, access);
		} else if (!a) {
			last_reason = "mod API not found (core not loaded) - fail closed";
		}
		Derived *d = static_cast<Derived *>(self);
		if (want && !applied) { applied = true; d->apply(); }
		else if (!want && applied) { applied = false; d->revert(); }
	}
	godot::Dictionary status() const {
		godot::Dictionary s;
		s["state"] = applied ? "active" : "blocked";
		s["reason"] = last_reason;
		s["tier"] = godot::String(access) == "cosmetic" ? "cosmetic" : "full";
		s["api"] = registered ? godot::String("registered (") + api_version + ")" : godot::String("not registered");
		return s;
	}
};







struct GameIO {
	godot::Node *self = nullptr;
	const char *mod = "";
	const char *api_version = "1.1";
	godot::PackedStringArray caps;
	mutable godot::Object *api_ = nullptr;
	mutable uint64_t api_id_ = 0;
	mutable bool registered_ = false;



	bool writes_need_open = true;



	struct Names {
		godot::StringName io_get{ "io_get" }, io_set{ "io_set" }, io_call{ "io_call" }, io_connect{ "io_connect" },
			io_disconnect{ "io_disconnect" }, io_node{ "io_node" }, io_autoload{ "io_autoload" }, io_resource{ "io_resource" },
			io_singleton{ "io_singleton" };
	};
	static const Names &names() { static const Names n; return n; }
	mutable godot::String mod_s_;
	mutable const char *mod_p_ = nullptr;
	mutable std::vector<std::pair<const char *, godot::String>> caps_s_;
	const godot::String &M() const {
		if (mod_p_ != mod) { mod_p_ = mod; mod_s_ = godot::String(mod); }
		return mod_s_;
	}
	const godot::String &C(const char *cap) const {
		for (auto &kv : caps_s_) if (kv.first == cap) return kv.second;
		caps_s_.emplace_back(cap, godot::String(cap));
		return caps_s_.back().second;
	}

	godot::Object *api() const {
		if (api_ && godot::ObjectDB::get_instance(api_id_) == api_) return api_;
		api_ = find_api(self);
		api_id_ = api_ ? uint64_t(api_->get_instance_id()) : 0;
		registered_ = false;
		if (api_) {
			godot::Dictionary r = api_->call("register_mod", mod, api_version, caps);
			registered_ = bool(r.get("ok", false));
		}
		return api_;
	}
	godot::Variant get(const char *cap, godot::Object *o, const godot::StringName &prop) const {
		godot::Object *a = o ? api() : nullptr;
		return a ? a->call(names().io_get, M(), C(cap), o, prop) : godot::Variant();
	}
	bool set(const char *cap, godot::Object *o, const godot::StringName &prop, const godot::Variant &v, bool undo = false) const {
		godot::Object *a = o ? api() : nullptr;
		return a && bool(a->call(names().io_set, M(), C(cap), o, prop, v, undo || !writes_need_open));
	}
	bool set_undo(const char *cap, godot::Object *o, const godot::StringName &prop, const godot::Variant &v) const { return set(cap, o, prop, v, true); }
	template <typename... T>
	godot::Variant call(const char *cap, godot::Object *o, const godot::StringName &method, const T &...args) const {
		godot::Object *a = o ? api() : nullptr;
		if (!a) return godot::Variant();
		godot::Array arr;
		(arr.push_back(godot::Variant(args)), ...);
		return a->call(names().io_call, M(), C(cap), o, method, arr, !writes_need_open);
	}
	template <typename... T>
	godot::Variant call_undo(const char *cap, godot::Object *o, const godot::StringName &method, const T &...args) const {
		godot::Object *a = o ? api() : nullptr;
		if (!a) return godot::Variant();
		godot::Array arr;
		(arr.push_back(godot::Variant(args)), ...);
		return a->call(names().io_call, M(), C(cap), o, method, arr, true);
	}
	godot::Error connect(const char *cap, godot::Object *o, const godot::StringName &sig, const godot::Callable &c, uint32_t flags = 0) const {
		godot::Object *a = o ? api() : nullptr;
		return a ? godot::Error(int64_t(a->call(names().io_connect, M(), C(cap), o, sig, c, int64_t(flags)))) : godot::ERR_UNAVAILABLE;
	}
	void disconnect(const char *cap, godot::Object *o, const godot::StringName &sig, const godot::Callable &c) const {
		if (godot::Object *a = o ? api() : nullptr) a->call(names().io_disconnect, M(), C(cap), o, sig, c);
	}
	godot::Node *node(const char *cap, godot::Node *from, const godot::String &path) const {
		godot::Object *a = from ? api() : nullptr;
		return a ? godot::Object::cast_to<godot::Node>(a->call(names().io_node, M(), C(cap), from, path).operator godot::Object *()) : nullptr;
	}

	template <class R> godot::Variant get(const char *cap, const godot::Ref<R> &o, const godot::StringName &p) const { return get(cap, o.ptr(), p); }
	template <class R> bool set(const char *cap, const godot::Ref<R> &o, const godot::StringName &p, const godot::Variant &v, bool undo = false) const { return set(cap, o.ptr(), p, v, undo); }
	template <class R> bool set_undo(const char *cap, const godot::Ref<R> &o, const godot::StringName &p, const godot::Variant &v) const { return set(cap, o.ptr(), p, v, true); }
	template <class R, typename... T> godot::Variant call(const char *cap, const godot::Ref<R> &o, const godot::StringName &m, const T &...args) const { return call(cap, o.ptr(), m, args...); }
	template <class R> godot::Error connect(const char *cap, const godot::Ref<R> &o, const godot::StringName &sig, const godot::Callable &c, uint32_t flags = 0) const { return connect(cap, o.ptr(), sig, c, flags); }
	template <class R> void disconnect(const char *cap, const godot::Ref<R> &o, const godot::StringName &sig, const godot::Callable &c) const { disconnect(cap, o.ptr(), sig, c); }
	godot::Node *node(const char *cap, godot::Node *from, const godot::NodePath &path) const { return node(cap, from, godot::String(path)); }
	godot::Node *node(const char *cap, godot::Node *from, const char *path) const { return node(cap, from, godot::String(path)); }
	godot::Node *autoload(const char *cap, const godot::String &name) const {
		godot::Object *a = api();
		return a ? godot::Object::cast_to<godot::Node>(a->call(names().io_autoload, M(), C(cap), name).operator godot::Object *()) : nullptr;
	}

	godot::Ref<godot::Resource> resource(const char *cap, const godot::String &path) const {
		godot::Object *a = api();
		if (!a) return godot::Ref<godot::Resource>();
		return godot::Ref<godot::Resource>(godot::Object::cast_to<godot::Resource>(a->call(names().io_resource, M(), C(cap), path).operator godot::Object *()));
	}
	godot::Object *singleton(const char *cap, const godot::String &name) const {
		godot::Object *a = api();
		return a ? a->call(names().io_singleton, M(), C(cap), name).operator godot::Object *() : nullptr;
	}

	template <typename... T>
	godot::Variant core(const char *fn, const T &...args) const {
		godot::Object *a = api();
		if (!a) return godot::Variant();
		return a->call(fn, mod, args...);
	}
};

}
