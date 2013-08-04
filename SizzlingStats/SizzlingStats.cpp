
/*========
        This file is part of SizzlingPlugins.

    Copyright (c) 2010-2013, Jordan Cristiano.
    This file is subject to the terms and conditions 
    defined in the file 'LICENSE', which is part of this
    source code package.
    
*/

////////////////////////////////////////////////////////////////////////////////
// Filename: SizzlingStats.cpp
////////////////////////////////////////////////////////////////////////////////
#include "SizzlingStats.h"
#include "SC_helpers.h"
#include "functors.h"
#include "engine/IEngineTrace.h"
#include "eiface.h"
#include "game/server/iplayerinfo.h"
#include "SizzPluginContext.h"
#include "SRecipientFilter.h"
#include "UserMessageHelpers.h"
#include "SSPlayerData.h"

#include <functional>

#ifdef FTP_STATS
#include "vstdlib/jobthread.h"
#include "HtmlGenerator.h"
#include "FtpUploader.h"
#include "utlbuffer.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern IEngineTrace		*enginetrace;

#ifdef FTP_STATS

static ConVar ftp_user("sizz_stats_ftp_username", "username", FCVAR_DONTRECORD | FCVAR_PROTECTED, "FTP username to connect with when using web stats.");
static ConVar ftp_pass("sizz_stats_ftp_password", "password", FCVAR_DONTRECORD | FCVAR_PROTECTED, "FTP password to connect with when using web stats.");
static ConVar ftp_server("sizz_stats_ftp_hostname", "ftp.myserver.com", FCVAR_DONTRECORD | FCVAR_PROTECTED, "FTP hostname to connect to when using web stats.");
static ConVar ftp_port("sizz_stats_ftp_port", "21", FCVAR_DONTRECORD | FCVAR_PROTECTED, "FTP port to use when using web stats.");
static ConVar web_hostname("sizz_stats_web_hostname", "myserver.com", FCVAR_DONTRECORD | FCVAR_PROTECTED, "Hostname of the external public directory of the FTP server.");

#endif

// hidden convar apikey
// set by a config on the server
#define HIDDEN_CVAR_FLAGS FCVAR_HIDDEN | FCVAR_PROTECTED | FCVAR_UNLOGGED

static ConVar apikey("sizz_stats_web_api_key", "", HIDDEN_CVAR_FLAGS, "");
static ConVar show_msg("sizz_stats_show_chat_messages", "0", FCVAR_NONE, "If nonzero, shows chat messages by the plugin");

#pragma warning( push )
#pragma warning( disable : 4351 )
SizzlingStats::SizzlingStats():
	m_plugin_context(nullptr),
	m_aPropOffsets(),
	m_PlayerFlagsOffset(0), 
	m_TeamRoundsWonOffset(0),
	m_PlayerClassOffset(0),
	m_iWeaponsOffset(0),
	m_iChargeLevelOffset(0),
	m_iOriginOffset(0),
	m_iChargeReleaseOffset(0),
	m_pRedTeam(NULL),
	m_pBluTeam(NULL),
	m_iTeamScoreOffset(0),
	m_iTeamNumOffset(0),
	m_iOldRedScore(0),
	m_iOldBluScore(0),
	m_PlayerDataManager(),
	m_pWebStatsHandler(NULL),
	m_refHostIP((IConVar*)NULL),
	m_refIP((IConVar*)NULL),
	m_refHostPort((IConVar*)NULL),
	m_hostInfo(),
	m_flRoundDuration(0),
	m_flMatchDuration(0),
	m_bTournamentMatchRunning(false),
	m_bFirstCapOfRound(false)
{
	m_pWebStatsHandler = new CWebStatsHandler();
}
#pragma warning( pop )

SizzlingStats::~SizzlingStats()
{
	delete m_pWebStatsHandler;
}

void SizzlingStats::Load( CSizzPluginContext *context )
{
	m_plugin_context = context;

    GetPropOffsets();
	GetEntities();
	m_refHostIP.Init("hostip", false);
	m_refIP.Init("ip", false);
	m_refHostPort.Init("hostport", false);
	LoadConfig();
	m_pWebStatsHandler->Initialize();

	using namespace std::placeholders;
	m_pWebStatsHandler->SetReceiveSessionIdCallback(std::bind(&SizzlingStats::OnSessionIdReceived, this, std::bind(sizz::move<sizz::CString>, _1)));
	m_pWebStatsHandler->SetReceiveMatchUrlCallback(std::bind(&SizzlingStats::OnMatchUrlReceived, this, std::bind(sizz::move<sizz::CString>, _1)));
}

