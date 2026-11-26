#pragma once
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/variant/array.hpp>
#include <vector>

namespace godot {

class JVModelLoader;









class JVModelTab : public Node {
	GDCLASS(JVModelTab, Node)

	ObjectID locker_;
	ObjectID tile_;
	int slim_ = 0;
	uint64_t skipped_ = 0;
	Ref<Script> ui_style_;
	std::vector<String> ids_;
	Array swatches_;
	bool swatch0_default_ = false;
	bool rebuild_queued_ = false;

	JVModelLoader *loader() const;
	static void react_to_pick_now(int64_t locker_id);
	Node *find_locker() const;
	bool try_inject(Node *locker);
	void build(Node *locker);
	void skip(Node *locker, const String &why);
	void fit_rail();
	Node *locker() const;

protected:
	static void _bind_methods();

public:
	static constexpr const char *kTab = "jvm_full_model";
	void _ready() override;
	void _process(double delta) override;
	void _on_tile();
	void _on_locker_exit();
	void _rebuild();
	void _on_pick(int idx);
	void _on_apply();
	void _on_swatch(int idx);
	void _on_color(const Color &c);
	void _on_reset();
	void _on_hands(int idx);
	void _on_target(int idx);
	Dictionary get_info() const;
};

}
