









#pragma once
#include <string>

namespace jvm::loader {

inline constexpr const char *kGoofyNodePath = "/root/JVGoofyMods";
inline constexpr int kGoofyApiMajor = 1;


inline bool api_compatible(const std::string &version) {
	if (version.empty()) return false;
	size_t dot = version.find('.');
	std::string major = version.substr(0, dot);
	if (major.empty()) return false;
	for (char c : major) if (c < '0' || c > '9') return false;
	return std::stoi(major) == kGoofyApiMajor;
}

struct BridgeState {

	bool goofy_usable = false;
	std::string map;
	bool has_block = false;
	std::string block_sig;

	bool told = false;
	std::string told_map;
	std::string applied_pack;
	std::string applied_sig;
};

struct Decision {
	bool set_map = false;
	std::string clear_pack;
	bool apply = false;
	const char *why = "";
};

inline Decision decide(const BridgeState &s) {
	Decision d;
	if (!s.goofy_usable) { d.why = "goofy not installed (or no compatible API): goofy section ignored"; return d; }
	const bool want_block = !s.map.empty() && s.has_block;

	if (!s.applied_pack.empty() && (!want_block || s.applied_pack != s.map)) d.clear_pack = s.applied_pack;
	if (!s.told || s.told_map != s.map) d.set_map = true;
	if (want_block && (s.applied_pack != s.map || s.applied_sig != s.block_sig)) d.apply = true;
	if (s.map.empty()) d.why = "no loader map in play";
	else if (!s.has_block) d.why = "loader map without a goofy section";
	else d.why = "goofy section handed to goofy (live only while goofy is enabled)";
	return d;
}

}
