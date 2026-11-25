


#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace jvm::goofy {

enum Target { PHYS, FEEL, EXT };

struct Param {
	const char *id, *section;
	Target target;
	const char *prop;
	bool is_color;
	double def[4];
	double min, max;
	const char *unit, *apply;
};
const std::vector<Param> &params();
const Param *find(const std::string &id);
int ball_param_count();
bool is_player_library(const Param &p);
bool is_player_param(const Param &p);



bool is_visual(const Param &p);


struct Value {
	bool mul = false;
	double v[4] = { 0, 0, 0, 1 };
};

enum class Scope { Session, Stage };
constexpr int kUserPriority = 100, kPackPriority = 200, kModPriority = 500, kMaxModPriority = 899, kForcePriority = 1000;

struct Layer {
	std::string owner;
	int priority = kModPriority;
	uint64_t seq = 0;
	Scope scope = Scope::Session;
	std::string stage;
	std::map<std::string, Value> values;
};

struct ParseResult {
	std::vector<std::string> applied, ignored, errors;
};




std::string canonical(const std::string &key, bool *as_mul = nullptr);

class Stack {
	std::vector<Layer> layers_;
	uint64_t seq_ = 0;
	std::string loader_map_;
	bool fixed_ = false;
	bool host_mode_ = false;
	std::map<std::string, Value> host_;

public:

	void set(Layer l);
	bool clear(const std::string &owner);
	const std::vector<Layer> &layers() const { return layers_; }

	std::vector<std::string> set_loader_map(const std::string &id);
	const std::string &loader_map() const { return loader_map_; }
	void set_fixed(bool on) { fixed_ = on; }
	bool fixed() const { return fixed_; }

	bool force_defaults() const { return fixed_ && !loader_map_.empty() && !host_mode_; }


	void set_host_mode(bool on) { host_mode_ = on; }
	void clear_host_values() { host_.clear(); }
	bool host_mode() const { return host_mode_; }
	void set_host_values(std::map<std::string, Value> v) { host_ = std::move(v); }
	const std::map<std::string, Value> &host_values() const { return host_; }

	bool resolve(const Param &p, const double stock[4], double out[4], std::string *owner = nullptr) const;
};

double clampd(double v, double lo, double hi);




bool sanitize_host_value(const Param &p, const double *in, int n, Value &out);

constexpr double kTimeScaleMin = 0.1, kTimeScaleMax = 4.0;
bool sanitize_time_scale(double in, double &out);
constexpr int kHostPayloadVersion = 2;
constexpr int kHostPayloadMaxBytes = 32768;

}
