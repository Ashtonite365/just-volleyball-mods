






#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace jvm::content {


using Hash = std::array<uint8_t, 32>;
Hash sha256(const uint8_t *data, size_t n);
struct Sha256 {
	Sha256();
	void update(const uint8_t *data, size_t n);
	Hash finish();
private:
	uint32_t h_[8]; uint8_t buf_[64]; uint64_t len_ = 0; size_t used_ = 0;
	void block(const uint8_t *p);
};
std::string to_hex(const Hash &h);
std::optional<Hash> from_hex(const std::string &s);




struct FileEntry { std::string path; Hash sha; uint64_t size = 0; std::string url; };
Hash tree_hash(std::vector<FileEntry> files);
uint64_t tree_size(const std::vector<FileEntry> &files);


enum class Kind : uint8_t { Outfit = 1, Accessory = 2, Ball = 3, Model = 4, Court = 5 };
const char *kind_name(Kind k);
std::optional<Kind> kind_from_type(const std::string &pack_type);

struct Item {
	uint16_t id = 0;
	Kind kind = Kind::Outfit;
	Hash hash{};
	uint32_t size = 0;
	bool operator==(const Item &o) const { return id == o.id && kind == o.kind && hash == o.hash && size == o.size; }
};




constexpr uint8_t kTypeOffer = 6, kTypeOfferOf = 7;
constexpr size_t kMaxItems = 16;
struct Offer { bool relayed = false; int32_t peer = 0; std::vector<Item> items; };
std::vector<uint8_t> encode(const Offer &o);
std::optional<Offer> decode(const std::vector<uint8_t> &bytes);


inline constexpr const char *kRepoPrefix = "https://raw.githubusercontent.com/Ashtonite365/just-volleyball-assets/";
inline constexpr const char *kRawBase = "https://raw.githubusercontent.com/Ashtonite365/just-volleyball-assets/main/";
inline constexpr const char *kManifestUrl = "https://raw.githubusercontent.com/Ashtonite365/just-volleyball-assets/main/manifest.json";
constexpr int kSchemaVersion = 1;
constexpr uint64_t kMaxManifestBytes = 512 * 1024;
constexpr uint64_t kMaxPackBytes = 16u * 1024 * 1024;
constexpr uint64_t kMaxFileBytes = 16u * 1024 * 1024;
constexpr uint64_t kMaxUnpackedBytes = 48u * 1024 * 1024;
constexpr size_t kMaxEntries = 64;

bool url_allowed(const std::string &url, const std::string &base = kRawBase);
bool safe_repo_path(const std::string &path);
std::string cache_folder(const std::string &repo_path);

struct RawManifestItem {
	std::string id, name, type, sha256, download_url, path;
	double size = -1;
	std::vector<std::pair<std::string, std::pair<std::string, double>>> files;
	std::map<std::string, std::string> file_urls;
};
struct CatalogItem {
	uint16_t id = 0; std::string name, type; Kind kind = Kind::Outfit;
	Hash zip_sha{}; uint64_t zip_size = 0; std::string url, folder;
	std::vector<FileEntry> files; Hash tree{}; uint64_t tree_bytes = 0;
};
bool files_have_urls(const CatalogItem &ci);


std::string install_verified_files(const std::string &dest, const CatalogItem &ci,
		const std::vector<std::pair<std::string, std::vector<uint8_t>>> &bodies);
struct Catalog { std::map<uint16_t, CatalogItem> items; std::vector<std::string> errors; };


Catalog validate_manifest(double schema_version, const std::vector<RawManifestItem> &raw, const std::string &base = kRawBase);






std::optional<std::string> safe_entry(const std::string &name);
bool allowed_ext(const std::string &path);


enum class Decision { Have, Download, NotInCatalog, HashMismatch, LocalDiffers, TooBig };
const char *decision_name(Decision d);


Decision decide(const Item &offered, const std::map<uint16_t, Hash> &installed, const Catalog &cat);

}
