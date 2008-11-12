/*
	Copyright (c) 2008 Robin Vobruba <hoijui.quaero@gmail.com>

	This program is free software {} you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation {} either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY {} without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "EngineOutHandler.h"
#include "Group.h"
#include "GroupHandler.h"
#include "SkirmishAIWrapper.h"
//#include "GroupAIWrapper.h"
#include "Game/GlobalSynced.h"
#include "Game/GameHelper.h"
#include "Game/Player.h"
#include "Sim/Units/Unit.h"
#include "Platform/ConfigHandler.h"
#include "LogOutput.h"
#include "Util.h"
#include "TimeProfiler.h"


CR_BIND_DERIVED(CEngineOutHandler,CObject,)

CR_REG_METADATA(CEngineOutHandler, (
				CR_MEMBER(skirmishAIs)//,
//				CR_MEMBER(groupAIs)
				));

/////////////////////////////
// BEGIN: Exception Handling

bool CEngineOutHandler::IsCatchExceptions() {

	static bool init = false;
	static bool isCatchExceptions;

	if (!init) {
		isCatchExceptions = configHandler.GetInt("CatchAIExceptions", 1) != 0;
		init = true;
	}

	return isCatchExceptions;
}

// to switch off the exception handling and have it catched by the debugger.
#define HANDLE_EXCEPTION  \
	catch (const std::exception& e) {		\
		if (IsCatchExceptions()) {			\
			handleAIException(e.what());	\
			throw;							\
		} else throw;						\
	}										\
	catch (const char *s) {					\
		if (IsCatchExceptions()) {			\
			handleAIException(s);			\
			throw;							\
		} else throw;						\
	}										\
	catch (...) {							\
		if (IsCatchExceptions()) {			\
			handleAIException(0);			\
			throw;							\
		} else throw;						\
	}

void handleAIException(const char* description) {

	if (description) {
		logOutput.Print("AI Exception: \'%s\'", description);
	} else {
		logOutput.Print("AI Exception");
	}
//	exit(-1);
}

// END: Exception Handling
/////////////////////////////

CEngineOutHandler* CEngineOutHandler::singleton = NULL;

void CEngineOutHandler::Initialize() {

	if (singleton == NULL) {
		singleton = SAFE_NEW CEngineOutHandler();
	}
}
CEngineOutHandler* CEngineOutHandler::GetInstance() {

	if (singleton == NULL) {
		Initialize();
	}

	return singleton;
}
void CEngineOutHandler::Destroy() {

	if (singleton != NULL) {
		CEngineOutHandler* tmp = singleton;
		singleton = NULL;
		delete tmp;
	}
}

CEngineOutHandler::CEngineOutHandler() : activeTeams(gs->activeTeams) {

	for (int t=0; t < MAX_TEAMS; ++t) {
		skirmishAIs[t] = NULL;
	}
	hasSkirmishAIs = false;

/*
	for (unsigned int t=0; t < MAX_TEAMS; ++t) {
		for (unsigned int g=0; g < MAX_GROUPS; ++g) {
			groupAIs[t][g] = NULL;
		}
		hasTeamGroupAIs[t] = false;
	}
	hasGroupAIs = false;
*/
}

CEngineOutHandler::~CEngineOutHandler() {

	for (int t=0; t < MAX_TEAMS; ++t) {
		delete skirmishAIs[t];
	}

/*
	for (unsigned int t=0; t < MAX_TEAMS; ++t) {
		for (unsigned int g=0; g < MAX_GROUPS; ++g) {
			delete groupAIs[t][g];
		}
	}
*/
}




#define DO_FOR_ALL_SKIRMISH_AIS(FUNC)						\
		if (hasSkirmishAIs) {								\
			for (unsigned int t=0; t < activeTeams; ++t) {	\
				if (skirmishAIs[t]) {						\
					skirmishAIs[t]->FUNC;					\
				}											\
			}												\
		}

/*
#define DO_FOR_ALL_GROUP_AIS(FUNC)									\
		if (hasGroupAIs) {											\
			for (unsigned int t=0; t < activeTeams; ++t) {			\
				if (hasTeamGroupAIs[t]) {							\
					for (unsigned int g=0; g < MAX_GROUPS; ++g) {	\
						if (groupAIs[t][g]) {						\
							groupAIs[t][g]->FUNC;					\
						}											\
					}												\
				}													\
			}														\
		}
*/

