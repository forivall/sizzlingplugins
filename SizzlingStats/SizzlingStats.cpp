////////////////////////////////////////////////////////////////////////////////
// Filename: SizzlingStats.cpp
////////////////////////////////////////////////////////////////////////////////
#include "SizzlingStats.h"
#include "PlayerMessage.h"
#include "playerdata.h"
#include "SC_helpers.h"
//#include "filesystem.h"
//#include "WebStatsHandler.h"
#include "HtmlGenerator.h"
#include "FtpUploader.h"
#include "utlbuffer.h"
#include "vstdlib/jobthread.h"
#include "ThreadCallQueue.h"
#include "curl/curl.h"

#include "fasttimer.h"

#include "eiface.h"
//#include "engine/iserverplugin.h"
#include "game/server/iplayerinfo.h"

#include "steam/steamclientpublic.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//extern PlayerMessage	*g_pMessage;
extern CGlobalVars		*gpGlobals;
extern IVEngineServer	*pEngine;
extern IPlayerInfoManager *playerinfomanager;
extern CTSCallQueue		*g_pTSCallQueue;

JOB_INTERFACE IThreadPool *g_pThreadPool;

#ifndef PUBLIC_RELEASE

static ConVar ftp_user("sizz_stats_ftp_username", "username", FCVAR_DONTRECORD | FCVAR_PROTECTED, "FTP username to connect with when using web stats.");
static ConVar ftp_pass("sizz_stats_ftp_password", "password", FCVAR_DONTRECORD | FCVAR_PROTECTED, "FTP password to connect with when using web stats.");
static ConVar ftp_server("sizz_stats_ftp_hostname", "ftp.myserver.com", FCVAR_DONTRECORD | FCVAR_PROTECTED, "FTP hostname to connect to when using web stats.");
static ConVar ftp_port("sizz_stats_ftp_port", "21", FCVAR_DONTRECORD | FCVAR_PROTECTED, "FTP port to use when using web stats.");
ConVar web_hostname("sizz_stats_web_hostname", "myserver.com", FCVAR_DONTRECORD | FCVAR_PROTECTED, "Hostname of the external public directory of the FTP server.");

#endif

#pragma warning( push )
#pragma warning( disable : 4351 )
SizzlingStats::SizzlingStats(): m_aPropOffsets(),
								m_PlayerFlagsOffset(0), 
								m_TeamRoundsWonOffset(0),
								m_PlayerClassOffset(0), // what am i using this for yet??
								m_flTimeOfLastCap(0.0f), // the stupid hack is back
								m_nCurrentPlayers(0),
								m_nCurrentRound(0),
								m_playerDataArchive(),
								//m_playerDataArchiveVec(0, 32),
								m_PlayerDataMemPool(MAX_PLAYERS),	// think about increasing this to
								m_ExtraDataMemPool(MAX_PLAYERS),	// avoid the slowdown from people joining
								m_pPlayerData(),
								m_pEntIndexToExtraData(),
#ifndef PUBLIC_RELEASE
								m_pWebStatsThread(new CWebStatsHandlerThread()),
#endif
								m_hostInfo()
{
	m_playerDataArchive.Init(32);

	// element 0 will never be used, element 33 might be used (in 33 slot servers)
	// we are still allocating for element 0 anyways... who cares
	//
	// should we allocate in here? or should we wait until we insert players
	// lets wait for now
	/*for (int i = 0; i < MAX_PLAYERS; ++i)
	{
		m_pPlayerData[i] = m_PlayerDataMemPool.Alloc();
		m_pEntIndexToExtraData[i] = m_ExtraDataMemPool.Alloc();
	}*/
	//m_SteamidToPlayerDataMap.SetLessFunc( SCHelpers::FUIntCmp );
	//m_entIndexToExtraDataMap.SetLessFunc( SCHelpers::FIntCmp );
}
#pragma warning( pop )

SizzlingStats::~SizzlingStats()
{
#ifndef PUBLIC_RELEASE
	delete m_pWebStatsThread;
#endif
	m_PlayerDataMemPool.Clear();
	m_ExtraDataMemPool.Clear();
}

