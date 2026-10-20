#if WITH_DEV_AUTOMATION_TESTS

#include "../TankInputSequence.h"
#include "Misc/AutomationTest.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTankMovementIntentSequenceTest, "NetworkTankArena.Multiplayer.MovementIntentSequence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTankMovementIntentSequenceTest::RunTest(const FString& Parameters)
{
	using NetworkTankArena::FMovementIntentSequenceState;
	constexpr uint32 Max = std::numeric_limits<uint32>::max();
	FMovementIntentSequenceState State;
	TestFalse(TEXT("Fresh history has no accepted sequence"), State.bHasAcceptedSequence);
	TestTrue(TEXT("First zero is accepted"), State.TryAccept(0));
	TestTrue(TEXT("First acceptance establishes history"), State.bHasAcceptedSequence);
	TestTrue(TEXT("Increasing sequence is accepted"), State.TryAccept(1));
	TestTrue(TEXT("Skipped sequence is accepted"), State.TryAccept(4));
	TestFalse(TEXT("Duplicate is rejected"), State.TryAccept(4));
	TestFalse(TEXT("Older/reordered sequence is rejected"), State.TryAccept(3));
	TestFalse(TEXT("Exactly half-range is rejected"), State.TryAccept(4u + 0x80000000u));
	TestEqual(TEXT("Rejection preserves last accepted sequence"), State.LastAcceptedSequence, 4u);

	FMovementIntentSequenceState Wrapped;
	TestTrue(TEXT("First nonzero is accepted, even near wrap"), Wrapped.TryAccept(Max - 1));
	TestTrue(TEXT("Maximum sequence is accepted"), Wrapped.TryAccept(Max));
	TestTrue(TEXT("Zero after maximum is newer"), Wrapped.TryAccept(0));
	TestTrue(TEXT("One after wrapped zero is newer"), Wrapped.TryAccept(1));
	TestFalse(TEXT("Pre-wrap maximum is stale after wrap"), Wrapped.TryAccept(Max));
	TestFalse(TEXT("Pre-wrap maximum-minus-one is stale after wrap"), Wrapped.TryAccept(Max - 1));
	TestFalse(TEXT("Wrapped zero is stale after one"), Wrapped.TryAccept(0));
	TestFalse(TEXT("Wrapped duplicate is rejected"), Wrapped.TryAccept(1));
	TestFalse(TEXT("Half-range is also rejected after wrap"), Wrapped.TryAccept(1u + 0x80000000u));
	TestEqual(TEXT("Wrap rejections preserve history"), Wrapped.LastAcceptedSequence, 1u);
	return true;
}

#endif
