


#pragma once
#include "jvm_common.hpp"

#include <godot_cpp/classes/node.hpp>

namespace jvm {

template <class Derived>
struct GateFollower {
	bool applied = false;
	bool connected = false;
	bool cosmetic = false;
	godot::String last_reason = "gate not found (core not loaded)";

	void sync(godot::Node *self) {
		godot::Node *gate = find_gate(self);
		if (gate && !connected) {
			gate->connect(cosmetic ? "cosmetic_gate_changed" : "gate_changed", godot::Callable(self, "_on_gate_changed"));
			connected = true;
		}
		bool want = false;
		if (gate) {
			want = bool(gate->call(cosmetic ? "is_cosmetic_active" : "is_active"));
			last_reason = gate->call(cosmetic ? "get_cosmetic_reason" : "get_reason");
		} else {
			last_reason = "gate not found (core not loaded) - fail closed";
		}
		Derived *d = static_cast<Derived *>(self);
		if (want && !applied) { applied = true; d->apply(); }
		else if (!want && applied) { applied = false; d->revert(); }
	}
	godot::Dictionary status() const {
		godot::Dictionary s;
		s["state"] = applied ? "active" : "blocked";
		s["reason"] = last_reason;
		s["tier"] = cosmetic ? "cosmetic" : "full";
		return s;
	}
};

}
