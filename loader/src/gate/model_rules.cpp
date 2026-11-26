#include "model_rules.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <set>

namespace jvm::models {

static std::string lower(std::string s) {
	for (auto &c : s) c = (char)std::tolower((unsigned char)c);
	return s;
}

bool is_model_kind(const std::string &kind) { return lower(kind) == "model"; }

std::optional<std::string> safe_relpath(const std::string &in) {
	std::string p = in;
	for (auto &c : p) if (c == '\\') c = '/';
	while (p.rfind("./", 0) == 0) p = p.substr(2);
	if (p.empty()) return std::nullopt;
	if (p[0] == '/') return std::nullopt;
	if (p.size() >= 2 && p[1] == ':') return std::nullopt;
	if (p.find("://") != std::string::npos) return std::nullopt;
	std::string seg, out;
	for (size_t i = 0; i <= p.size(); i++) {
		if (i == p.size() || p[i] == '/') {
			if (seg == "..") return std::nullopt;
			if (!seg.empty() && seg != ".") out += (out.empty() ? "" : "/") + seg;
			seg.clear();
		} else seg += p[i];
	}
	if (out.empty()) return std::nullopt;
	return out;
}

static int hexv(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

bool parse_color(const std::string &s0, float out[4]) {
	std::string s = s0;
	if (!s.empty() && s[0] == '#') s = s.substr(1);
	for (char c : s) if (hexv(c) < 0) return false;
	float v[4] = { 1, 1, 1, 1 };
	if (s.size() == 3) {
		for (int i = 0; i < 3; i++) v[i] = hexv(s[i]) * 17 / 255.0f;
	} else if (s.size() == 6 || s.size() == 8) {
		for (size_t i = 0; i < s.size() / 2; i++) v[i] = (hexv(s[2 * i]) * 16 + hexv(s[2 * i + 1])) / 255.0f;
	} else return false;
	for (int i = 0; i < 4; i++) out[i] = v[i];
	return true;
}

ParseResult parse_manifest(const RawManifest &m) {
	ParseResult r;
	Spec s;
	if (!is_model_kind(m.kind)) { r.errors.push_back("kind is '" + m.kind + "', not 'model'"); return r; }
	auto id = jvm::skins::parse_id(m.id);
	if (!id) r.errors.push_back("invalid id '" + m.id + "' (need 4 hex chars)");
	else s.id = *id;
	s.name = m.name;
	for (auto &k : m.bad_types) r.warnings.push_back("'" + k + "' has the wrong type (ignored, default used)");
	if (m.model.empty()) r.errors.push_back("missing \"model\" (path to a .glb inside the pack)");
	else {
		auto p = safe_relpath(m.model);
		if (!p) r.errors.push_back("\"model\" path '" + m.model + "' must stay inside the pack folder (no '..', drive letters or res://)");
		else {
			std::string ext;
			size_t dot = p->find_last_of('.');
			size_t slash = p->find_last_of('/');
			if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) ext = lower(p->substr(dot + 1));
			if (ext != "glb")
				r.errors.push_back("\"model\" '" + m.model + "' must be a self-contained .glb (packs are data only: no .gltf/.tscn/.scn scenes or scripts)");
			s.model = *p;
			s.ext = ext;
		}
	}
	if (m.hide_base) s.hide_base = *m.hide_base;
	if (m.allow_scripts && *m.allow_scripts) r.warnings.push_back("\"allow_scripts\" ignored: packs are data only and never run scripts");
	if (m.flash) s.flash = *m.flash;
	if (m.color) {
		if (parse_color(*m.color, s.color)) { s.has_color = true; s.tint = Tint::None; }
		else r.warnings.push_back("\"color\" '" + *m.color + "' is not #RRGGBB (ignored)");
	}
	if (m.team_tint) {
		std::string t = lower(*m.team_tint);

		if (t == "jersey" || t == "team" || t == "skin")
			r.warnings.push_back("\"team_tint\" '" + *m.team_tint + "' ignored: team tint is disabled, the model keeps its own colours");
		else if (t != "none" && t != "off") r.warnings.push_back("\"team_tint\" '" + *m.team_tint + "' unknown (use none); ignored");
	}
	if (m.tint_param) {
		if (m.tint_param->empty()) r.warnings.push_back("\"tint_param\" empty; 'albedo' kept");
		else s.tint_param = *m.tint_param;
	}
	if (m.tint_strength) {
		double v = *m.tint_strength;
		if (!std::isfinite(v)) v = 1.0;
		s.tint_strength = std::clamp(v, 0.0, 1.0);
		if (s.tint_strength != v) r.warnings.push_back("\"tint_strength\" clamped to 0..1");
	}
	for (auto &[k, v] : m.tint_light) {
		if (k.empty() || !std::isfinite(v)) continue;
		s.tint_light[k] = std::clamp(v, 0.0, 1.0);
	}
	if (m.game_hands) {
		std::string h = lower(*m.game_hands);
		if (h == "keep") s.hands = Hands::Keep;
		else if (h == "hide") s.hands = Hands::Hide;
		else if (h == "tint") s.hands = Hands::Tint;
		else r.warnings.push_back("\"game_hands\" '" + *m.game_hands + "' unknown (use keep|hide|tint); 'keep' used");
	}
	if (m.apply_to) {
		std::string a = lower(*m.apply_to);
		if (a == "local" || a == "me") s.target = Target::Local;
		else if (a == "all" || a == "everyone") s.target = Target::All;
		else r.warnings.push_back("\"apply_to\" '" + *m.apply_to + "' unknown (use local|all); 'local' used");
	}
	if (m.max_rest_error) {
		double v = *m.max_rest_error;
		if (std::isfinite(v) && v >= 0) s.max_rest_error = v;
	}
	if (m.has_arms) {
		std::string mode = lower(m.arms_mode.value_or("follow_hands"));
		if (mode == "follow_hands" || mode == "follow") s.arms.follow = true;
		else if (mode != "model" && mode != "off" && mode != "none")
			r.warnings.push_back("\"arms\".mode '" + *m.arms_mode + "' unknown (use follow_hands|model); the model's own arms are used");
		if (m.arms_anchor_bone && !m.arms_anchor_bone->empty()) s.arms.anchor_bone = *m.arms_anchor_bone;
		auto len = [&](const std::optional<double> &v, double &out, const char *k) {
			if (!v) return;
			if (std::isfinite(*v) && *v > 0.01 && *v < 10) out = *v;
			else r.warnings.push_back(std::string("\"arms\".") + k + " must be 0.01..10 (default kept)");
		};
		len(m.arms_upper, s.arms.upper, "upper");
		len(m.arms_lower, s.arms.lower, "lower");
		auto vec3 = [&](const std::optional<std::vector<double>> &v, double out[3], const char *k, bool positive) {
			if (!v) return;
			bool ok = v->size() == 3;
			for (double x : *v) ok = ok && std::isfinite(x) && std::fabs(x) < 100 && (!positive || (x > 0.001 && x < 5));
			if (ok) for (int i = 0; i < 3; i++) out[i] = (*v)[i];
			else r.warnings.push_back(std::string("\"arms\".") + k + (positive ? " must be 3 radii 0.001..5" : " must be [x, y, z]") + " (default kept)");
		};
		vec3(m.arms_shoulder, s.arms.shoulder, "shoulder", false);
		vec3(m.arms_radius, s.arms.radius, "radius", true);
		vec3(m.arms_elbow_hint, s.arms.elbow_hint, "elbow_hint", false);
	}
	if (r.errors.empty()) r.spec = s;
	return r;
}

V3 solve_elbow(const V3 &s, const V3 &h, double a, double b, const V3 &hint) {

	V3 d{ h.x - s.x, h.y - s.y, h.z - s.z };
	double L = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
	if (L < 1e-9) return V3{ s.x + hint.x * a, s.y + hint.y * a, s.z + hint.z * a };
	V3 u{ d.x / L, d.y / L, d.z / L };
	auto straight = [&]() { double t = a / (a + b); return V3{ s.x + d.x * t, s.y + d.y * t, s.z + d.z * t }; };
	if (L >= a + b) return straight();
	double dot = hint.x * u.x + hint.y * u.y + hint.z * u.z;
	V3 p{ hint.x - u.x * dot, hint.y - u.y * dot, hint.z - u.z * dot };
	double pl = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
	if (pl < 1e-6) {
		V3 f = std::fabs(u.z) < 0.9 ? V3{ 0, 0, -1 } : V3{ 1, 0, 0 };
		double fd = f.x * u.x + f.y * u.y + f.z * u.z;
		p = V3{ f.x - u.x * fd, f.y - u.y * fd, f.z - u.z * fd };
		pl = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
	}
	p = V3{ p.x / pl, p.y / pl, p.z / pl };
	if (L < std::fabs(a - b)) L = std::fabs(a - b) + 1e-9;
	double x = (a * a - b * b + L * L) / (2 * L);
	double y = std::sqrt(std::max(0.0, a * a - x * x));
	return V3{ s.x + u.x * x + p.x * y, s.y + u.y * x + p.y * y, s.z + u.z * x + p.z * y };
}

void apply_user_settings(Spec &s, const UserSettings &u, std::vector<std::string> &warnings) {
	if (u.color) {
		float c[4];
		if (parse_color(*u.color, c)) { s.has_color = true; s.user_color = true; for (int i = 0; i < 4; i++) s.color[i] = c[i]; s.color[3] = 1.0f; }
		else warnings.push_back("model_settings.json: \"color\" '" + *u.color + "' is not #RRGGBB (model colour kept)");
	}
	if (u.game_hands) {
		std::string h = lower(*u.game_hands);
		if (h == "keep") s.hands = Hands::Keep;
		else if (h == "hide") s.hands = Hands::Hide;
		else if (h == "tint") s.hands = Hands::Tint;
		else warnings.push_back("model_settings.json: \"game_hands\" '" + *u.game_hands + "' unknown (use keep|hide|tint); pack value kept");
	}
	if (u.apply_to) {
		std::string a = lower(*u.apply_to);
		if (a == "local" || a == "me") s.target = Target::Local;
		else if (a == "all" || a == "everyone") s.target = Target::All;
		else warnings.push_back("model_settings.json: \"apply_to\" '" + *u.apply_to + "' unknown (use local|all); pack value kept");
	}
}

std::string format_color(const float c[4]) {
	char b[8];
	auto ch = [](float v) { return (int)std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f); };
	std::snprintf(b, sizeof b, "#%02X%02X%02X", ch(c[0]), ch(c[1]), ch(c[2]));
	return b;
}