#define DO_FOR_SKIRMISH_AND_GROUP_AIS(FUNC)	\
		DO_FOR_ALL_SKIRMISH_AIS(FUNC)//		\
		//DO_FOR_ALL_GROUP_AIS(FUNC)


void CEngineOutHandler::PostLoad() {}

void CEngineOutHandler::PreDestroy() {

	try {
		DO_FOR_SKIRMISH_AND_GROUP_AIS(PreDestroy())
	} HANDLE_EXCEPTION;
}

void CEngineOutHandler::Load(std::istream* s) {

	try {
		DO_FOR_SKIRMISH_AND_GROUP_AIS(Load(s))
	} HANDLE_EXCEPTION;
}

void CEngineOutHandler::Save(std::ostream* s) {

	try {
		DO_FOR_SKIRMISH_AND_GROUP_AIS(Save(s))
	} HANDLE_EXCEPTION;
}



void CEngineOutHandler::Update() {

	SCOPED_TIMER("AI")
	try {
		int frame = gs->frameNum;
		DO_FOR_SKIRMISH_AND_GROUP_AIS(Update(frame))
	} HANDLE_EXCEPTION;
}





#define DO_FOR_ALLIED_SKIRMISH_AIS(FUNC, ALLY_TEAM_ID, UNIT_ALLY_TEAM_ID)	\
	if (hasSkirmishAIs && !gs->Ally(ALLY_TEAM_ID, UNIT_ALLY_TEAM_ID)) {		\
		for (unsigned int t=0; t < activeTeams; ++t) {						\
			if (skirmishAIs[t] && gs->AllyTeam(t) == ALLY_TEAM_ID) {		\
				try {														\
					skirmishAIs[t]->FUNC;									\
				} HANDLE_EXCEPTION;											\
			}																\
		}																	\
	}

/*
#define DO_FOR_ALLIED_GROUP_AIS(FUNC, ALLY_TEAM_ID, UNIT_ALLY_TEAM_ID)		\
	if (hasGroupAIs && !gs->Ally(ALLY_TEAM_ID, UNIT_ALLY_TEAM_ID)) {		\
		for (unsigned int t=0; t < activeTeams; ++t) {						\
			if (hasTeamGroupAIs[t] && gs->AllyTeam(t) == ALLY_TEAM_ID) {	\
				for (unsigned int g=0; g < MAX_GROUPS; ++g) {				\
					if (groupAIs[t][g]) {									\
						try {												\
							groupAIs[t][g]->EnemyEnterLOS(unitId);			\
						} HANDLE_EXCEPTION;									\
					}														\
				}															\
			}																\
		}																	\
	}
*/


#define DO_FOR_ALLIED_SKIRMISH_AND_GROUP_AIS(FUNC, ALLY_TEAM_ID, UNIT_ALLY_TEAM_ID)	\
		DO_FOR_ALLIED_SKIRMISH_AIS(FUNC, ALLY_TEAM_ID, UNIT_ALLY_TEAM_ID)//			\
		//DO_FOR_ALLIED_GROUP_AIS(FUNC, ALLY_TEAM_ID, UNIT_ALLY_TEAM_ID)


void CEngineOutHandler::UnitEnteredLos(const CUnit& unit, int allyTeamId) {

	int unitId = unit.id;
	int unitAllyTeamId = unit.allyteam;
	DO_FOR_ALLIED_SKIRMISH_AND_GROUP_AIS(EnemyEnterLOS(unitId), allyTeamId, unitAllyTeamId)
	
/*
	if (hasSkirmishAIs) {
		for (unsigned int t=0; t < activeTeams; ++t) {
			if (skirmishAIs[t] && gs->AllyTeam(t) == allyTeamId && !gs->Ally(allyTeamId, unit->allyteam)) {
				try {
					skirmishAIs[t]->EnemyEnterLOS(unitId);
				} HANDLE_EXCEPTION;
			}
		}
	}
	
	if (hasGroupAIs) {
		for (unsigned int t=0; t < activeTeams; ++t) {
			if (hasTeamGroupAIs[t] && gs->AllyTeam(t) == allyTeamId && !gs->Ally(allyTeamId, unit->allyteam)) {
				for (unsigned int g=0; g < MAX_GROUPS; ++g) {
					if (groupAIs[t][g]) {
						try {
							groupAIs[t][g]->EnemyEnterLOS(unitId);
						} HANDLE_EXCEPTION;
					}
				}
			}	
		}
	}
*/
}

