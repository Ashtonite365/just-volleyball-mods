




#pragma once
#include "skin_sync.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace jvm::models {

using jvm::skins::SkinId;

enum class Tint { Jersey, Skin, None };
enum class Hands { Keep, Hide, Tint };
enum class Target { Local, All };


struct RawManifest {
	std::string kind, id, name, model;
	std::optional<std::string> team_tint, tint_param, game_hands, apply_to, color;
	std::optional<bool> hide_base, allow_scripts, flash;
	std::optional<double> tint_strength, max_rest_error;
	std::map<std::string, double> tint_light;
	std::vector<std::string> bad_types;

	bool has_arms = false;
	std::optional<std::string> arms_mode, arms_anchor_bone;
	std::optional<double> arms_upper, arms_lower;
	std::optional<std::vector<double>> arms_shoulder, arms_radius, arms_elbow_hint;
};





struct Arms {
	bool follow = false;
	std::string anchor_bone = "spine1";
	double shoulder[3] = { 0.50, 2.42, 0.0 };
	double upper = 0.45, lower = 0.72;
	double radius[3] = { 0.15, 0.16, 0.155 };
	double elbow_hint[3] = { 1.0, -0.5, -0.6 };
};

struct Spec {
	SkinId id = 0;
	std::string name;
	std::string model;
	std::string ext;
	bool hide_base = true;
	Hands hands = Hands::Keep;
	Tint tint = Tint::None;
	std::string tint_param = "albedo";
	double tint_strength = 1.0;
	std::map<std::string, double> tint_light;
	bool has_color = false;
	bool user_color = false;
	float color[4] = { 1, 1, 1, 1 };
	Target target = Target::Local;
	bool allow_scripts = false;
	bool flash = true;
	double max_rest_error = 0.05;
	Arms arms;
};



struct V3 { double x = 0, y = 0, z = 0; };
V3 solve_elbow(const V3 &s, const V3 &h, double a, double b, const V3 &hint);

struct ParseResult {
	std::optional<Spec> spec;
	std::vector<std::string> errors;
	std::vector<std::string> warnings;
};

bool is_model_kind(const std::string &kind);
ParseResult parse_manifest(const RawManifest &m);




struct UserSettings { std::optional<std::string> color, game_hands, apply_to; };
void apply_user_settings(Spec &s, const UserSettings &u, std::vector<std::string> &warnings);
std::string format_color(const float c[4]);



std::optional<std::string> safe_relpath(const std::string &p);
bool parse_color(const std::string &s, float out[4]);




struct InstalledPack { bool is_model = false; bool usable = false; std::string pack; };
struct EquipResult {
	std::optional<SkinId> model;
	std::vector<std::string> warnings;
};
EquipResult select_model(const std::optional<std::string> &model_field, const std::vector<std::string> &equipped,
		const std::map<SkinId, InstalledPack> &installed);




struct BoneCheck { bool ok = true; std::vector<std::string> missing; std::map<std::string, std::string> resolved;  };
std::string bone_key(const std::string &name);
BoneCheck check_bones(const std::vector<std::string> &needed, const std::vector<std::string> &base);
std::string join(const std::vector<std::string> &v, const std::string &sep, size_t max_items = 8);


bool bytes_have_script(const std::string &bytes);
bool is_script_path(const std::string &dep);

}
