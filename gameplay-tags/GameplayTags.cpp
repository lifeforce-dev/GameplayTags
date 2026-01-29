#include "GameplayTags.h"

#include <algorithm>
#include <cassert>
#include <iostream>

//=============================================================================
// GameplayTag Implementation
//=============================================================================

const GameplayTag GameplayTag::None{};

std::string_view GameplayTag::GetName() const noexcept
{
	if (m_id == kInvalidTagId)
	{
		return {};
	}
	return TagRegistry::Get().GetTagName(m_id);
}

GameplayTag GameplayTag::GetParent() const noexcept
{
	if (m_id == kInvalidTagId)
	{
		return None;
	}
	uint32_t parentId = TagRegistry::Get().GetParentId(m_id);
	return GameplayTag{parentId};
}

//=============================================================================
// TagRegistry Implementation
//=============================================================================

TagRegistry& TagRegistry::Get()
{
	static TagRegistry instance;
	return instance;
}

TagRegistry::TagRegistry()
{
	// Reserve index 0 for the invalid/none tag.
	m_strings.push_back(StringRef{});
	m_parentIds.push_back(kInvalidTagId);
}

uint32_t TagRegistry::FindTagId(std::string_view tagPath) const
{
	auto it = std::find_if(m_lookup.begin(), m_lookup.end(),
		[tagPath](const std::pair<StringRef, uint32_t>& entry)
		{
			return entry.first == tagPath;
		});

	return (it != m_lookup.end()) ? it->second : kInvalidTagId;
}

GameplayTag TagRegistry::GetTag(std::string_view tagPath) const
{
	if (tagPath.empty())
	{
		return GameplayTag::None;
	}

	uint32_t id = FindTagId(tagPath);
	if (id == kInvalidTagId)
	{
#ifdef _DEBUG
		assert(false && "Tag not found in registry");
#endif
		std::cerr << "[GameplayTags] ERROR: Tag not found: \"" << tagPath << "\"\n";
		return GameplayTag::None;
	}

	return GameplayTag{id};
}

GameplayTag TagRegistry::RegisterTag(std::string_view tagPath)
{
	if (tagPath.empty())
	{
		return GameplayTag::None;
	}

	if (m_frozen)
	{
		std::cerr << "[GameplayTags] ERROR: Cannot register tag after FreezeRegistry(): \"" << tagPath << "\"\n";
		return GameplayTag::None;
	}

	// Check if already registered.
	uint32_t id = FindTagId(tagPath);
	if (id != kInvalidTagId)
	{
		return GameplayTag{id};
	}

	// Create the tag (and any missing parent tags).
	id = InternTag(tagPath);
	return GameplayTag{id};
}

void TagRegistry::RegisterTagsFromSource(RegistryLoader loader)
{
	if (m_frozen)
	{
		std::cerr << "[GameplayTags] ERROR: Cannot register tags after FreezeRegistry()\n";
		return;
	}

	std::vector<std::string> tags = loader();
	for (const auto& tagPath : tags)
	{
		if (!tagPath.empty())
		{
			InternTag(tagPath);
		}
	}
}

uint32_t TagRegistry::InternTag(std::string_view tagPath)
{
	// Check if already exists.
	uint32_t existing = FindTagId(tagPath);
	if (existing != kInvalidTagId)
	{
		return existing;
	}

	// Collect all ancestor paths that need to be created (deepest first).
	std::vector<std::string_view> pathsToCreate;
	pathsToCreate.push_back(tagPath);

	std::string_view currentPath = tagPath;
	while (true)
	{
		std::size_t lastDot = currentPath.rfind('.');
		if (lastDot == std::string_view::npos)
		{
			break;
		}

		currentPath = currentPath.substr(0, lastDot);
		if (FindTagId(currentPath) != kInvalidTagId)
		{
			break;  // This ancestor already exists.
		}

		pathsToCreate.push_back(currentPath);
	}

	// Create tags from shallowest to deepest (reverse order).
	uint32_t lastCreatedId = kInvalidTagId;
	for (auto it = pathsToCreate.rbegin(); it != pathsToCreate.rend(); ++it)
	{
		std::string_view path = *it;

		// Determine parent ID.
		uint32_t parentId = kInvalidTagId;
		std::size_t lastDot = path.rfind('.');
		if (lastDot != std::string_view::npos)
		{
			std::string_view parentPath = path.substr(0, lastDot);
			parentId = FindTagId(parentPath);
		}

		// Store string in arena and create entry.
		const char* storedPtr = m_arena.Store(path);
		if (storedPtr == nullptr)
		{
			std::cerr << "[GameplayTags] ERROR: Tag path exceeds maximum length (" 
			          << kMaxTagLength << "): \"" << path.substr(0, 64) << "...\"\n";
			return kInvalidTagId;
		}

		StringRef ref{storedPtr, static_cast<uint32_t>(path.size())};

		uint32_t newId = static_cast<uint32_t>(m_strings.size());
		m_strings.push_back(ref);
		m_parentIds.push_back(parentId);
		m_lookup.emplace_back(ref, newId);

		lastCreatedId = newId;
	}

	return lastCreatedId;
}