void SizzlingStats::Load()
{
	ConVar *pVar = g_pCVar->FindVar("hostname");
	if (pVar)
	{
		V_strncpy(m_hostInfo.m_hostname, pVar->GetString(), 64);
	}
}

void SizzlingStats::Unload()
{
#ifndef PUBLIC_RELEASE
	//m_pWebStatsThread->StartThread();
	m_pWebStatsThread->Join();
#endif
}

void SizzlingStats::LevelInit(const char *pMapName)
{
	V_strncpy(m_hostInfo.m_mapname, pMapName, 64);
#ifndef PUBLIC_RELEASE
	m_pWebStatsThread->Join();
	m_pWebStatsThread->ResetSession();
#endif
}

bool SizzlingStats::SS_InsertPlayer( edict_t *pEdict )
{
	PROFILE_SCOPE( new_insert_player );
	Msg( "SS_InsertPlayer\n" );
	if ( !pEdict || pEdict->IsFree() )
	{
		SS_Msg("invalid edict, aborting insert\n");
		return false;
	}

	IPlayerInfo *pPlayerInfo = playerinfomanager->GetPlayerInfo(pEdict);
	if (pPlayerInfo)
	{
		if (!(pPlayerInfo->IsConnected()))
		{
			SS_Msg("error: player not yet connected, aborting insert\n");
		}
	}
	else
	{
		SS_Msg("error: could not get player info, aborting insert\n");
		return false;
	}

	const CSteamID *pSteamID = pEngine->GetClientSteamID(pEdict);
	if (!pSteamID)
	{
		SS_Msg("error: client %s not authenticated with steam, aborting insert\n", pPlayerInfo->GetName());
		return false;
	}

	//unsigned int accountId = pSteamID->GetAccountID();
	// check if info is in the archive (hashmap)
	// if it is, move the data to the ptr array
	// null the entry in the hashmap
	UtlHashFastHandle_t hashHandle = m_playerDataArchive.InvalidHandle();//.Find(accountId);
	// if the data is not in our archive
	if (hashHandle == m_playerDataArchive.InvalidHandle())
	{
		// for when we allocate at the beginning
		//m_pPlayerData[pEngine->IndexOfEdict(pEdict)]->SetBaseData(pEdict, pPlayerInfo);
		int entIndex = pEngine->IndexOfEdict(pEdict);
		m_pPlayerData[entIndex] = m_PlayerDataMemPool.Alloc();
		m_pPlayerData[entIndex]->SetBaseData(pEdict, pPlayerInfo);
		m_pEntIndexToExtraData[entIndex] = m_ExtraDataMemPool.Alloc();

		m_nCurrentPlayers += 1;
		SS_Msg( "current players: %i\n", m_nCurrentPlayers );
		SS_Msg( "Stats for player #%i: '%s' will be tracked\n", pSteamID->GetAccountID(), pPlayerInfo->GetName() );
	}
	else // we have an archive of the player
	{
		// if the hash handle is valid, i guess this pointer will be too
		// TODO: need to think about how to restore stats if the person is rejoining a round
		playerAndExtra &data = m_playerDataArchive.Element(hashHandle);
		m_playerDataArchive.Remove(hashHandle);
		// we need to remove the archive mempool pointers from the vector

		int entIndex = pEngine->IndexOfEdict(pEdict);
		m_pPlayerData[entIndex] = data.m_pPlayerData;
		m_pPlayerData[entIndex]->SetBaseData(pEdict, pPlayerInfo);
		m_pEntIndexToExtraData[entIndex] = data.m_pExtraData;
	}
	return true;
}

