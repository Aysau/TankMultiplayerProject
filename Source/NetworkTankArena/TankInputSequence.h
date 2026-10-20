#pragma once

#include "CoreTypes.h"

namespace NetworkTankArena
{
	// One history per PlayerController lifetime, independent of pawn/input clearing.
	struct FInputSequenceState
	{
		bool bHasAcceptedSequence = false;
		uint32 LastAcceptedSequence = 0;

		bool TryAccept(uint32 Candidate)
		{
			const uint32 Distance = Candidate - LastAcceptedSequence;
			const bool bNewer = Distance != 0 && Distance < 0x80000000u;
			// Serial-number arithmetic assumes fewer than 2^31 outstanding increments.
			// Zero is a duplicate; exactly half the range is ambiguous and rejected.
			if (bHasAcceptedSequence && !bNewer)
			{
				return false;
			}
			bHasAcceptedSequence = true;
			LastAcceptedSequence = Candidate;
			return true;
		}
	};
	// Movement and fire use distinct instances, never a shared sequence history.
	using FMovementIntentSequenceState = FInputSequenceState;
}