std::string_view TagRegistry::GetTagName(uint32_t id) const noexcept
{
	if (id < m_strings.size())
	{
		return m_strings[id];
	}
	return {};
}

uint32_t TagRegistry::GetParentId(uint32_t id) const noexcept
{
	if (id < m_parentIds.size())
	{
		return m_parentIds[id];
	}
	return kInvalidTagId;
}

//=============================================================================
// GameplayTagContainer Implementation
//=============================================================================

void GameplayTagContainer::AddTag(GameplayTag tag)
{
	if (!tag.IsValid())
	{
		return;
	}

	// Check if already present.
	auto it = std::find(m_explicitTags.begin(), m_explicitTags.end(), tag);
	if (it != m_explicitTags.end())
	{
		return;
	}

	m_explicitTags.push_back(tag);
	RebuildParentTags();
}

void GameplayTagContainer::AddTag(std::string_view tagPath)
{
	AddTag(TagRegistry::Get().RegisterTag(tagPath));
}

bool GameplayTagContainer::RemoveTag(GameplayTag tag)
{
	auto it = std::find(m_explicitTags.begin(), m_explicitTags.end(), tag);
	if (it == m_explicitTags.end())
	{
		return false;
	}

	m_explicitTags.erase(it);
	RebuildParentTags();
	return true;
}

bool GameplayTagContainer::RemoveTag(std::string_view tagPath)
{
	return RemoveTag(TagRegistry::Get().GetTag(tagPath));
}

void GameplayTagContainer::RebuildParentTags()
{
	m_parentTags.clear();

	for (GameplayTag tag : m_explicitTags)
	{
		GameplayTag parent = tag.GetParent();
		while (parent.IsValid())
		{
			// Add if not already present (in either collection).
			if (!ContainsInEither(parent))
			{
				m_parentTags.push_back(parent);
			}
			parent = parent.GetParent();
		}
	}
}

bool GameplayTagContainer::ContainsInEither(GameplayTag tag) const
{
	auto inExplicit = std::find(m_explicitTags.begin(), m_explicitTags.end(), tag);
	if (inExplicit != m_explicitTags.end())
	{
		return true;
	}

	auto inParents = std::find(m_parentTags.begin(), m_parentTags.end(), tag);
	return inParents != m_parentTags.end();
}

bool GameplayTagContainer::HasTag(GameplayTag tag) const
{
	if (!tag.IsValid())
	{
		return false;
	}
	return ContainsInEither(tag);
}

bool GameplayTagContainer::HasTag(std::string_view tagPath) const
{
	return HasTag(TagRegistry::Get().GetTag(tagPath));
}

bool GameplayTagContainer::HasTagExact(GameplayTag tag) const
{
	if (!tag.IsValid())
	{
		return false;
	}
	auto it = std::find(m_explicitTags.begin(), m_explicitTags.end(), tag);
	return it != m_explicitTags.end();
}

bool GameplayTagContainer::HasTagExact(std::string_view tagPath) const
{
	return HasTagExact(TagRegistry::Get().GetTag(tagPath));
}

bool GameplayTagContainer::HasAny(const GameplayTagContainer& other) const
{
	return std::any_of(other.m_explicitTags.begin(), other.m_explicitTags.end(),
		[this](GameplayTag tag) { return HasTag(tag); });
}

bool GameplayTagContainer::HasAll(const GameplayTagContainer& other) const
{
	return std::all_of(other.m_explicitTags.begin(), other.m_explicitTags.end(),
		[this](GameplayTag tag) { return HasTag(tag); });
}

bool GameplayTagContainer::HasAnyExact(const GameplayTagContainer& other) const
{
	return std::any_of(other.m_explicitTags.begin(), other.m_explicitTags.end(),
		[this](GameplayTag tag) { return HasTagExact(tag); });
}

bool GameplayTagContainer::HasAllExact(const GameplayTagContainer& other) const
{
	return std::all_of(other.m_explicitTags.begin(), other.m_explicitTags.end(),
		[this](GameplayTag tag) { return HasTagExact(tag); });
}

void GameplayTagContainer::Clear()
{
	m_explicitTags.clear();
	m_parentTags.clear();
}

std::string GameplayTagContainer::ToString() const
{
	std::string result;
	for (std::size_t i = 0; i < m_explicitTags.size(); ++i)
	{
		if (i > 0)
		{
			result += ',';
		}
		result += m_explicitTags[i].GetName();
	}
	return result;
}

GameplayTagContainer GameplayTagContainer::FromString(std::string_view str)
{
	GameplayTagContainer container;

	std::size_t pos = 0;
	while (pos < str.size())
	{
		std::size_t comma = str.find(',', pos);
		if (comma == std::string_view::npos)
		{
			comma = str.size();
		}

		std::string_view tagPath = str.substr(pos, comma - pos);
		if (!tagPath.empty())
		{
			container.AddTag(tagPath);
		}

		pos = comma + 1;
	}

	return container;
}