void CEngineOutHandler::UnitLeftLos(const CUnit& unit, int allyTeamId) {

	int unitId = unit.id;
	int unitAllyTeamId = unit.allyteam;
	DO_FOR_ALLIED_SKIRMISH_AND_GROUP_AIS(EnemyLeaveLOS(unitId), allyTeamId, unitAllyTeamId)
}

void CEngineOutHandler::UnitEnteredRadar(const CUnit& unit, int allyTeamId) {

	int unitId = unit.id;
	int unitAllyTeamId = unit.allyteam;
	DO_FOR_ALLIED_SKIRMISH_AND_GROUP_AIS(EnemyEnterRadar(unitId), allyTeamId, unitAllyTeamId)
}

void CEngineOutHandler::UnitLeftRadar(const CUnit& unit, int allyTeamId) {

	int unitId = unit.id;
	int unitAllyTeamId = unit.allyteam;
	DO_FOR_ALLIED_SKIRMISH_AND_GROUP_AIS(EnemyLeaveRadar(unitId), allyTeamId, unitAllyTeamId)
}




#define DO_FOR_TEAM_SKIRMISH_AIS(FUNC, TEAM_ID)		\
	if (skirmishAIs[TEAM_ID]) {						\
		try {										\
			skirmishAIs[TEAM_ID]->FUNC;				\
		} HANDLE_EXCEPTION;							\
	}

/*
#define DO_FOR_TEAM_GROUP_AIS(FUNC, TEAM_ID)		\
	if (hasTeamGroupAIs[TEAM_ID]) {				\
		for (unsigned int g=0; g < MAX_GROUPS; ++g) {	\
			if (groupAIs[TEAM_ID][g]) {			\
				try {									\
					groupAIs[TEAM_ID][g]->FUNC;	\
				} HANDLE_EXCEPTION;						\
			}											\
		}												\
	}
*/


#define DO_FOR_TEAM_SKIRMISH_AND_GROUP_AIS(FUNC, TEAM_ID)	\
		DO_FOR_TEAM_SKIRMISH_AIS(FUNC, TEAM_ID)//				\
		//DO_FOR_TEAM_GROUP_AIS(FUNC, TEAM_ID)

void CEngineOutHandler::UnitIdle(const CUnit& unit) {

	int teamId = unit.team;
	int unitId = unit.id;

	DO_FOR_TEAM_SKIRMISH_AND_GROUP_AIS(UnitIdle(unitId), teamId);
}

void CEngineOutHandler::UnitCreated(const CUnit& unit) {

	int teamId = unit.team;
	int unitId = unit.id;

	DO_FOR_TEAM_SKIRMISH_AND_GROUP_AIS(UnitCreated(unitId), teamId);
}

void CEngineOutHandler::UnitFinished(const CUnit& unit) {

	int teamId = unit.team;
	int unitId = unit.id;

	DO_FOR_TEAM_SKIRMISH_AND_GROUP_AIS(UnitFinished(unitId), teamId);
}


void CEngineOutHandler::UnitMoveFailed(const CUnit& unit) {

	int teamId = unit.team;
	int unitId = unit.id;

	DO_FOR_TEAM_SKIRMISH_AND_GROUP_AIS(UnitMoveFailed(unitId), teamId);
}

void CEngineOutHandler::UnitGiven(const CUnit& unit, int oldTeam) {

	int newTeam = unit.team;
	int unitId = unit.id;

	DO_FOR_TEAM_SKIRMISH_AND_GROUP_AIS(UnitGiven(unitId, oldTeam, newTeam), newTeam);
	//DO_FOR_TEAM_SKIRMISH_AND_GROUP_AIS(UnitGiven(unitId, oldTeam, newTeam), oldTeam);
}

void CEngineOutHandler::UnitCaptured(const CUnit& unit, int newTeam) {

	int oldTeam = unit.team;
	int unitId = unit.id;

	DO_FOR_TEAM_SKIRMISH_AND_GROUP_AIS(UnitCaptured(unitId, oldTeam, newTeam), oldTeam);
	//DO_FOR_TEAM_SKIRMISH_AND_GROUP_AIS(UnitCaptured(unitId, oldTeam, newTeam), newTeam);
}


