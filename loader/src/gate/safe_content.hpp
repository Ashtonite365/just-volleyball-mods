




#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace jvm::safe {

constexpr uint64_t kMaxImageFileBytes = 32u * 1024 * 1024;
constexpr uint32_t kMaxImageSide = 8192;
constexpr uint64_t kMaxImagePixels = uint64_t(8192) * 4096;
constexpr uint64_t kMaxGlbBytes = 64u * 1024 * 1024;
constexpr uint64_t kMaxJsonBytes = 1u * 1024 * 1024;
constexpr size_t kMaxRelPath = 200;
constexpr int kMaxRelDepth = 8;

enum class Type { None, Png, Jpeg, Webp, Glb, Json };
const char *type_name(Type t);

std::string lower(std::string s);
std::string ext_of(const std::string &path);

std::optional<std::string> pack_rel(const std::string &rel);

bool image_ext(const std::string &ext);
bool content_ext(const std::string &ext);

Type sniff(const uint8_t *p, size_t n);

struct ImageInfo { Type type = Type::None; uint32_t width = 0, height = 0; };
std::optional<ImageInfo> probe_image(const uint8_t *p, size_t n);

std::string check_image(const uint8_t *p, size_t n, const std::string &ext, ImageInfo *info = nullptr);

struct GlbResult {
	std::string error;
	std::vector<uint8_t> glb;
	int stripped_extensions = 0;
	int stripped_extras = 0;
	int dropped_chunks = 0;
	int images = 0;
};
GlbResult sanitize_glb(const uint8_t *p, size_t n);

bool glb_extension_allowed(const std::string &name);

std::string check_json_bytes(const uint8_t *p, size_t n);

std::string check_content_file(const std::string &rel, const uint8_t *p, size_t n);

bool game_texture_target(const std::string &res_path);

}