bool SizzlingStats::SS_DeletePlayer( edict_t *pEdict )
{
	Msg( "SS_DeletePlayer\n" );
	if ( !pEdict || pEdict->IsFree() )
	{
		SS_Msg("error: invalid edict, aborting delete\n");
		return false;
	}

	const CSteamID *pSteamID = pEngine->GetClientSteamID(pEdict);
	if (!pSteamID)
	{
		// TODO: verify that this can happen
		SS_Msg("error: client not authenticated with steam, aborting delete\n");
		return false;
	}

	int entIndex = pEngine->IndexOfEdict(pEdict);
	//playerAndExtra data = {m_pPlayerData[entIndex], m_pEntIndexToExtraData[entIndex]};

	// insert vs fastinsert, insert checks for duplicates
	//m_playerDataArchive.FastInsert(pSteamID->GetAccountID(), data);
	m_PlayerDataMemPool.Free(m_pPlayerData[entIndex]);
	m_pPlayerData[entIndex] = NULL;
	m_ExtraDataMemPool.Free(m_pEntIndexToExtraData[entIndex]);
	m_pEntIndexToExtraData[entIndex] = NULL;

	SS_Msg( "deleted data index #%i\n", entIndex );
	SS_Msg( "size before delete: %i\n", m_nCurrentPlayers );
	m_nCurrentPlayers -= 1;
	SS_Msg( "size after delete: %i\n", m_nCurrentPlayers );
	return true;
}

bool SizzlingStats::SS_DeleteAllPlayerData()
{
	SS_Msg( "deleting all data\n" );
	for (int i = 0; i < MAX_PLAYERS; ++i)
	{
		if (m_pPlayerData[i])
		{
			m_PlayerDataMemPool.Free(m_pPlayerData[i]);
			m_pPlayerData[i] = NULL;
			m_ExtraDataMemPool.Free(m_pEntIndexToExtraData[i]);
			m_pEntIndexToExtraData[i] = NULL;

			SS_Msg( "deleted data index #%i\n", i );
			SS_Msg( "size before delete: %i\n", m_nCurrentPlayers );
			m_nCurrentPlayers -= 1;
			SS_Msg( "size after delete: %i\n", m_nCurrentPlayers );
		}
	}
	
	m_playerDataArchive.RemoveAll();

	// clear messes everything up
	// need to reinit after a clear
	// but init is protected
	//m_PlayerDataMemPool.Clear();
	//m_ExtraDataMemPool.Clear();
	
	SS_Msg( "all data successfully cleared\n" );

	return true;
}

void SizzlingStats::SS_Msg( const char *pMsg, ... )
{
	va_list argList;
	va_start( argList, pMsg );
	char message[96];

	V_vsnprintf( message, 96, pMsg, argList );
	Msg( "[SS]: %s", message );

	va_end( argList );
}

void SizzlingStats::SS_SingleUserChatMessage( edict_t *pEntity, const char *szMessage )
{
	g_pMessage->SingleUserChatMessage( pEntity, szMessage );
}

void SizzlingStats::SS_AllUserChatMessage( const char *szMessage )
{
	g_pMessage->AllUserChatMessage( szMessage, "\x04[\x05SizzlingStats\x04]\x06: \x03" );
	//g_pMessage->AllUserChatMessage( szMessage, "\x01\\x01\x02\\x02\x03\\x03\x04\\x04\x05\\x05\x06\\x06\x07\\x07\x08\\x08\x09\\x09\n" );
}