void SizzlingStats::Unload()
{
	m_pWebStatsHandler->Shutdown();
}

void SizzlingStats::LevelInit(const char *pMapName)
{
	LoadConfig();
}

void SizzlingStats::ServerActivate()
{
	GetEntities();
}

void SizzlingStats::GameFrame()
{
}

void SizzlingStats::LoadConfig()
{
	m_plugin_context->ServerCommand( "exec " PLUGIN_CONFIG_FILE "\n" );
}

void SizzlingStats::PlayerHealed( int entindex, int amount )
{
	playerAndExtra_t data = m_PlayerDataManager.GetPlayerData(entindex);
	data.m_pExtraData->healsrecv += amount;
}

void SizzlingStats::MedPick( int entindex )
{
	playerAndExtra_t data = m_PlayerDataManager.GetPlayerData(entindex);
	data.m_pExtraData->medpicks += 1;
}

void SizzlingStats::UberDropped( int entindex )
{
	playerAndExtra_t data = m_PlayerDataManager.GetPlayerData(entindex);
	data.m_pExtraData->ubersdropped += 1;
}

void SizzlingStats::PlayerChangedClass( int entindex, EPlayerClass player_class )
{
	if (player_class != k_ePlayerClassMedic)
	{
		m_vecMedics.FindAndRemove(entindex);
	}
	else
	{
		m_vecMedics.AddToTail(entindex);
	}

	m_PlayerDataManager.PlayerChangedClass( entindex, player_class, SCHelpers::RoundDBL(Plat_FloatTime()) );
}

void SizzlingStats::ChatEvent( int entindex, const char *pText, bool bTeamChat )
{
	// this needs to be checked carefully for 
	// the error that happened below before any 
	// null check optimizations can be made
	SS_PlayerData *player_data = m_PlayerDataManager.GetPlayerData(entindex).m_pPlayerData;
	if (player_data)
	{
		IPlayerInfo *pInfo = m_plugin_context->GetPlayerInfo(entindex);
		const char *pSteamId = pInfo->GetNetworkIDString();
		// during the match, m_flMatchDuration is the Plat_FloatTime() from when the game started
		// so subtracting gets the time since the match started
		m_pWebStatsHandler->PlayerChatEvent(Plat_FloatTime() - m_flMatchDuration, pSteamId, pText, bTeamChat);
	}
	else
	{
		// TODO: error
		// this happened once somehow
	}
}

void SizzlingStats::TeamCapped( int team_index )
{
	if (m_bTournamentMatchRunning && m_bFirstCapOfRound)
	{
		m_bFirstCapOfRound = false;
		m_hostInfo.m_iFirstCapTeamIndex = team_index;
	}
}

class CTraceFilterSkipTwo: public ITraceFilter
{
public:
	CTraceFilterSkipTwo( IHandleEntity *pEnt1, IHandleEntity *pEnt2 ):
		m_pEnt1(pEnt1),
		m_pEnt2(pEnt2)
	{
	}

	virtual bool ShouldHitEntity( IHandleEntity *pServerEntity, int contentsMask )
	{
		if (!pServerEntity || (pServerEntity == m_pEnt1) || (pServerEntity == m_pEnt2))
		{
			return false;
		}
		return true;
	}

	virtual TraceType_t	GetTraceType() const
	{
		return TRACE_EVERYTHING_FILTER_PROPS;
	}

private:
	IHandleEntity *m_pEnt1;
	IHandleEntity *m_pEnt2;
};

static char *UTIL_VarArgs( char *format, ... )
{
    va_list     argptr;
    static char     string[1024];
    
    va_start (argptr, format);
    Q_vsnprintf(string, sizeof(string), format,argptr);
    va_end (argptr);

    return string;  
}

