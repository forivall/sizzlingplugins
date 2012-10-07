////////////////////////////////////////////////////////////////////////////////
// Filename: UserIdTracker.cpp
////////////////////////////////////////////////////////////////////////////////
#include "UserIdTracker.h"

#include "eiface.h"

// Interfaces from the engine
extern IVEngineServer			*pEngine;

static UserIdTracker gUserIdTracker;
UserIdTracker *g_pUserIdTracker = &gUserIdTracker;

UserIdTracker::UserIdTracker()
{
	// increases binary size by the array size for some reason
	//Load();
}

UserIdTracker::~UserIdTracker()
{	
}

void UserIdTracker::Load()
{
	memset( m_entIndexTable, -1, 65536 );
}

int UserIdTracker::ClientActive( edict_t *pEdict )
{
	int userid = pEngine->GetPlayerUserId( pEdict );
	m_entIndexTable[userid] = pEngine->IndexOfEdict( pEdict );
	return static_cast<int>( m_entIndexTable[userid] );
}

void UserIdTracker::ClientDisconnect( edict_t *pEdict )
{
	int userid = pEngine->GetPlayerUserId( pEdict );
	m_entIndexTable[userid] = -1;
}

int UserIdTracker::GetEntIndex( int userid )
{
	return static_cast<int>( m_entIndexTable[userid] );
}