EquipResult select_model(const std::optional<std::string> &model_field, const std::vector<std::string> &equipped,
		const std::map<SkinId, InstalledPack> &installed) {
	EquipResult r;
	for (auto &e : equipped) {
		auto id = jvm::skins::parse_id(e);
		if (!id) continue;
		auto it = installed.find(*id);
		if (it != installed.end() && it->second.is_model)
			r.warnings.push_back("equipped.json: " + jvm::skins::format_id(*id) + " is a model pack listed under \"equipped\" (skins); "
					"ignored for models - put it in \"model\": \"" + jvm::skins::format_id(*id) + "\" instead (\"equipped\" ids are sent to other players)");
	}
	if (!model_field || model_field->empty()) return r;
	auto id = jvm::skins::parse_id(*model_field);
	if (!id) { r.warnings.push_back("equipped.json: \"model\" '" + *model_field + "' is not a 4-hex id"); return r; }
	auto it = installed.find(*id);
	if (it == installed.end()) { r.warnings.push_back("equipped.json: model " + jvm::skins::format_id(*id) + " is not installed"); return r; }
	if (!it->second.is_model) { r.warnings.push_back("equipped.json: " + jvm::skins::format_id(*id) + " ('" + it->second.pack + "') is not a model pack (kind != model)"); return r; }
	if (!it->second.usable) { r.warnings.push_back("equipped.json: model " + jvm::skins::format_id(*id) + " ('" + it->second.pack + "') has an invalid skin.json"); return r; }
	r.model = *id;
	return r;
}

