#include "GameplayTags.h"

#include <cassert>
#include <iostream>

//=============================================================================
// Test Utilities
//=============================================================================

#define TEST(name) void name(); \
	struct name##_register { name##_register() { std::cout << "Running " #name "...\n"; name(); std::cout << "  PASSED\n"; } } name##_instance; \
	void name()

//=============================================================================
// Core Functionality Tests
//=============================================================================

TEST(TestBasicTagCreation)
{
	auto tag = TagRegistry::Get().RegisterTag("Gameplay.Character.Status");
	assert(tag.IsValid());
	assert(tag.GetName() == "Gameplay.Character.Status");
}

TEST(TestInvalidTagHandling)
{
	// Empty string should return invalid tag.
	auto emptyTag = TagRegistry::Get().RegisterTag("");
	assert(!emptyTag.IsValid());
	
	// None tag should report as invalid.
	assert(!GameplayTag::None.IsValid());
	assert(GameplayTag::None.GetName().empty());
	
	// Adding invalid tags should be no-op.
	GameplayTagContainer container;
	container.AddTag(GameplayTag::None);
	container.AddTag("");
	assert(container.IsEmpty());
	
	// HasTag on invalid tag should return false.
	container.AddTag("Some.Valid.Tag");
	assert(!container.HasTag(GameplayTag::None));
	assert(!container.HasTagExact(GameplayTag::None));
}

TEST(TestDuplicateAdd)
{
	GameplayTagContainer container;
	container.AddTag("Duplicate.Test.Tag");
	container.AddTag("Duplicate.Test.Tag");
	container.AddTag("Duplicate.Test.Tag");
	
	// Should only have one explicit tag.
	assert(container.Num() == 1);
	
	// Removing once should clear it.
	container.RemoveTag("Duplicate.Test.Tag");
	assert(container.IsEmpty());
}

TEST(TestRemoveNonExistent)
{
	GameplayTagContainer container;
	
	// Removing from empty container should be safe.
	bool removed = container.RemoveTag("Never.Added.Tag");
	assert(!removed);
	assert(container.IsEmpty());
	
	// Removing non-existent from non-empty container.
	container.AddTag("Exists.Tag");
	removed = container.RemoveTag("Does.Not.Exist");
	assert(!removed);
	assert(container.Num() == 1);
	assert(container.HasTagExact("Exists.Tag"));
}

TEST(TestParentTagChain)
{
	auto tag = TagRegistry::Get().RegisterTag("A.B.C.D");
	
	auto parent1 = tag.GetParent();
	assert(parent1.IsValid());
	assert(parent1.GetName() == "A.B.C");
	
	auto parent2 = parent1.GetParent();
	assert(parent2.IsValid());
	assert(parent2.GetName() == "A.B");
	
	auto parent3 = parent2.GetParent();
	assert(parent3.IsValid());
	assert(parent3.GetName() == "A");
	
	auto parent4 = parent3.GetParent();
	assert(!parent4.IsValid());  // Root has no parent.
}

TEST(TestTagEquality)
{
	auto tag1 = TagRegistry::Get().RegisterTag("Test.Tag.One");
	auto tag2 = TagRegistry::Get().RegisterTag("Test.Tag.One");
	auto tag3 = TagRegistry::Get().RegisterTag("Test.Tag.Two");
	
	assert(tag1 == tag2);  // Same path = same tag.
	assert(tag1 != tag3);  // Different path = different tag.
}

//=============================================================================
// P0 Fix: Hash Collision Test
//=============================================================================

TEST(TestNoHashCollision)
{
	// This was the P0 bug: two tags with same leaf segment but different paths
	// would incorrectly match because only segment hashes were stored.
	
	GameplayTagContainer container;
	container.AddTag("Gameplay.Character.Status.Stunned");
	
	// These should NOT match - different parent paths.
	assert(!container.HasTag("UI.Menu.Status"));
	assert(!container.HasTag("Audio.Effect.Status"));
	assert(!container.HasTag("Status"));  // Root-level "Status" is different.
	
	// These SHOULD match - they're parents of the added tag.
	assert(container.HasTag("Gameplay.Character.Status"));
	assert(container.HasTag("Gameplay.Character"));
	assert(container.HasTag("Gameplay"));
}

//=============================================================================
// P1 Fix: RemoveTag Correctly Rebuilds Parents
//=============================================================================

TEST(TestRemoveTagRebuildsParents)
{
	GameplayTagContainer container;
	
	// Add two tags that share parent "A.B".
	container.AddTag("A.B.C");
	container.AddTag("A.B.D");
	
	assert(container.HasTag("A.B"));  // Parent should match.
	assert(container.HasTag("A"));
	
	// Remove one leaf - parent should still match.
	container.RemoveTag("A.B.C");
	assert(container.HasTag("A.B"));  // D still provides this parent.
	assert(container.HasTag("A"));
	
	// Remove the other leaf - parents should NO LONGER match.
	container.RemoveTag("A.B.D");
	assert(!container.HasTag("A.B"));  // No more children!
	assert(!container.HasTag("A"));
	assert(container.IsEmpty());
}

//=============================================================================
// HasTagExact Tests
//=============================================================================