void CEngineOutHandler::UnitDestroyed(const CUnit& destroyed,
		const CUnit* attacker) {

	int destroyedId = destroyed.id;
	int attackerId = attacker ? attacker->id : 0;

	if (hasSkirmishAIs) {
		try {
			for (unsigned int t=0; t < activeTeams; ++t) {
				if (skirmishAIs[t]
						&& !gs->Ally(gs->AllyTeam(t), destroyed.allyteam)
						&& (skirmishAIs[t]->IsCheatEventsEnabled()
							|| (destroyed.losStatus[t] & (LOS_INLOS | LOS_INRADAR)))) {
					skirmishAIs[t]->EnemyDestroyed(destroyedId, attackerId);
				}
			}
			if (skirmishAIs[destroyed.team]) {
				skirmishAIs[destroyed.team]->UnitDestroyed(destroyedId, attackerId);
			}
		} HANDLE_EXCEPTION;
	}
	
/*
	if (hasGroupAIs) {
		for (unsigned int t=0; t < activeTeams; ++t) {
			if (hasTeamGroupAIs[t]
						&& !gs->Ally(gs->AllyTeam(t), destroyed.allyteam)) {
				bool isVisible =
						destroyed.losStatus[t] & (LOS_INLOS | LOS_INRADAR);
				for (unsigned int g=0; g < MAX_GROUPS; ++g) {
					if (groupAIs[t][g]
							&& (groupAIs[t][g]->IsCheatEventsEnabled()
								|| isVisible)) {
						try {
							groupAIs[t][g]->EnemyDestroyed(destroyedId, attackerId);
						} HANDLE_EXCEPTION;
					}
				}
			}	
		}
		if (hasTeamGroupAIs[destroyed.team]) {
			for (unsigned int g=0; g < MAX_GROUPS; ++g) {
				if (groupAIs[destroyed.team][g]) {
					try {
						groupAIs[destroyed.team][g]->EnemyDestroyed(destroyedId, attackerId);
					} HANDLE_EXCEPTION;
				}
			}
		}
	}
*/
}


void CEngineOutHandler::UnitDamaged(const CUnit& damaged, const CUnit* attacker,
		float damage) {

	int damagedUnitId = damaged.id;
	int attackerUnitId = attacker ? attacker->id : -1;
	float3 attackDir_damagedsView;
	float3 attackDir_attackersView;
	if (attacker) {
		attackDir_damagedsView =
				helper->GetUnitErrorPos(attacker, damaged.allyteam)
				- damaged.pos;
		attackDir_damagedsView.ANormalize();

		attackDir_attackersView =
				attacker->pos
				- helper->GetUnitErrorPos(&damaged, attacker->allyteam);
		attackDir_attackersView.ANormalize();
	} else {
		attackDir_damagedsView = ZeroVector;
	}
	int dt = damaged.team;
	int at = attacker->team;

	if (hasSkirmishAIs) {
		try {
			if (skirmishAIs[dt]) {
				skirmishAIs[dt]->UnitDamaged(damagedUnitId,
						attackerUnitId, damage, attackDir_damagedsView);
			}

			if (attacker) {
				if (skirmishAIs[at]
						&& !gs->Ally(gs->AllyTeam(at), damaged.allyteam)
						&& (skirmishAIs[at]->IsCheatEventsEnabled()
							|| (damaged.losStatus[at] & (LOS_INLOS | LOS_INRADAR)))) {
					skirmishAIs[at]->EnemyDamaged(damagedUnitId, attackerUnitId,
							damage, attackDir_attackersView);
				}
			}
		} HANDLE_EXCEPTION;
	}

/*
	if (hasTeamGroupAIs[dt]) {
		for (unsigned int g=0; g < MAX_GROUPS; ++g) {
			if (groupAIs[dt][g]) {
				try {
					groupAIs[dt][g]->UnitDamaged(damagedUnitId,
							attackerUnitId, damage, attackDir_damagedsView);
				} HANDLE_EXCEPTION;
			}
		}
	}
	if (hasTeamGroupAIs[at]
			&& !gs->Ally(gs->AllyTeam(at), attacked->allyteam)) {
		bool isVisible = attacked->losStatus[at] & (LOS_INLOS | LOS_INRADAR);
		for (unsigned int g=0; g < MAX_GROUPS; ++g) {
			if (groupAIs[at][g]
					&& (groupAIs[at][g]->IsCheatEventsEnabled()
						|| isVisible)) {
				try {
					groupAIs[at][g]->EnemyDamaged(damagedUnitId, attackerUnitId,
							damage, attackDir_attackersView);
				} HANDLE_EXCEPTION;
			}
		}
	}
*/
}