std::string bone_key(const std::string &name) {
	std::string k;
	for (char c : name) {
		if (c == '.' || c == '-' || c == ' ' || c == '_') k += '_';
		else k += (char)std::tolower((unsigned char)c);
	}
	return k;
}

BoneCheck check_bones(const std::vector<std::string> &needed, const std::vector<std::string> &base) {
	BoneCheck c;
	std::set<std::string> have(base.begin(), base.end()), seen;
	std::map<std::string, std::string> loose;
	for (auto &b : base) loose.emplace(bone_key(b), b);
	for (auto &n : needed) {
		if (c.resolved.count(n) || seen.count(n)) continue;
		if (have.count(n)) { c.resolved[n] = n; continue; }
		auto it = n.empty() ? loose.end() : loose.find(bone_key(n));
		if (it != loose.end()) { c.resolved[n] = it->second; continue; }
		seen.insert(n);
		c.missing.push_back(n.empty() ? std::string("<unnamed>") : n);
	}
	c.ok = c.missing.empty();
	return c;
}

std::string join(const std::vector<std::string> &v, const std::string &sep, size_t max_items) {
	std::string o;
	for (size_t i = 0; i < v.size() && i < max_items; i++) o += (i ? sep : "") + v[i];
	if (v.size() > max_items) o += sep + "... (+" + std::to_string(v.size() - max_items) + ")";
	return o;
}

bool bytes_have_script(const std::string &b) {
	static const char *needles[] = { "type=\"GDScript\"", "type=\"Script\"", "type=\"CSharpScript\"", "GDScript", "CSharpScript",
		".gd\"", ".gdc\"", ".cs\"" };
	for (const char *n : needles) if (b.find(n) != std::string::npos) return true;
	return false;
}

bool is_script_path(const std::string &dep0) {
	std::string d = lower(dep0);
	if (d.find("::script::") != std::string::npos || d.find("::gdscript::") != std::string::npos || d.find("::csharpscript::") != std::string::npos) return true;
	auto ends = [&](const char *e) { std::string s(e); return d.size() >= s.size() && d.compare(d.size() - s.size(), s.size(), s) == 0; };
	return ends(".gd") || ends(".gdc") || ends(".cs");
}

}
