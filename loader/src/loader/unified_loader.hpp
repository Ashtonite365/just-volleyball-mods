#pragma once










#include "bridge_logic.hpp"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <string>

namespace godot {

class JVLoader : public Node {
	GDCLASS(JVLoader, Node)

	ObjectID textures_, models_;
	bool spawned_ = false;
	double tick_t_ = 1e9;

	String active_map_;
	String active_reason_ = "not started";

	Dictionary block_cache_;

	jvm::loader::BridgeState bs_;
	ObjectID goofy_id_;
	String goofy_version_;
	bool goofy_enabled_ = false;
	String bridge_why_ = "not started";
	Dictionary last_result_;
	int applies_ = 0, clears_ = 0, map_calls_ = 0;

	Node *texture_node() const;
	Node *model_node() const;
	Dictionary goofy_info() const;
	void spawn_subsystems();
	void update_stage();
	void update_bridge();
	void clear_now();

protected:
	static void _bind_methods();

public:
	JVLoader();
	void _ready() override;
	void _process(double delta) override;
	void _exit_tree() override;

	static constexpr const char *kVersion = "1.0";
	Dictionary get_status() const;
	Dictionary get_bridge_status() const;

	bool is_loader_map(const String &stage_id) const;
	String get_active_map() const { return active_map_; }
	Dictionary get_map_goofy_block(const String &stage_id);
	void refresh();
	bool reload_config();
};

}