void SizzlingStats::SS_DisplayStats( SS_PlayerData &playerData )
{
	char pText[64] = {};
	//SS_PlayerData &playerData = *pPlayerData;
	//ScoreData *pData = pPlayerData->
	//unsigned int size = sizeof(pText);
	int kills = playerData.GetStat(m_nCurrentRound, Kills);
	int assists = playerData.GetStat(m_nCurrentRound, KillAssists);
	int deaths = playerData.GetStat(m_nCurrentRound, Deaths);
	int damagedone = playerData.GetStat(m_nCurrentRound, DamageDone);
	int amounthealed = playerData.GetStat(m_nCurrentRound, HealPoints);
	int points = playerData.GetStat(m_nCurrentRound, Points);
	int backstabs = playerData.GetStat(m_nCurrentRound, Backstabs);
	int headshots = playerData.GetStat(m_nCurrentRound, Headshots);
	int captures = playerData.GetStat(m_nCurrentRound, Captures);
	int defenses = playerData.GetStat(m_nCurrentRound, Defenses);
	int healsrecv = playerData.GetStat(m_nCurrentRound, HealsReceived);
	int ubers = playerData.GetStat(m_nCurrentRound, Invulns);
	int drops = playerData.GetStat(m_nCurrentRound, UbersDropped);
	int medpicks = playerData.GetStat(m_nCurrentRound, MedPicks);
	
	edict_t *pEntity = playerData.GetEdict();

	V_snprintf( pText, 64, "\x04[\x05SizzlingStats\x04]\x06: \x03%s\n", playerData.GetPlayerInfo()->GetName() );
	SS_SingleUserChatMessage( pEntity, pText );

	memset( pText, 0, sizeof(pText) );
	if ( deaths != 0 )
		V_snprintf( pText, 64, "K/D: %i:%i (%.2f), Assists: %i\n", kills, deaths, (double)kills/(double)deaths, assists );
	else
		V_snprintf( pText, 64, "K/D: %i:%i, Assists: %i\n", kills, deaths, assists );
	SS_SingleUserChatMessage( pEntity, pText );

	memset( pText, 0, sizeof(pText) );
	//Msg( "class: %i\n", *((int *)(((unsigned char *)playerData.GetBaseEntity()) + m_PlayerClassOffset)) );
	if ( *((int *)(((unsigned char *)playerData.GetBaseEntity()) + m_PlayerClassOffset)) - 5 != 0 ) // if the player isn't a medic
	{
		V_snprintf( pText, 64, "Damage Done: %i, Heals Received (not incl. buffs): %i\n", damagedone, healsrecv );
	}
	else
	{
		V_snprintf( pText, 64, "Ubers/Kritz Used: %i, Ubers/Kritz Dropped: %i", ubers, drops );
		//SS_SingleUserChatMessage( pEntity, pText );
		//memset( pText, 0, size );
		//V_snprintf( pText, 64, "Avg Charge Time: %i seconds", chargetime );
		//SS_SingleUserChatMessage( pEntity, pText );
	}
	SS_SingleUserChatMessage( pEntity, pText );

	if ( amounthealed != 0 )
	{
		memset( pText, 0, sizeof(pText) );
		V_snprintf( pText, 64, "Amount Healed: %i\n", amounthealed );
		SS_SingleUserChatMessage( pEntity, pText );
	}

	if ( medpicks != 0 )
	{
		memset( pText, 0, sizeof(pText) );
		V_snprintf( pText, 64, "Medic Picks: %i\n", medpicks );
		SS_SingleUserChatMessage( pEntity, pText );
	}

	if ( (backstabs != 0) && (headshots != 0) )
	{
		memset( pText, 0, sizeof(pText) );
		V_snprintf( pText, 64, "Backstabs: %i, Headshots: %i\n", backstabs, headshots );
		SS_SingleUserChatMessage( pEntity, pText );
	}
	else if ( (backstabs != 0) && (headshots == 0) )
	{
		memset( pText, 0, sizeof(pText) );
		V_snprintf( pText, 64, "Backstabs: %i\n", backstabs );
		SS_SingleUserChatMessage( pEntity, pText );
	}
	else if ( (backstabs == 0) && (headshots != 0) )
	{
		memset( pText, 0, sizeof(pText) );
		V_snprintf( pText, 64, "Headshots: %i\n", headshots );
		SS_SingleUserChatMessage( pEntity, pText );
	}

	memset( pText, 0, sizeof(pText) );
	V_snprintf( pText, 64, "Captures: %i, Defenses: %i\n", captures, defenses );
	SS_SingleUserChatMessage( pEntity, pText );

	memset( pText, 0, sizeof(pText) );
	V_snprintf( pText, 64, "Round Score: %i\n", points );
	SS_SingleUserChatMessage( pEntity, pText );
}