void SizzlingStats::GiveUber( int entindex )
{
	SS_PlayerData *pData = m_PlayerDataManager.GetPlayerData(entindex).m_pPlayerData;
	if (pData && (pData->GetClass(m_PlayerClassOffset) == 5))
	{
		CBaseEntity *pPlayer = m_plugin_context->BaseEntityFromEntIndex(entindex);
		// +4 because we want the medigun slot
		CBaseHandle *hMedigun = SCHelpers::ByteOffsetFromPointer<CBaseHandle*>(pPlayer, m_iWeaponsOffset + 4);
		CBaseEntity *pMedigun = m_plugin_context->BaseEntityFromBaseHandle(hMedigun);

		if (pMedigun)
		{
			float *flChargeLevel = SCHelpers::ByteOffsetFromPointer<float*>(pMedigun, m_iChargeLevelOffset);
			*flChargeLevel = 1.0f;
		}
	}
}

void SizzlingStats::CheckPlayerDropped( int victimIndex )
{
	for (int i = 0; i < m_vecMedics.Count(); ++i)
	{
		int medIndex = m_vecMedics[i];
		SS_PlayerData *pMedData = m_PlayerDataManager.GetPlayerData(medIndex).m_pPlayerData;
		SS_PlayerData *pVictimData = m_PlayerDataManager.GetPlayerData(victimIndex).m_pPlayerData;

		IPlayerInfo *pMedPlayerInfo = m_plugin_context->GetPlayerInfo(medIndex);
		IPlayerInfo *pVictimPlayerInfo = m_plugin_context->GetPlayerInfo(victimIndex);
		if ( pMedPlayerInfo->GetTeamIndex() == pVictimPlayerInfo->GetTeamIndex() )
		{
			using namespace SCHelpers;
			CBaseHandle *hMedigun = ByteOffsetFromPointer<CBaseHandle*>(pMedData->GetBaseEntity(), m_iWeaponsOffset+4); // +4 because we want the medigun slot
			CBaseEntity *pMedigun = m_plugin_context->BaseEntityFromBaseHandle(hMedigun);

			const char *szWeapon = SCHelpers::GetClassName(pMedigun);
			if ( szWeapon && SCHelpers::FStrEq(szWeapon, "tf_weapon_medigun") )
			{
				float flChargeLevel = *ByteOffsetFromPointer<float*>(pMedigun, m_iChargeLevelOffset);
				uint32 charge = static_cast<uint32>(flChargeLevel);

				bool bReleasingCharge = *ByteOffsetFromPointer<bool*>(pMedigun, m_iChargeReleaseOffset);

				if (charge == 1 || bReleasingCharge)
				{
					Vector *victimPos = ByteOffsetFromPointer<Vector*>(pVictimData->GetBaseEntity(), m_iOriginOffset);
					Vector *medPos = ByteOffsetFromPointer<Vector*>(pMedData->GetBaseEntity(), m_iOriginOffset);
		
					vec_t distance = victimPos->DistToSqr( *medPos );
					SS_AllUserChatMessage( UTIL_VarArgs("distance: %.2f\n", distance) );
					if (static_cast<uint32>(distance) <= 230400) // ~480 units is max target distance for medigun
					{
						Ray_t ray;
						ray.Init(*medPos, *victimPos);
						IHandleEntity *pMedHandleEnt = m_plugin_context->HandleEntityFromEntIndex(medIndex);
						IHandleEntity *pMedigunHandleEnt = m_plugin_context->HandleEntityFromEntIndex(hMedigun->GetEntryIndex());
						CTraceFilterSkipTwo traceFilter(pMedHandleEnt, pMedigunHandleEnt);
						trace_t trace;
						enginetrace->TraceRay(ray, MASK_SHOT_HULL, &traceFilter, &trace);
						if (!trace.DidHit())
						{
							SS_AllUserChatMessage( "player dropped\n" );
						}
					}
				}
			}
		}
	}
}

void SizzlingStats::CapFix( const char *cappers, int length )
{
	for (int i = 0; i < length; ++i)
	{
		int index = cappers[i];
		m_PlayerDataManager.SetCapFix(index);
	}
}

