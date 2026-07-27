/*=====================================================================
WorldCreation.h
---------------
Copyright Glare Technologies Limited 2022 -
=====================================================================*/
#pragma once


#include "ServerWorldState.h"


/*=====================================================================
WorldCreation
-------------
Parcel layout and creation, road layout and creation etc.
=====================================================================*/
class WorldCreation
{
public:
	static void createParcelsAndRoads(Reference<ServerAllWorldsState> world_state);

	static void removeHypercardMaterials(ServerAllWorldsState& all_worlds_state);

	static void createPhysicsTest(ServerAllWorldsState& all_worlds_state);

	// Idempotent: creates a purple test cube near the default spawn point if one doesn't already exist (identified by its 'content' marker string).
	// Used to verify we have working end-to-end control over server-side world state.
	static void ensurePurpleTestCubeExists(Reference<ServerAllWorldsState> world_state);

	// Idempotent: creates a large invisible-but-collidable slab (200x200m footprint, 10m thick, top surface at world z=10) to use as a new "virtual ground level"
	// for Gaussian splat testing, without touching the built-in terrain grid texture/rendering. See snapshot notes for why: hiding the built-in grid would require
	// editing shared client rendering code (TerrainSystem.cpp), which the owner didn't want to touch; this sidesteps that entirely by staying in our own test-scene setup file.
	static void ensureTestGroundPlatformExists(Reference<ServerAllWorldsState> world_state);
};