void SizzlingStats::SS_PrintIndex()
{
	//FOR_EACH_MAP_FAST( m_SteamidToPlayerDataMap, itMapData )
	//{
	//	PlayerData *pData = m_SteamidToPlayerDataMap.Element( itMapData );
	//	Msg( "name: %s, index: %i\n", pEngine->IndexOfEdict( pServerEnts->BaseEntityToEdict( pData->GetBaseEntity() ) ) );
	//}
}

// capper is an ent index
void SizzlingStats::SS_CheckFixEndOfRoundCappers( int capper )
{
	if (m_pPlayerData[capper])
	{
		m_pPlayerData[capper]->SetDoFixTrue();
	}
}

void SizzlingStats::SS_EndOfRound()
{
	if (!V_strlen(m_hostInfo.m_bluname))
	{
		ConVar *pVar = cvar->FindVar("mp_tournament_blueteamname");
		if (pVar)
		{
			V_strncpy(m_hostInfo.m_bluname, pVar->GetString(), 32);
		}
	}
	if (!V_strlen(m_hostInfo.m_redname))
	{
		ConVar *pVar = cvar->FindVar("mp_tournament_redteamname");
		if (pVar)
		{
			V_strncpy(m_hostInfo.m_redname, pVar->GetString(), 32);
		}
	}
	if (!V_strlen(m_hostInfo.m_hostname))
	{
		ConVar *pVar = cvar->FindVar("hostname");
		if (pVar)
		{
			V_strncpy(m_hostInfo.m_hostname, pVar->GetString(), 64);
		}
	}
#ifndef PUBLIC_RELEASE
	m_pWebStatsThread->SetHostData(m_hostInfo);
#endif
	for (int i = 0; i < MAX_PLAYERS; ++i)
	{
		playerAndExtra data = {m_pPlayerData[i], m_pEntIndexToExtraData[i]};
		if (data.m_pPlayerData)
		{
			data.m_pPlayerData->UpdateRoundData(m_nCurrentRound, m_aPropOffsets); //needs to be before the updateextradata or crash cause no vector
			data.m_pPlayerData->UpdateExtraData(m_nCurrentRound, *data.m_pExtraData);

			if (data.m_pPlayerData->GetPlayerInfo()->GetTeamIndex() > 1)
			{
				//V_snprintf(m_postStrings[Data.GetEntIndex()], 256, "name=%s&steamid=%s", Data.GetPlayerInfo()->GetName(), Data.GetPlayerInfo()->GetNetworkIDString());
				//g_pThreadPool->AddCall(SS_SendHttpPostData, (const char *)temp);
#ifndef PUBLIC_RELEASE
				playerWebStats_t stats;
				stats.m_scoreData = data.m_pPlayerData->GetScoreData(m_nCurrentRound);
				V_strncpy(stats.m_playerInfo.m_name, data.m_pPlayerData->GetPlayerInfo()->GetName(), 32);
				//stats.m_playerInfo.m_steamid = data.m_pPlayerData->
				V_strncpy(stats.m_playerInfo.m_steamid, data.m_pPlayerData->GetPlayerInfo()->GetNetworkIDString(), 32);
				stats.m_playerInfo.m_teamid = data.m_pPlayerData->GetPlayerInfo()->GetTeamIndex();

				m_pWebStatsThread->EnqueueItem(stats);
#endif
				SS_DisplayStats( *data.m_pPlayerData );
			}
			data.m_pPlayerData->UpdateTotalData( m_nCurrentRound );
			data.m_pPlayerData->ResetExtraData( m_nCurrentRound );
		}
	}
#ifndef PUBLIC_RELEASE
	m_pWebStatsThread->StartThread();
#endif
}

void SizzlingStats::SS_ResetData()
{
	for (int i = 0; i < MAX_PLAYERS; ++i)
	{
		if (m_pPlayerData[i])
		{
			m_pPlayerData[i]->ResetExtraData(m_nCurrentRound);
			m_pEntIndexToExtraData[i]->operator=(0);
		}
	}
}