void CEngineOutHandler::SeismicPing(int allyTeamId, const CUnit& unit,
		const float3& pos, float strength) {

	int unitId = unit.id;
	int unitAllyTeamId = unit.allyteam;
	DO_FOR_ALLIED_SKIRMISH_AND_GROUP_AIS(SeismicPing(allyTeamId, unitId, pos, strength), allyTeamId, unitAllyTeamId)
}

void CEngineOutHandler::WeaponFired(const CUnit& unit, const WeaponDef& def) {

	int teamId = unit.team;
	int unitId = unit.id;
	int defId = def.id;

	DO_FOR_TEAM_SKIRMISH_AND_GROUP_AIS(WeaponFired(unitId, defId), teamId);
}

void CEngineOutHandler::PlayerCommandGiven(
		const std::vector<int>& selectedUnitIds, const Command& c, int playerId)
{

	int teamId = gs->players[playerId]->team;
	
	DO_FOR_TEAM_SKIRMISH_AND_GROUP_AIS(PlayerCommandGiven(selectedUnitIds, c, playerId), teamId);
}

void CEngineOutHandler::CommandFinished(const CUnit& unit, int commandTopicId) {

	int teamId = unit.team;
	int unitId = unit.id;
	DO_FOR_TEAM_SKIRMISH_AND_GROUP_AIS(CommandFinished(unitId, commandTopicId), teamId);
}

void CEngineOutHandler::GotChatMsg(const char* msg, int fromPlayerId) {

	DO_FOR_SKIRMISH_AND_GROUP_AIS(GotChatMsg(msg, fromPlayerId))
}






bool CEngineOutHandler::CreateSkirmishAI(int teamId, const SSAIKey& key,
		const std::map<std::string, std::string>& options) {

	if ((teamId < 0) || (teamId >= gs->activeTeams)) {
		return false;
	}

	try {
		if (skirmishAIs[teamId]) {
			delete skirmishAIs[teamId];
			skirmishAIs[teamId] = NULL;
		}

		skirmishAIs[teamId] = SAFE_NEW CSkirmishAIWrapper(teamId, key, options);
		hasSkirmishAIs = true;
		
		return true;
	} HANDLE_EXCEPTION;
	
	return false;
}

bool CEngineOutHandler::IsSkirmishAI(int teamId) const {
	return skirmishAIs[teamId];
}

void CEngineOutHandler::DestroySkirmishAI(int teamId) {
	
	try {
		delete skirmishAIs[teamId];
		skirmishAIs[teamId] = NULL;
	} HANDLE_EXCEPTION;
}




/*
bool CreateGroupAI(const CGroup& group, const SGAIKey& key,
			const std::map<std::string, std::string>& options) {

	int teamId = group->handler->team;
	int groupId = group->id;

	if ((teamId < 0) || (teamId >= gs->activeTeams)) {
		return false;
	}

	try {
		if (groupAIs[teamId][groupId]) {
			delete groupAIs[teamId][groupId];
			groupAIs[teamId][groupId] = NULL;
		}

		groupAIs[teamId][groupId] = SAFE_NEW CGroupAIWrapper(teamId, groupId, key, options);
		hasGroupAIs = true;
		hasTeamGroupAIs[teamId] = true;

		return true;
	} HANDLE_EXCEPTION;

	return false;
}


bool CEngineOutHandler::IsGroupAI(const CGroup& group) {

	int teamId = group.handler->team;
	int groupId = group.id;

	return groupAIs[teamId][groupId];
}

void CEngineOutHandler::DestroyGroupAI(const CGroup& group) {

	int teamId = group.handler->team;
	int groupId = group.id;

	try {
		delete groupAIs[teamId][groupId];
		groupAIs[teamId][groupId] = NULL;
	} HANDLE_EXCEPTION;
}
*/