bool SizzlingStats::SS_InsertPlayer( edict_t *pEdict )
{
	Msg( "SS_InsertPlayer\n" );
	int ent_index = SCHelpers::EntIndexFromEdict(pEdict);
	if (ent_index != -1)
	{
		IPlayerInfo *pPlayerInfo = m_plugin_context->GetPlayerInfo(ent_index);
		if (pPlayerInfo && pPlayerInfo->IsConnected())
		{
			CBaseEntity *pEnt = SCHelpers::EdictToBaseEntity(pEdict);
			bool ret = m_PlayerDataManager.InsertPlayer(ent_index, pEnt);
			if (ret)
			{
				int acc_id = m_plugin_context->SteamIDFromEntIndex(ent_index);
				SS_Msg("Stats for player #%i: '%s' will be tracked\n", acc_id, pPlayerInfo->GetName());
			}
			return ret;
		}
	}
	return false;
}

void SizzlingStats::SS_DeletePlayer( edict_t *pEdict )
{
	Msg( "SS_DeletePlayer\n" );
	int ent_index = SCHelpers::EntIndexFromEdict(pEdict);
	if (ent_index != -1)
	{
		m_vecMedics.FindAndRemove(ent_index);
		IPlayerInfo *pPlayerInfo = m_plugin_context->GetPlayerInfo(ent_index);
		if (pPlayerInfo && pPlayerInfo->IsConnected())
		{
			unsigned int acc_id = m_plugin_context->SteamIDFromEntIndex(ent_index);
			m_PlayerDataManager.RemovePlayer(ent_index, pPlayerInfo, acc_id);
		}
	}
}