void SizzlingStats::SS_Credits( int entindex, const char *pszVersion )
{
	char version[32];
	V_snprintf( version, 32, "SizzlingStats v%s\n", pszVersion );
	g_pMessage->SingleUserChatMessage( entindex, "-----------------------\n" );
	g_pMessage->SingleUserChatMessage( entindex, version );
	g_pMessage->SingleUserChatMessage( entindex, "Credits go to:\n" );
	g_pMessage->SingleUserChatMessage( entindex, "SizzlingCalamari for creation and development.\n" );
	g_pMessage->SingleUserChatMessage( entindex, "Drunken F00l for his insight on coding with the Source Engine.\n" );
	g_pMessage->SingleUserChatMessage( entindex, "Whal3r for his endless testing whenever I called him in.\n" );
	g_pMessage->SingleUserChatMessage( entindex, "-----------------------\n" );
}

#ifndef PUBLIC_RELEASE

static void messagethis()
{
	g_pMessage->AllUserChatMessage("thread completed execution\n");
}

static void messagemain()
{
	g_pMessage->AllUserChatMessage("message from main\n");
}

static void messagethread()
{
	g_pMessage->AllUserChatMessage("message from thread\n");
}

static void threadstuff()
{
	CUtlBuffer html;
	CHtmlGenerator htmlGen(html);
	htmlGen.StartFile();
	htmlGen.StartHead();
	htmlGen.EndHead();
	htmlGen.StartBody();
	htmlGen.WriteToBuffer("this is a test2\n");
	htmlGen.EndBody();
	htmlGen.EndFile();
	char temp[256];
	V_snprintf(temp, sizeof(temp), "ftp://%s:%s@%s/public/sizzlingstats/asdf.html.uploading", ftp_user.GetString(), ftp_pass.GetString(), ftp_server.GetString());
	CFtpUploader uploader(temp);
	uploader.UploadFile(html);
	Msg("size of html buff: %i\n", html.GetBytesRemaining());
	CFunctor *pFunctor = CreateFunctor(messagethis);
	// do i need to delete functors?
	// it looks like they are reference counted... so no, but not sure.....
	g_pTSCallQueue->EnqueueFunctor( pFunctor );
}

static void loopmessage()
{
	for (int i = 0; i < 100; ++i)
	{
		g_pTSCallQueue->EnqueueFunctor(CreateFunctor(messagethread));
	}
}

#endif

void SizzlingStats::SetTeamScores( int redscore, int bluscore )
{
	m_hostInfo.m_redscore = redscore;
	m_hostInfo.m_bluscore = bluscore;
}

void SizzlingStats::TeamNameChange( int entIndex, const char *teamname )
{
	int teamindex = m_pPlayerData[entIndex]->GetPlayerInfo()->GetTeamIndex();
	if (teamindex == 2)
	{
		V_strncpy(m_hostInfo.m_redname, teamname, 32);
	}
	else
	{
		V_strncpy(m_hostInfo.m_bluname, teamname, 32);
	}
}

#ifndef PUBLIC_RELEASE

static size_t read_callback(void *ptr, size_t size, size_t nmemb, void *userdata)
{
	CUtlBuffer *pBuffer = static_cast<CUtlBuffer*>(userdata);
	const int maxSize = size*nmemb;
	if ( pBuffer->GetBytesRemaining() >= maxSize )
	{
		pBuffer->Get( ptr, maxSize );
		return maxSize;
	}
	else
	{
		const int bytesRemaining = pBuffer->GetBytesRemaining();
		pBuffer->Get( ptr, bytesRemaining );
		return bytesRemaining;
	}
}

void SS_SendHttpPostData()
{
	CURL *curl;
	CURLcode res;

	curl = curl_easy_init();
	if(curl) {
		/* First set the URL that is about to receive our POST. This URL can
		just as well be a https:// URL if that is what should receive the
		data. */ 
		curl_easy_setopt(curl, CURLOPT_URL, "204.12.223.55:1337");
		/* Now specify the POST data */ 
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "matchid=0&name=SizzlingStats&steamid=123456789&kills=999&deaths=0");

		/* Perform the request, res will get the return code */ 
		res = curl_easy_perform(curl);

		/* always cleanup */ 
		curl_easy_cleanup(curl);
	}
}

