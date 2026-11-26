




#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace jvm::native_audio {

inline constexpr const char *kBeachPath =
	"res://game/sfx/Ambiance/717146__ghosttropic__short-sand-beach-waves-3.wav";
inline constexpr const char *kBeachId =
	"ambiance/717146__ghosttropic__short-sand-beach-waves-3";
inline constexpr const char *kBeachAlias = "beach";

inline constexpr const char *kParkPath =
	"res://game/sfx/Ambiance/729328__tradeachest__urban-park-ambiance-loop.wav";
inline constexpr const char *kParkId =
	"ambiance/729328__tradeachest__urban-park-ambiance-loop";
inline constexpr const char *kParkAlias = "park";

inline constexpr const char *kAudiencePath =
	"res://game/sfx/Ambiance/freesound_community-audience-77867.mp3";


inline constexpr const char *kScanRoots[] = { "res://game/sfx", "res://game/ost" };
inline constexpr int kScanRootCount = 2;

struct Entry {
	std::string id;
	std::string path;
	std::string category;
	std::string default_bus;
};

struct PackAudio {
	bool present = false;
	std::string ambience;
	std::string bus;
	float volume = 1.0f;
};

struct PlayOpts {
	bool loop = false;
	float volume = 1.0f;
	std::string bus;
	bool has_position = false;
	float x = 0, y = 0, z = 0;
};

struct Voice {
	int handle = 0;
	std::string id;
	std::string path;
	std::string bus;
	float volume = 1.0f;
	bool loop = false;
	bool spatial = false;
};

struct Stats {
	bool started = false;
	bool index_built = false;
	int index_count = 0;
	int alias_count = 0;
	int cached_streams = 0;
	int players = 0;
	int nodes = 0;
	int allocations = 0;
};


bool is_audio_filename(const std::string &name);
std::string strip_extension(const std::string &name);
std::string normalize_key(const std::string &s);
std::string short_id_from_path(const std::string &path);
std::string category_from_path(const std::string &path);
std::string default_bus_from_path(const std::string &path);


void add_known_aliases(std::unordered_map<std::string, std::string> &aliases,
	const std::vector<Entry> &index);






bool pack_audio_ref_ok(const std::string &ref);

bool native_audio_path_ok(const std::string &path);
PackAudio make_pack_audio(bool has_block, const std::string &ambience,
	const std::string &bus, float volume);



class Session {
public:
	Stats stats() const { return stats_; }
	int allocation_count() const { return stats_.allocations; }



	void build_index(const std::vector<std::string> &paths);
	const std::vector<Entry> &index() const { return index_; }
	const Entry *find(const std::string &id_or_path) const;
	std::string resolve_path(const std::string &id_or_path) const;




	int play(const std::string &id, const PlayOpts &opts);
	bool stop(int handle);
	int stop_id(const std::string &id);
	void stop_all();
	bool set_volume(int handle, float vol);
	bool set_bus(int handle, const std::string &bus);
	const Voice *voice(int handle) const;

	int apply_pack(const PackAudio &pack);
	void clear_pack();
	int pack_handle() const { return pack_handle_; }

private:
	void account(int n);
	void ensure_started();
	void maybe_idle();
	void drop_stream(const std::string &path);
	Voice *mutable_voice(int handle);

	Stats stats_;
	std::vector<Entry> index_;
	std::unordered_map<std::string, std::size_t> by_key_;
	std::unordered_map<std::string, int> stream_users_;
	std::unordered_map<int, Voice> voices_;
	int next_handle_ = 1;
	int pack_handle_ = 0;
};

}