void SizzlingStats::SS_DeleteAllPlayerData()
{
	SS_Msg( "deleting all data\n" );
	m_vecMedics.RemoveAll();
	m_PlayerDataManager.RemoveAllPlayers();
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

void SizzlingStats::SS_SingleUserChatMessage( int ent_index, const char *szMessage )
{
	if (show_msg.GetInt() != 0)
	{
		CUserMessageHelpers h(m_plugin_context);
		h.SingleUserChatMessage(ent_index, szMessage);
	}
}

void SizzlingStats::SS_AllUserChatMessage( const char *szMessage )
{
	if (show_msg.GetInt() != 0)
	{
		CUserMessageHelpers h(m_plugin_context);
		h.AllUserChatMessage("%s%s", "\x04[\x05SizzlingStats\x04]\x06: \x03", szMessage);
	}
	//g_pMessage->AllUserChatMessage( szMessage, "\x01\\x01\x02\\x02\x03\\x03\x04\\x04\x05\\x05\x06\\x06\x07\\x07\x08\\x08\x09\\x09\n" );
}

void SizzlingStats::SS_TournamentMatchStarted( const char *RESTRICT hostname, 
											  const char *RESTRICT mapname, 
											  const char *RESTRICT bluname, 
											  const char *RESTRICT redname )
{
	Msg( "tournament match started\n" );
	m_bTournamentMatchRunning = true;
	m_flMatchDuration = Plat_FloatTime();

	V_strncpy(m_hostInfo.m_hostname, hostname, 64);
	V_strncpy(m_hostInfo.m_mapname, mapname, 64);
	V_strncpy(m_hostInfo.m_bluname, bluname, 32);
	V_strncpy(m_hostInfo.m_redname, redname, 32);
	m_hostInfo.m_hostip = m_refHostIP.GetInt();
	V_strncpy(m_hostInfo.m_ip, m_refIP.GetString(), 32);
	m_hostInfo.m_hostport = m_refHostPort.GetInt();
	m_hostInfo.m_roundduration = m_flRoundDuration;
	m_pWebStatsHandler->SetHostData(m_hostInfo);

	for (int i = 1; i <= MAX_PLAYERS; ++i)
	{
		playerAndExtra_t data = m_PlayerDataManager.GetPlayerData(i);
		if (data.m_pPlayerData)
		{
			IPlayerInfo *pInfo = m_plugin_context->GetPlayerInfo(i);
			playerInfo_t info;
			V_strncpy(info.m_name, pInfo->GetName(), 32);
			V_strncpy(info.m_steamid, pInfo->GetNetworkIDString(), 32);
			info.m_teamid = pInfo->GetTeamIndex();
			info.m_mostPlayedClass = data.m_pPlayerData->GetClass(m_PlayerClassOffset);
			m_pWebStatsHandler->EnqueuePlayerInfo(info);
		}
	}

	// set the api key
	m_pWebStatsHandler->SetApiKey(apikey.GetString());

	// send the initial match info to the web
	m_pWebStatsHandler->SendGameStartEvent();
}

void SizzlingStats::SS_TournamentMatchEnded()
{
	Msg( "tournament match ended\n" );
	m_bTournamentMatchRunning = false;
	m_flMatchDuration = Plat_FloatTime() - m_flMatchDuration;
	m_pWebStatsHandler->SendGameOverEvent(m_flMatchDuration);
	//SetTeamScores(0, 0);
}

void SizzlingStats::SS_PreRoundFreeze()
{
	Msg( "pre-round started\n" );
	SS_ResetData();
	double curtime = Plat_FloatTime();
	m_flRoundDuration = curtime;
	m_PlayerDataManager.ResetAndStartClassTracking(m_PlayerClassOffset, SCHelpers::RoundDBL(curtime));
}

void SizzlingStats::SS_RoundStarted()
{
	Msg( "round started\n" );
	m_iOldBluScore = *SCHelpers::ByteOffsetFromPointer<uint32*>(m_pBluTeam, m_iTeamScoreOffset);
	m_iOldRedScore = *SCHelpers::ByteOffsetFromPointer<uint32*>(m_pRedTeam, m_iTeamScoreOffset);
	m_bFirstCapOfRound = true;
	m_hostInfo.m_iFirstCapTeamIndex = 0;
	SS_AllUserChatMessage( "Stats Recording Started\n" );
}

void SizzlingStats::SS_RoundEnded()
{
	Msg( "round ended\n" );
	double curtime = Plat_FloatTime();
	m_flRoundDuration = curtime - m_flRoundDuration;
	m_PlayerDataManager.StopClassTracking(SCHelpers::RoundDBL(curtime));
	SS_AllUserChatMessage( "Stats Recording Stopped\n" );
	SS_EndOfRound();
	m_PlayerDataManager.RemoveArchivedPlayers();
}

void SizzlingStats::SS_DisplayStats( int ent_index )
{
	char pText[64] = {};
	SS_PlayerData &playerData = *m_PlayerDataManager.GetPlayerData(ent_index).m_pPlayerData;
	int kills = playerData.GetStat(Kills);
	int assists = playerData.GetStat(KillAssists);
	int deaths = playerData.GetStat(Deaths);
	int damagedone = playerData.GetStat(DamageDone);
	int amounthealed = playerData.GetStat(HealPoints);
	int points = playerData.GetStat(Points);
	int backstabs = playerData.GetStat(Backstabs);
	int headshots = playerData.GetStat(Headshots);
	int captures = playerData.GetStat(Captures);
	int defenses = playerData.GetStat(Defenses);
	int healsrecv = playerData.GetStat(HealsReceived);
	int ubers = playerData.GetStat(Invulns);
	int drops = playerData.GetStat(UbersDropped);
	int medpicks = playerData.GetStat(MedPicks);
	
	IPlayerInfo *pPlayerInfo = m_plugin_context->GetPlayerInfo(ent_index);

	V_snprintf( pText, 64, "\x04[\x05SizzlingStats\x04]\x06: \x03%s\n", pPlayerInfo->GetName() );
	SS_SingleUserChatMessage(ent_index, pText);

	memset( pText, 0, sizeof(pText) );
	if ( deaths != 0 )
		V_snprintf( pText, 64, "K/D: %i:%i (%.2f), Assists: %i\n", kills, deaths, (double)kills/(double)deaths, assists );
	else
		V_snprintf( pText, 64, "K/D: %i:%i, Assists: %i\n", kills, deaths, assists );
	SS_SingleUserChatMessage(ent_index, pText);

	memset( pText, 0, sizeof(pText) );
	//Msg( "class: %i\n", *((int *)(((unsigned char *)playerData.GetBaseEntity()) + m_PlayerClassOffset)) );
	if ( (playerData.GetClass(m_PlayerClassOffset) - 5) != 0 ) // if the player isn't a medic
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
	SS_SingleUserChatMessage(ent_index, pText);

	if ( amounthealed != 0 )
	{
		memset( pText, 0, sizeof(pText) );
		V_snprintf( pText, 64, "Amount Healed: %i\n", amounthealed );
		SS_SingleUserChatMessage(ent_index, pText);
	}

	if ( medpicks != 0 )
	{
		memset( pText, 0, sizeof(pText) );
		V_snprintf( pText, 64, "Medic Picks: %i\n", medpicks );
		SS_SingleUserChatMessage(ent_index, pText);
	}

	if ( (backstabs != 0) && (headshots != 0) )
	{
		memset( pText, 0, sizeof(pText) );
		V_snprintf( pText, 64, "Backstabs: %i, Headshots: %i\n", backstabs, headshots );
		SS_SingleUserChatMessage(ent_index, pText);
	}
	else if ( (backstabs != 0) && (headshots == 0) )
	{
		memset( pText, 0, sizeof(pText) );
		V_snprintf( pText, 64, "Backstabs: %i\n", backstabs );
		SS_SingleUserChatMessage(ent_index, pText);
	}
	else if ( (backstabs == 0) && (headshots != 0) )
	{
		memset( pText, 0, sizeof(pText) );
		V_snprintf( pText, 64, "Headshots: %i\n", headshots );
		SS_SingleUserChatMessage(ent_index, pText);
	}

	memset( pText, 0, sizeof(pText) );
	V_snprintf( pText, 64, "Captures: %i, Defenses: %i\n", captures, defenses );
	SS_SingleUserChatMessage(ent_index, pText);

	memset( pText, 0, sizeof(pText) );
	V_snprintf( pText, 64, "Round Score: %i\n", points );
	SS_SingleUserChatMessage(ent_index, pText);
}

void SizzlingStats::SS_EndOfRound()
{
	for (int i = 1; i <= MAX_PLAYERS; ++i)
	{
		playerAndExtra_t data = m_PlayerDataManager.GetPlayerData(i);
		if (data.m_pPlayerData)
		{
			// UpdateRoundData needs to be before the UpdateExtraData or crash cause no vector
			data.m_pPlayerData->UpdateRoundStatsData(m_aPropOffsets);
			data.m_pPlayerData->UpdateRoundExtraData(*data.m_pExtraData);

			IPlayerInfo *pInfo = m_plugin_context->GetPlayerInfo(i);

			if (m_bTournamentMatchRunning)
			{
				playerWebStats_t stats;
				stats.m_scoreData = data.m_pPlayerData->GetRoundStatsData();
				V_strncpy(stats.m_playerInfo.m_name, pInfo->GetName(), 32);
				V_strncpy(stats.m_playerInfo.m_steamid, pInfo->GetNetworkIDString(), 32);
				stats.m_playerInfo.m_teamid = pInfo->GetTeamIndex();
				CPlayerClassTracker *pTracker = data.m_pPlayerData->GetClassTracker();
				stats.m_playerInfo.m_mostPlayedClass = pTracker->GetMostPlayedClass();
				stats.m_playerInfo.m_playedClasses = pTracker->GetPlayedClasses();
				m_pWebStatsHandler->EnqueuePlayerStats(stats);
			}

			if (pInfo->GetTeamIndex() > 1)
			{
				SS_DisplayStats(i);
			}
		}
	}

	if (m_bTournamentMatchRunning)
	{
		m_hostInfo.m_roundduration = m_flRoundDuration;
		m_pWebStatsHandler->SetHostData(m_hostInfo);
		m_pWebStatsHandler->SendStatsToWeb();
	}
}

void SizzlingStats::SS_ResetData()
{
	for (int i = 1; i <= MAX_PLAYERS; ++i)
	{
		playerAndExtra_t data = m_PlayerDataManager.GetPlayerData(i);
		if (data.m_pPlayerData)
		{
			data.m_pExtraData->operator=(0);
		}
	}
}

void SizzlingStats::SS_Credits( int entindex, const char *pszVersion )
{
	char version[32] = {};
	V_snprintf( version, 32, "\x03SizzlingStats v%s\n", pszVersion );
	CUserMessageHelpers h(m_plugin_context);
	h.SingleUserChatMessage(entindex, "========================\n");
	h.SingleUserChatMessage(entindex, version);
	h.SingleUserChatMessage(entindex, "\x03\x42y:\n");
	h.SingleUserChatMessage(entindex, "\x03\tSizzlingCalamari\n");
	h.SingleUserChatMessage(entindex, "\x03\tTechnosex\n");
	h.SingleUserChatMessage(entindex, "========================\n");
}

void SizzlingStats::SetTeamScores( int redscore, int bluscore )
{
	m_hostInfo.m_redscore = redscore - m_iOldRedScore;
	m_hostInfo.m_bluscore = bluscore - m_iOldBluScore;
}

#ifdef FTP_STATS

static void messagethis()
{
	CPlayerMessage::AllUserChatMessage("thread completed execution\n");
}

static void messagemain()
{
	CPlayerMessage::AllUserChatMessage("message from main\n");
}

static void messagethread()
{
	CPlayerMessage::AllUserChatMessage("message from thread\n");
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
	CFtpUploader::UploadFile(temp, html);
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

#endif

void SizzlingStats::SS_ShowHtmlStats( int entindex, bool reload_page )
{
	//V_snprintf(temp, 256, "%s/sizzlingstats/asdf.html", web_hostname.GetString());
	CUserMessageHelpers h(m_plugin_context);
	if (m_pWebStatsHandler->HasMatchUrl())
	{
		motd_msg_cfg_t cfg;
		cfg.type = MOTDPANEL_TYPE_URL;
		// reload the page when using the ".ss" method
		// don't reload the page when using the script to show stats
		if (reload_page)
		{
			char temp[128] = {};
			m_pWebStatsHandler->GetMatchUrl(temp, 128);
			h.SingleUserMOTDPanelMessage(entindex, temp, cfg);
		}
		else
		{
			h.SingleUserMOTDPanelMessage(entindex, "", cfg);
		}
	}
	else
	{
		h.SingleUserChatMessage(entindex, "\x03No match stats to view.\n");
	}
}

void SizzlingStats::SS_HideHtmlStats( int entindex )
{
	// send a blank/invisible html page to clear 
	// the visible one if it's open, but make it 
	// match the ss url so the site is still cached
	CUserMessageHelpers h(m_plugin_context);
	motd_msg_cfg_t cfg;
	cfg.type = MOTDPANEL_TYPE_URL;
	cfg.visible = false;
	h.SingleUserMOTDPanelMessage(entindex, "", cfg);
}

void SizzlingStats::OnSessionIdReceived( sizz::CString sessionid )
{
	m_plugin_context->EnqueueGameFrameFunctor(CreateFunctor(this, &SizzlingStats::LogSessionId, std::move(sessionid)));
}

void SizzlingStats::LogSessionId( const sizz::CString &str )
{
	const char *sessionid = str.ToCString();
	char temp[128] = {};
	V_snprintf(temp, 128, "[SizzlingStats]: sessionid %s\n", sessionid);
	m_plugin_context->LogPrint(temp);
}

void SizzlingStats::OnMatchUrlReceived( sizz::CString matchurl )
{
	m_plugin_context->EnqueueGameFrameFunctor(CreateFunctor(this, &SizzlingStats::CacheSiteOnPlayer, std::move(matchurl)));
}

void SizzlingStats::CacheSiteOnPlayer( const sizz::CString &match_url )
{
	motd_msg_cfg_t cfg;
	cfg.type = MOTDPANEL_TYPE_URL;
	cfg.visible = false;
	CUserMessageHelpers h(m_plugin_context);
	h.AllUserMOTDPanelMessage(match_url.ToCString(), cfg);
}

void SizzlingStats::GetPropOffsets()
{
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
	m_iWeaponsOffset = GetPropOffsetFromTable( "DT_BaseCombatCharacter", "m_hMyWeapons", bError ); // should get the 0 offsets before it incase something changes
	m_iChargeLevelOffset = GetPropOffsetFromTable( "DT_LocalTFWeaponMedigunData", "m_flChargeLevel", bError ); // should get the 0 offsets before it incase something changes
	m_iOriginOffset = GetPropOffsetFromTable( "DT_BaseEntity", "m_vecOrigin", bError );
	m_iChargeReleaseOffset = GetPropOffsetFromTable( "DT_WeaponMedigun", "m_bChargeRelease", bError );

	m_iTeamScoreOffset = GetPropOffsetFromTable( "DT_Team", "m_iScore", bError );
	m_iTeamNumOffset = GetPropOffsetFromTable( "DT_Team", "m_iTeamNum", bError );
}

void SizzlingStats::GetEntities()
{
	SCHelpers::GetTeamEnts(m_plugin_context, &m_pBluTeam, &m_pRedTeam, m_iTeamNumOffset);
}
