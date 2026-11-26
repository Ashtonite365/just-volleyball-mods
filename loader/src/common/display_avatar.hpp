#pragma once

#include "api_client.hpp"
#include "jvm_common.hpp"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/sub_viewport.hpp>
#include <vector>

namespace jvm {

inline bool is_character_portrait_player(godot::Node *player) {
	if (!player || !script_is(player, "Player")) return false;
	auto *vp = godot::Object::cast_to<godot::SubViewport>(player->get_parent());
	if (!vp || !vp->is_using_own_world_3d() || !vp->has_transparent_background()) return false;
	return player->get_process_mode() == godot::Node::PROCESS_MODE_DISABLED;
}

inline bool display_looks_like_me(GameIO &io, godot::Node *player) {
	godot::Object *mine = nullptr;
	if (godot::Node *gc = io.node("player.look", player, "/root/GameConfig")) {
		godot::Variant c = io.get("player.look", gc, "character");
		if (c.get_type() == godot::Variant::OBJECT) mine = c.operator godot::Object *();
	}
	godot::Variant pv = io.get("player.look", player, "character");
	godot::Object *theirs = pv.get_type() == godot::Variant::OBJECT ? pv.operator godot::Object *() : nullptr;
	if (!mine || !theirs) return false;
	if (mine == theirs) return true;
	if (!mine->has_method("look_dict") || !theirs->has_method("look_dict")) return false;
	godot::Variant a = io.call("player.look", mine, "look_dict"), b = io.call("player.look", theirs, "look_dict");
	if (a.get_type() != godot::Variant::DICTIONARY || b.get_type() != godot::Variant::DICTIONARY) return false;
	godot::Dictionary da = godot::Dictionary(a).duplicate(), db = godot::Dictionary(b).duplicate();
	da.erase("h");
	db.erase("h");
	return da.recursive_equal(db, 4);
}

inline void collect_portrait_players(godot::Node *root, std::vector<godot::Node *> &out) {
	if (!root) return;
	for (int i = 0; i < root->get_child_count(); i++) {
		godot::Node *n = root->get_child(i);
		if (!godot::Object::cast_to<godot::SubViewport>(n)) continue;
		for (int j = 0; j < n->get_child_count(); j++) {
			godot::Node *c = n->get_child(j);
			if (is_character_portrait_player(c)) out.push_back(c);
		}
	}
}

}
