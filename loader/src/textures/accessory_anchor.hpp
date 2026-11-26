#pragma once











#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/skeleton3d.hpp>
#include <godot_cpp/variant/dictionary.hpp>

namespace jvm_anchor {



struct Tuning {
	float hat_scale = 0.90f;
	float hat_drop_m = 0.05f;
	float stock_drop_m = 0.06f;
	float tail_drop_m = 0.05f;
	float dup_head_step_m = 0.06f;
	float dup_back_step_m = 0.07f;
};
const Tuning &tuning();

enum Kind { kHead = 0, kBack = 1 };




struct Override {
	godot::String bone;
	bool has_offset = false;
	godot::Vector3 offset;
	bool has_scale = false;
	float scale = 1.0f;
};
struct Overrides { Override head, back; godot::String key; };
Overrides parse_overrides(const godot::Variant &anchors_dict);




int sync_player(godot::Node *player, const Overrides &ov);
int sync_rig(godot::Node *rig, const Overrides &ov);





godot::Node3D *seat_for_spawn(godot::Skeleton3D *skel, Kind kind, int32_t mount_bone, const godot::String &slot,
		const godot::String &acc_id, bool authored, const godot::Vector3 &place_origin, const Overrides &ov);


Kind kind_of_mount(const godot::String &mount);


godot::Dictionary describe(godot::Node *player);

}