TEST(TestHasTagExact)
{
	GameplayTagContainer container;
	container.AddTag("Gameplay.Character.Status.Stunned");
	
	// HasTag matches parents.
	assert(container.HasTag("Gameplay.Character.Status.Stunned"));
	assert(container.HasTag("Gameplay.Character.Status"));
	assert(container.HasTag("Gameplay.Character"));
	assert(container.HasTag("Gameplay"));
	
	// HasTagExact only matches explicitly added tags.
	assert(container.HasTagExact("Gameplay.Character.Status.Stunned"));
	assert(!container.HasTagExact("Gameplay.Character.Status"));
	assert(!container.HasTagExact("Gameplay.Character"));
	assert(!container.HasTagExact("Gameplay"));
}

//=============================================================================
// HasAny / HasAll Tests
//=============================================================================

TEST(TestHasAny)
{
	GameplayTagContainer container;
	container.AddTag("Status.Burning");
	container.AddTag("Status.Frozen");
	
	GameplayTagContainer query;
	query.AddTag("Status.Burning");
	query.AddTag("Status.Poisoned");
	
	assert(container.HasAny(query));  // Has Burning.
	
	GameplayTagContainer noMatch;
	noMatch.AddTag("Status.Stunned");
	noMatch.AddTag("Status.Poisoned");
	
	assert(!container.HasAny(noMatch));  // Has neither.
}

TEST(TestHasAll)
{
	GameplayTagContainer container;
	container.AddTag("Ability.Fire.Fireball");
	container.AddTag("Ability.Ice.Frostbolt");
	
	GameplayTagContainer subset;
	subset.AddTag("Ability.Fire");  // Parent of Fireball.
	subset.AddTag("Ability.Ice");   // Parent of Frostbolt.
	
	assert(container.HasAll(subset));  // Parent matching works.
	
	GameplayTagContainer tooMuch;
	tooMuch.AddTag("Ability.Fire");
	tooMuch.AddTag("Ability.Lightning");  // Not present.
	
	assert(!container.HasAll(tooMuch));
}

TEST(TestHasAnyAllWithEmptyContainer)
{
	GameplayTagContainer container;
	container.AddTag("Some.Tag");
	
	GameplayTagContainer empty;
	
	// HasAll with empty query is vacuously true (no requirements to fail).
	assert(container.HasAll(empty));
	assert(container.HasAllExact(empty));
	
	// HasAny with empty query is false (nothing to match).
	assert(!container.HasAny(empty));
	assert(!container.HasAnyExact(empty));
	
	// Empty container checked against non-empty.
	GameplayTagContainer emptyContainer;
	GameplayTagContainer query;
	query.AddTag("Any.Tag");
	
	assert(!emptyContainer.HasAny(query));
	assert(!emptyContainer.HasAll(query));
}

TEST(TestClearContainer)
{
	GameplayTagContainer container;
	container.AddTag("A.B.C");
	container.AddTag("X.Y.Z");
	
	assert(!container.IsEmpty());
	assert(container.HasTag("A.B"));  // Parent matching works.
	
	container.Clear();
	
	assert(container.IsEmpty());
	assert(container.Num() == 0);
	assert(!container.HasTag("A.B.C"));
	assert(!container.HasTag("A.B"));  // Parents also cleared.
	assert(!container.HasTag("A"));
}

//=============================================================================
// Serialization Tests
//=============================================================================

TEST(TestSerialization)
{
	GameplayTagContainer original;
	original.AddTag("Game.Mode.PvP");
	original.AddTag("Game.Mode.Ranked");
	original.AddTag("Player.Status.Ready");
	
	std::string serialized = original.ToString();
	GameplayTagContainer restored = GameplayTagContainer::FromString(serialized);
	
	assert(restored.Num() == original.Num());
	assert(restored.HasTagExact("Game.Mode.PvP"));
	assert(restored.HasTagExact("Game.Mode.Ranked"));
	assert(restored.HasTagExact("Player.Status.Ready"));
}

//=============================================================================
// Strict Mode Tests
//=============================================================================

TEST(TestFreezeRegistry)
{
	// Pre-register some tags.
	TagRegistry::Get().RegisterTag("Registered.Tag.One");
	TagRegistry::Get().RegisterTag("Registered.Tag.Two");
	
	// Note: We can't truly test FreezeRegistry in static init tests
	// because once frozen, other tests would fail. Just verify the API exists.
	assert(!TagRegistry::Get().IsFrozen());
	
	// Registered tags should be findable via GetTag.
	auto valid = TagRegistry::Get().GetTag("Registered.Tag.One");
	assert(valid.IsValid());
}

//=============================================================================
// Entry Point
//=============================================================================

int main()
{
	std::cout << "=== GameplayTags Test Suite ===\n\n";
	// Tests run automatically via static initialization.
	std::cout << "\nAll tests passed!\n";


	GameplayTagContainer container;
	TagRegistry::Get().RegisterTag("Gameplay.Status.Frozen");
	TagRegistry::Get().RegisterTag("Gameplay.Status.Stunned");
	TagRegistry::Get().RegisterTag("Gameplay.Status.Poisoned");
	TagRegistry::Get().RegisterTag("Gameplay.Status.Burned");

	TagRegistry::Get().RegisterTag("Gameplay.State.Running");
	TagRegistry::Get().RegisterTag("Gameplay.State.Jumping");
	TagRegistry::Get().RegisterTag("Gameplay.State.Shooting");

	container.AddTag("Gameplay.State.Running");
	if (container.HasTag("Gameplay.State.Running"))
	{
		std::cout << "Container has Gameplay.State tag via parent matching.\n";
		container.AddTag("Gameplay.Status.Frozen");
	}
	else
	{
		std::cout << "Container does NOT have Gameplay.State tag.\n";
	}



	return 0;
}
