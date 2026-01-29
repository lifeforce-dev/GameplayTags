#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <functional>

//=============================================================================
// Constants
//=============================================================================

static constexpr uint32_t kInvalidTagId = 0;
static constexpr std::size_t kMaxTagLength = 512;

//=============================================================================
// StringArena - Monotonic allocator for stable string storage
//=============================================================================

// Provides stable memory for strings that never need to be freed individually.
// Returned pointers remain valid for the lifetime of the arena.
class StringArena
{
public:
	static constexpr std::size_t kBlockSize = 4096;
	using Block = std::array<char, kBlockSize>;

	StringArena() = default;

	// Non-copyable, non-movable (pointers into blocks must remain stable).
	StringArena(const StringArena&) = delete;
	StringArena& operator=(const StringArena&) = delete;
	StringArena(StringArena&&) = delete;
	StringArena& operator=(StringArena&&) = delete;

	// Store a string and return a stable pointer. The pointer remains valid
	// for the lifetime of this arena. Returns nullptr if string exceeds max length.
	[[nodiscard]] const char* Store(std::string_view str)
	{
		// Reject oversized strings to prevent memory exhaustion.
		if (str.size() > kMaxTagLength)
		{
			return nullptr;
		}

		std::size_t requiredSize = str.size() + 1;  // Include null terminator.

		// Allocate new block if current block has insufficient space.
		if (m_offset + requiredSize > kBlockSize)
		{
			m_blocks.push_back(std::make_unique<Block>());
			m_offset = 0;
		}

		char* dest = m_blocks.back()->data() + m_offset;
		std::memcpy(dest, str.data(), str.size());
		dest[str.size()] = '\0';
		m_offset += requiredSize;

		return dest;
	}

private:
	std::vector<std::unique_ptr<Block>> m_blocks;
	std::size_t m_offset = kBlockSize;  // Forces first block allocation.
};

//=============================================================================
// StringRef - Lightweight handle to arena-stored string
//=============================================================================

struct StringRef
{
	// Use empty string literal instead of nullptr for portability.
	static constexpr const char* kEmptyString = "";

	const char* ptr = kEmptyString;
	uint32_t len = 0;

	[[nodiscard]] operator std::string_view() const noexcept
	{
		return {ptr, len};
	}

	[[nodiscard]] bool operator==(std::string_view other) const noexcept
	{
		return std::string_view{ptr, len} == other;
	}
};

//=============================================================================
// GameplayTag - Lightweight handle to an interned tag path
//=============================================================================

class GameplayTag
{
public:
	constexpr GameplayTag() noexcept = default;

	[[nodiscard]] bool operator==(const GameplayTag& other) const noexcept { return m_id == other.m_id; }
	[[nodiscard]] bool operator!=(const GameplayTag& other) const noexcept { return m_id != other.m_id; }
	[[nodiscard]] bool operator<(const GameplayTag& other) const noexcept { return m_id < other.m_id; }

	[[nodiscard]] bool IsValid() const noexcept { return m_id != kInvalidTagId; }
	[[nodiscard]] explicit operator bool() const noexcept { return IsValid(); }

	// Returns the full tag path (e.g., "Gameplay.Character.Status.Stunned").
	// The returned view is stable for the lifetime of the TagRegistry.
	// Use for debugging/serialization only - not for runtime comparisons.
	[[nodiscard]] std::string_view GetName() const noexcept;

	// Returns the parent tag, or invalid tag if this is a root-level tag.
	[[nodiscard]] GameplayTag GetParent() const noexcept;

	// Hash support for use in containers.
	[[nodiscard]] std::size_t GetHash() const noexcept { return static_cast<std::size_t>(m_id); }

	static const GameplayTag None;

private:
	friend class TagRegistry;

	explicit constexpr GameplayTag(uint32_t id) noexcept : m_id(id) {}

	uint32_t m_id = kInvalidTagId;
};

// Hash specialization for std::unordered_set/map.
namespace std
{
	template<>
	struct hash<GameplayTag>
	{
		std::size_t operator()(const GameplayTag& tag) const noexcept
		{
			return tag.GetHash();
		}
	};
}

//=============================================================================
// TagRegistry - Singleton managing all tag registrations
//
// USAGE:
//   1. At startup, register all valid tags via RegisterTag() or
//      RegisterTagsFromSource().
//   2. Call FreezeRegistry() to lock the registry.
//   3. At runtime, use GetTag() to look up tags. Lookups for unregistered
//      tags will assert in debug builds and log an error in release.
//
// THREADING: This class is NOT thread-safe. All tag registration and the
// FreezeRegistry() call must occur on a single thread during initialization.
// The caller must ensure FreezeRegistry() completes and is visible to all
// threads (e.g., via a startup barrier) before any concurrent access.
// After that point, read-only operations are safe from any thread.
//
// LIFETIME: String views returned by GetTagName() are stable for the lifetime
// of the registry (which is effectively program lifetime for the singleton).
//=============================================================================