void SizzlingStats::SS_TestPost()
{
	for (int i = 0; i < MAX_PLAYERS; ++i)
	{
		if (m_pPlayerData[i])
		{
			SS_PlayerData &Data = *m_pPlayerData[i];
			if ( Data.GetPlayerInfo()->GetTeamIndex() > 1 )	// don't send stats messages to spectators
			{
				char temp[128];
				V_snprintf(temp, 128, "name=%s&steamid=%s", Data.GetPlayerInfo()->GetName(), Data.GetPlayerInfo()->GetNetworkIDString());
				//g_pThreadPool->AddCall(SS_SendHttpPostData, (const char *)temp);
				//SS_SendHttpPostData(temp);
				g_pMessage->AllUserChatMessage(temp);
			}
		}
	}
	g_pThreadPool->AddCall(&SS_SendHttpPostData);
}

void SizzlingStats::SS_TestThreading()
{
	g_pThreadPool->AddCall(loopmessage);
	for (int i = 0; i < 100; ++i)
	{
		g_pTSCallQueue->EnqueueFunctor(CreateFunctor(messagemain));
	}
}

void SizzlingStats::SS_UploadStats()
{
	g_pThreadPool->AddCall(threadstuff);
}

void SizzlingStats::SS_ShowHtmlStats( int entindex )
{
	char temp[128];
	//V_snprintf(temp, 256, "%s/sizzlingstats/asdf.html", web_hostname.GetString());
	if (m_pWebStatsThread->HasMatchUrl())
	{
		m_pWebStatsThread->GetMatchUrl(temp, 128);
		g_pMessage->SingleUserVGUIMenu( entindex, "testasdf", temp );
	}
}

#endif

void SizzlingStats::SS_GameOver()
{
#ifndef PUBLIC_RELEASE
	m_pWebStatsThread->ResetSession();
#endif
}

