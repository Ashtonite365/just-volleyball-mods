




#pragma once

#include "native_audio_core.hpp"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <unordered_map>
#include <vector>

namespace godot {
class AudioStream;
class AudioStreamPlayer;
class AudioStreamPlayer3D;
}

namespace jvm::native_audio {

struct PlayRequest {
	godot::String id;
	bool loop = false;
	float volume = 1.0f;
	godot::String bus;
	bool has_position = false;
	godot::Vector3 position;
};

PlayRequest play_request_from_dict(const godot::Dictionary &opts, const godot::String &id, bool loop);


Session &session();



godot::Array list_native();
godot::PackedStringArray list_buses();
godot::Dictionary status_dict();
godot::Dictionary info_dict(const godot::String &id);

int play_native(const PlayRequest &req);
int play_one_shot(const godot::String &id, const godot::Dictionary &opts);
int play_loop(const godot::String &id, const godot::Dictionary &opts);
bool stop_handle(int handle);
int stop_named(const godot::String &id);
void stop_everything();
bool set_voice_volume(int handle, float volume);
bool set_voice_bus(int handle, const godot::String &bus);

PackAudio pack_from_dict(const godot::Dictionary &audio);
int apply_pack_audio(const PackAudio &pack);
void clear_pack_audio();


void apply_pack_audio_godot(const godot::Dictionary &audio);
void clear_pack_audio_godot();

}


namespace godot {
class JVMNativeAudio : public Node {
	GDCLASS(JVMNativeAudio, Node)
protected:
	static void _bind_methods();
public:
	void on_finished(int handle);
};
}