class TagRegistry
{
public:
	// Singleton access.
	[[nodiscard]] static TagRegistry& Get();

	// ====================================
	// REGISTRATION (startup only)
	// ====================================

	// Register a tag by path. Creates the tag and all parent tags.
	// Must be called before FreezeRegistry(). Returns invalid tag if frozen.
	[[nodiscard]] GameplayTag RegisterTag(std::string_view tagPath);

	// Register tags from an external source (e.g., config file).
	// Loader returns a list of tag paths to register.
	using RegistryLoader = std::function<std::vector<std::string>()>;
	void RegisterTagsFromSource(RegistryLoader loader);

	// Lock the registry. After this call:
	//   - RegisterTag() will fail and log an error
	//   - GetTag() for unknown tags will log an error
	void FreezeRegistry() noexcept { m_frozen = true; }
	[[nodiscard]] bool IsFrozen() const noexcept { return m_frozen; }

	// ====================================
	// LOOKUP (runtime)
	// ====================================

	// Get a tag by path. If not found: asserts in debug builds, logs in release.
	// This is the primary runtime API after FreezeRegistry().
	[[nodiscard]] GameplayTag GetTag(std::string_view tagPath) const;

	// Get tag name by ID. Returns empty view for invalid ID.
	[[nodiscard]] std::string_view GetTagName(uint32_t id) const noexcept;

	// Get parent tag ID. Returns kInvalidTagId for root tags.
	[[nodiscard]] uint32_t GetParentId(uint32_t id) const noexcept;

	// Non-copyable singleton.
	TagRegistry(const TagRegistry&) = delete;
	TagRegistry& operator=(const TagRegistry&) = delete;

private:
	TagRegistry();
	~TagRegistry() = default;

	uint32_t InternTag(std::string_view tagPath);
	uint32_t FindTagId(std::string_view tagPath) const;

	// Stable string storage.
	StringArena m_arena;

	// String table: index -> StringRef (pointer + length into arena).
	std::vector<StringRef> m_strings;

	// Reverse lookup: path -> index. Using vector of pairs for simplicity.
	// For larger registries, consider std::unordered_map.
	std::vector<std::pair<StringRef, uint32_t>> m_lookup;

	// Parent ID for each tag (kInvalidTagId = no parent).
	std::vector<uint32_t> m_parentIds;

	bool m_frozen = false;
};

//=============================================================================
// GameplayTagContainer - Holds a set of tags with parent matching
//=============================================================================

class GameplayTagContainer
{
public:
	GameplayTagContainer() = default;

	// Add a tag (also enables matching on all parent tags).
	void AddTag(GameplayTag tag);

	// Remove an explicit tag. Parent matching is rebuilt automatically.
	bool RemoveTag(GameplayTag tag);

	// Check if tag is present (matches explicit tags OR their parents).
	[[nodiscard]] bool HasTag(GameplayTag tag) const;

	// Check if tag is explicitly present (exact match only, no parent matching).
	[[nodiscard]] bool HasTagExact(GameplayTag tag) const;

	// Check if this container has ANY of the tags in the other container.
	[[nodiscard]] bool HasAny(const GameplayTagContainer& other) const;

	// Check if this container has ALL of the tags in the other container.
	[[nodiscard]] bool HasAll(const GameplayTagContainer& other) const;

	// Exact variants (no parent matching).
	[[nodiscard]] bool HasAnyExact(const GameplayTagContainer& other) const;
	[[nodiscard]] bool HasAllExact(const GameplayTagContainer& other) const;

	// Container operations.
	void Clear();
	[[nodiscard]] bool IsEmpty() const noexcept { return m_explicitTags.empty(); }
	[[nodiscard]] std::size_t Num() const noexcept { return m_explicitTags.size(); }

	// Iteration over explicit tags.
	[[nodiscard]] const std::vector<GameplayTag>& GetExplicitTags() const noexcept { return m_explicitTags; }

	// String conversion for serialization/debugging.
	[[nodiscard]] std::string ToString() const;
	static GameplayTagContainer FromString(std::string_view str);

	// Convenience: add/check by string (delegates to TagRegistry).
	// AddTag registers the tag if not frozen. Query methods (HasTag, HasTagExact,
	// RemoveTag) require tags to already be registered and will assert/log if not.
	void AddTag(std::string_view tagPath);
	bool RemoveTag(std::string_view tagPath);
	[[nodiscard]] bool HasTag(std::string_view tagPath) const;
	[[nodiscard]] bool HasTagExact(std::string_view tagPath) const;

private:
	void RebuildParentTags();
	[[nodiscard]] bool ContainsInEither(GameplayTag tag) const;

	std::vector<GameplayTag> m_explicitTags;  // Tags explicitly added.
	std::vector<GameplayTag> m_parentTags;    // Derived parent tags, rebuilt on mutation.
};