void SizzlingStats::GetPropOffsets()
{
//DT_TFPlayerScoringDataExclusive
//	4, Captures				0
//	8, Defenses				1
//	12, Kills				2
//	16, Deaths				3
//	20, Suicides			4
//	24, Dominations			5
//	28, Revenge				6
//	32, BuildingsBuilt		7
//	36, BuildingsDestroyed	8
//	40, Headshots			9
//	44, Backstabs			10
//	48, HealPoints			11
//	52, Invulns				12
//	56, Teleports			13
//	60, DamageDone			14
//	64, Crits				15
//	68, ResupplyPoints		16
//	72, KillAssists			17
//	76, BonusPoints			18
//	80, Points				19
	using namespace SCHelpers;
	bool bError = false;
	int iTFPlayerScoreingDataExclusiveOffset = GetPropOffsetFromTable( "DT_TFPlayer", "m_Shared", bError ) 
											+ GetPropOffsetFromTable( "DT_TFPlayerShared", "tfsharedlocaldata", bError )
											+ GetPropOffsetFromTable( "DT_TFPlayerSharedLocal", "m_RoundScoreData", bError );

	m_aPropOffsets[0] = iTFPlayerScoreingDataExclusiveOffset + GetPropOffsetFromTable( "DT_TFPlayerScoringDataExclusive", "m_iCaptures", bError );
	m_aPropOffsets[1] = iTFPlayerScoreingDataExclusiveOffset + GetPropOffsetFromTable( "DT_TFPlayerScoringDataExclusive", "m_iDefenses", bError );
	m_aPropOffsets[2] = iTFPlayerScoreingDataExclusiveOffset + GetPropOffsetFromTable( "DT_TFPlayerScoringDataExclusive", "m_iKills", bError );
	m_aPropOffsets[3] = iTFPlayerScoreingDataExclusiveOffset + GetPropOffsetFromTable( "DT_TFPlayerScoringDataExclusive", "m_iDeaths", bError );
	m_aPropOffsets[4] = iTFPlayerScoreingDataExclusiveOffset + GetPropOffsetFromTable( "DT_TFPlayerScoringDataExclusive", "m_iSuicides", bError );
	m_aPropOffsets[5] = iTFPlayerScoreingDataExclusiveOffset + GetPropOffsetFromTable( "DT_TFPlayerScoringDataExclusive", "m_iDominations", bError );
	m_aPropOffsets[6] = iTFPlayerScoreingDataExclusiveOffset + GetPropOffsetFromTable( "DT_TFPlayerScoringDataExclusive", "m_iRevenge", bError );
	m_aPropOffsets[7] = iTFPlayerScoreingDataExclusiveOffset + GetPropOffsetFromTable( "DT_TFPlayerScoringDataExclusive", "m_iBuildingsBuilt", bError );
	m_aPropOffsets[8] = iTFPlayerScoreingDataExclusiveOffset + GetPropOffsetFromTable( "DT_TFPlayerScoringDataExclusive", "m_iBuildingsDestroyed", bError );
	m_aPropOffsets[9] = iTFPlayerScoreingDataExclusiveOffset + GetPropOffsetFromTable( "DT_TFPlayerScoringDataExclusive", "m_iHeadshots", bError );
	m_aPropOffsets[10] = iTFPlayerScoreingDataExclusiveOffset + GetPropOffsetFromTable( "DT_TFPlayerScoringDataExclusive", "m_iBackstabs", bError );
	m_aPropOffsets[11] = iTFPlayerScoreingDataExclusiveOffset + GetPropOffsetFromTable( "DT_TFPlayerScoringDataExclusive", "m_iHealPoints", bError );
	m_aPropOffsets[12] = iTFPlayerScoreingDataExclusiveOffset + GetPropOffsetFromTable( "DT_TFPlayerScoringDataExclusive", "m_iInvulns", bError );
	m_aPropOffsets[13] = iTFPlayerScoreingDataExclusiveOffset + GetPropOffsetFromTable( "DT_TFPlayerScoringDataExclusive", "m_iTeleports", bError );
	m_aPropOffsets[14] = iTFPlayerScoreingDataExclusiveOffset + GetPropOffsetFromTable( "DT_TFPlayerScoringDataExclusive", "m_iDamageDone", bError );
	m_aPropOffsets[15] = iTFPlayerScoreingDataExclusiveOffset + GetPropOffsetFromTable( "DT_TFPlayerScoringDataExclusive", "m_iCrits", bError );
	m_aPropOffsets[16] = iTFPlayerScoreingDataExclusiveOffset + GetPropOffsetFromTable( "DT_TFPlayerScoringDataExclusive", "m_iResupplyPoints", bError );
	m_aPropOffsets[17] = iTFPlayerScoreingDataExclusiveOffset + GetPropOffsetFromTable( "DT_TFPlayerScoringDataExclusive", "m_iKillAssists", bError );
	m_aPropOffsets[18] = iTFPlayerScoreingDataExclusiveOffset + GetPropOffsetFromTable( "DT_TFPlayerScoringDataExclusive", "m_iBonusPoints", bError );
	m_aPropOffsets[19] = iTFPlayerScoreingDataExclusiveOffset + GetPropOffsetFromTable( "DT_TFPlayerScoringDataExclusive", "m_iPoints", bError );

	m_PlayerClassOffset = GetPropOffsetFromTable( "DT_TFPlayer", "m_PlayerClass", bError ) + GetPropOffsetFromTable( "DT_TFPlayerClassShared", "m_iClass", bError );
	m_PlayerFlagsOffset = GetPropOffsetFromTable( "DT_BasePlayer", "m_fFlags", bError );
	m_TeamRoundsWonOffset = GetPropOffsetFromTable( "DT_Team", "m_iRoundsWon", bError ); 

	//oKills = m_PlayerFlagsOffset;

	//CBaseEntity *pEntity = gEntList.FindEntityByClassname( pServerEnts->EdictToBaseEntity( engine->PEntityOfEntIndex( gpGlobals->maxClients ) ), "CWeaponIFMSteadyCam" );
}