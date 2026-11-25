#include "goofy_layers.hpp"

#include <algorithm>
#include <cmath>

namespace jvm::goofy {

const std::vector<Param> &params() {
	static const std::vector<Param> t = {
#define GP(id, sec, tgt, prop, col, d0, d1, d2, d3, lo, hi, unit, apply) Param{ id, sec, tgt, prop, col, { d0, d1, d2, d3 }, lo, hi, unit, apply },
#include "goofy_params.inc"
#include "goofy_player_params.inc"
#undef GP
	};
	return t;
}

static int count_ball() {
	int n = 0;
#define GP(...) n++;
#include "goofy_params.inc"
#undef GP
	return n;
}
int ball_param_count() { static const int n = count_ball(); return n; }
bool is_player_library(const Param &p) {
	const auto &t = params();
	return &p >= t.data() + ball_param_count() && &p < t.data() + t.size();
}
bool is_player_param(const Param &p) { return std::string(p.id).rfind("player.", 0) == 0; }

bool is_visual(const Param &p) { return p.is_color || std::string(p.section) == "visuals"; }

bool sanitize_time_scale(double in, double &out) {
	if (!std::isfinite(in)) return false;
	out = clampd(in, kTimeScaleMin, kTimeScaleMax);
	return true;
}

bool sanitize_host_value(const Param &p, const double *in, int n, Value &out) {
	out = Value();
	if (is_visual(p)) return false;
	for (int i = 0; i < n; i++) if (!std::isfinite(in[i])) return false;
	if (p.is_color) {
		if (n < 3 || n > 4) return false;
		for (int i = 0; i < 4; i++) out.v[i] = clampd(i < n ? in[i] : 1.0, 0.0, 1.0);
		return true;
	}
	if (n != 1) return false;
	double v = clampd(in[0], p.min, p.max);
	if (std::string(p.apply) == "live_int") v = std::round(v);
	out.v[0] = v;
	return true;
}

const Param *find(const std::string &id) {
	static std::map<std::string, const Param *> idx;
	if (idx.empty()) for (auto &p : params()) idx[p.id] = &p;
	auto it = idx.find(id);
	return it == idx.end() ? nullptr : it->second;
}

std::string canonical(const std::string &key, bool *as_mul) {
	if (as_mul) *as_mul = false;
	if (find(key)) return key;
	if (key == "gravity_scale") { if (as_mul) *as_mul = true; return "ball.gravity"; }
	if (key == "bounce_scale") { if (as_mul) *as_mul = true; return "ball.floor_bounce"; }
	if (key == "big_ball") { if (as_mul) *as_mul = true; return "ball.radius"; }
	if (key == "player_scale") return "player.body_scale";
	if (find("ball." + key)) return "ball." + key;
	return "";
}

double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

void Stack::set(Layer l) {
	clear(l.owner);
	if (l.values.empty()) return;
	l.seq = ++seq_;
	if (l.scope == Scope::Stage && l.stage.empty()) l.stage = loader_map_;
	layers_.push_back(std::move(l));
	std::stable_sort(layers_.begin(), layers_.end(), [](const Layer &a, const Layer &b) {
		return a.priority != b.priority ? a.priority < b.priority : a.seq < b.seq;
	});
}

bool Stack::clear(const std::string &owner) {
	auto n = layers_.size();
	layers_.erase(std::remove_if(layers_.begin(), layers_.end(), [&](const Layer &l) { return l.owner == owner; }), layers_.end());
	return n != layers_.size();
}

std::vector<std::string> Stack::set_loader_map(const std::string &id) {
	std::vector<std::string> dropped;
	if (id == loader_map_) return dropped;
	loader_map_ = id;
	for (auto it = layers_.begin(); it != layers_.end();) {
		if (it->scope == Scope::Stage && it->stage != id) { dropped.push_back(it->owner); it = layers_.erase(it); }
		else ++it;
	}
	return dropped;
}

bool Stack::resolve(const Param &p, const double stock[4], double out[4], std::string *owner) const {
	for (int i = 0; i < 4; i++) out[i] = stock[i];
	if (host_mode_ && !is_visual(p)) {
		auto h = host_.find(p.id);
		if (owner) *owner = "host";
		if (h == host_.end()) return false;
		for (int i = 0; i < 4; i++) out[i] = p.is_color ? h->second.v[i] : (i == 0 ? h->second.v[0] : stock[i]);
		for (int i = 0; i < 4; i++) if (out[i] != stock[i]) return true;
		return false;
	}
	if (force_defaults() && !is_player_library(p)) { if (owner) *owner = "fixed"; return false; }
	for (auto it = layers_.rbegin(); it != layers_.rend(); ++it) {
		auto f = it->values.find(p.id);
		if (f == it->values.end()) continue;
		const Value &v = f->second;
		if (p.is_color) for (int i = 0; i < 4; i++) out[i] = clampd(v.v[i], 0.0, 1.0);
		else out[0] = clampd(v.mul ? stock[0] * v.v[0] : v.v[0], p.min, p.max);
		if (owner) *owner = it->owner;
		for (int i = 0; i < 4; i++) if (out[i] != stock[i]) return true;
		return false;
	}
	return false;
}

}
