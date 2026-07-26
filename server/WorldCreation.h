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
};
