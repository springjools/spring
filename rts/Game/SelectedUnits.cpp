// SelectedUnits.cpp: implementation of the CSelectedUnits class.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include <map>
#include <SDL_types.h>
#include <SDL_keysym.h>

#include "mmgr.h"

#include "Game/Team.h"
#include "Game/GlobalSynced.h"
#include "SelectedUnits.h"
#include "WaitCommandsAI.h"
#include "Rendering/GL/myGL.h"
#include "NetProtocol.h"
#include "Net/PackPacket.h"
#include "ExternalAI/GroupHandler.h"
#include "ExternalAI/Group.h"
#include "ExternalAI/EngineOutHandler.h"
#include "UI/CommandColors.h"
#include "UI/GuiHandler.h"
#include "UI/TooltipConsole.h"
#include "LogOutput.h"
#include "Rendering/UnitModels/3DOParser.h"
#include "SelectedUnitsAI.h"
#include "Sim/Features/Feature.h"
#include "Sim/Units/Unit.h"
#include "Sim/Units/UnitDef.h"
#include "Sim/Units/UnitHandler.h"
#include "Sim/Units/CommandAI/BuilderCAI.h"
#include "Sim/Units/CommandAI/CommandAI.h"
#include "Sim/Units/CommandAI/LineDrawer.h"
#include "Sim/Units/UnitTypes/TransportUnit.h"
#include "System/EventHandler.h"
#include "System/Platform/ConfigHandler.h"
#include "Player.h"
#include "Camera.h"
#include "Sound.h"
#include "Util.h"

extern Uint8 *keys;


//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////
CSelectedUnits selectedUnits;


CSelectedUnits::CSelectedUnits()
: selectionChanged(false),
	possibleCommandsChanged(true),
	buildIconsFirst(false),
	selectedGroup(-1)
{
}


CSelectedUnits::~CSelectedUnits()
{
}


void CSelectedUnits::Init()
{
	buildIconsFirst = !!configHandler.GetInt("BuildIconsFirst", 0);
}


void CSelectedUnits::ToggleBuildIconsFirst()
{
	buildIconsFirst = !buildIconsFirst;
	possibleCommandsChanged = true;
}


CSelectedUnits::AvailableCommandsStruct CSelectedUnits::GetAvailableCommands()
{
	GML_RECMUTEX_LOCK(sel); // GetAvailableCommands
	GML_STDMUTEX_LOCK(group); // GetAvailableCommands

	possibleCommandsChanged = false;

	if (selectedGroup != -1 && grouphandlers[gu->myTeam]->groups[selectedGroup]->ai) {
		AvailableCommandsStruct ac;
		ac.commandPage = grouphandlers[gu->myTeam]->groups[selectedGroup]->lastCommandPage;
		ac.commands = grouphandlers[gu->myTeam]->groups[selectedGroup]->GetPossibleCommands();

		CommandDescription c;			//make sure we can clear the group even when selected
		c.id      = CMD_GROUPCLEAR;
		c.action  = "groupclear";
		c.type    = CMDTYPE_ICON;
		c.name    = "Clear group";
		c.tooltip = "Removes the units from any group they belong to";
		ac.commands.push_back(c);

		return ac;
	}

	int commandPage = 1000;
	int foundGroup = -2;
	int foundGroup2 = -2;
	map<int, int> states;

	for (CUnitSet::iterator ui = selectedUnits.begin(); ui != selectedUnits.end(); ++ui) {
		vector<CommandDescription>* c = &(*ui)->commandAI->GetPossibleCommands();
		vector<CommandDescription>::iterator ci;
		for (ci = c->begin(); ci != c->end(); ++ci) {
			states[ci->id] = ci->disabled ? 2 : 1;
		}
		if ((*ui)->commandAI->lastSelectedCommandPage < commandPage) {
			commandPage = (*ui)->commandAI->lastSelectedCommandPage;
		}

		if (foundGroup == -2 && (*ui)->group) {
			foundGroup = (*ui)->group->id;
		}
		if (!(*ui)->group || foundGroup!=(*ui)->group->id) {
			foundGroup = -1;
		}

		if (foundGroup2 == -2 && (*ui)->group) {
			foundGroup2 = (*ui)->group->id;
		}
		if (foundGroup2 >= 0 && (*ui)->group && (*ui)->group->id != foundGroup2) {
			foundGroup2 = -1;
		}
	}

	vector<CommandDescription> groupCommands;
	if (!gs->noHelperAIs) {
		//create a new group
		if (foundGroup != -2) {
			map<AIKey, string>::iterator aai;
			map<AIKey, string> suitedAis = grouphandlers[gu->myTeam]->GetSuitedAis(selectedUnits);
			if (suitedAis.size() >= 2) { // default doesn't count
				CommandDescription c;
				c.id      = CMD_AISELECT;
				c.action  = "aiselect";
				c.type    = CMDTYPE_COMBO_BOX;
				c.name    = "Select AI";
				c.tooltip = "Create a new group using the selected units and with the ai selected";

				c.params.push_back("0");
				c.params.push_back("None");
				for (aai = suitedAis.begin(); aai != suitedAis.end(); ++aai) {
					c.params.push_back((aai->second).c_str());
				}
				groupCommands.push_back(c);
			}
		}

		// add the selected units to a previous group (that at least one unit is also selected from)
		if ((foundGroup < 0) && (foundGroup2 >= 0)) {
			CommandDescription c;
			c.id      = CMD_GROUPADD;
			c.action  = "groupadd";
			c.type    = CMDTYPE_ICON;
			c.name    = "Add to group";
			c.tooltip = "Adds the selected to an existing group (of which one or more units is already selected)";
			groupCommands.push_back(c);
		}

		// select the group to which the units belong
		if (foundGroup >= 0) {
			CommandDescription c;

			c.id      = CMD_GROUPSELECT;
			c.action  = "groupselect";
			c.type    = CMDTYPE_ICON;
			c.name    = "Select group";
			c.tooltip = "Select the group that these units belong to";
			groupCommands.push_back(c);
		}

		// remove all selected units from any groups they belong to
		if (foundGroup2 != -2) {
			CommandDescription c;

			c.id      = CMD_GROUPCLEAR;
			c.action  = "groupclear";
			c.type    = CMDTYPE_ICON;
			c.name    = "Clear group";
			c.tooltip = "Removes the units from any group they belong to";
			groupCommands.push_back(c);
		}
	} // end if (!gs->noHelperAIs)

	vector<CommandDescription> commands ;
	// load the first set  (separating build and non-build commands)
	for (CUnitSet::iterator ui = selectedUnits.begin(); ui != selectedUnits.end(); ++ui) {
		vector<CommandDescription>* c = &(*ui)->commandAI->GetPossibleCommands();
		vector<CommandDescription>::iterator ci;
		for (ci = c->begin(); ci != c->end(); ++ci) {
			if (buildIconsFirst) {
				if (ci->id >= 0) { continue; }
			} else {
				if (ci->id < 0)  { continue; }
			}
			if (ci->showUnique && selectedUnits.size() > 1) {
				continue;
			}
			if (states[ci->id] > 0) {
				commands.push_back(*ci);
				states[ci->id] = 0;
			}
		}
	}

	if (!buildIconsFirst && !gs->noHelperAIs) {
		vector<CommandDescription>::iterator ci;
		for(ci = groupCommands.begin(); ci != groupCommands.end(); ++ci) {
			commands.push_back(*ci);
		}
	}

	// load the second set  (all those that have not already been included)
	for (CUnitSet::iterator ui = selectedUnits.begin(); ui != selectedUnits.end(); ++ui) {
		vector<CommandDescription>* c = &(*ui)->commandAI->GetPossibleCommands();
		vector<CommandDescription>::iterator ci;
		for (ci = c->begin(); ci != c->end(); ++ci) {
			if (buildIconsFirst) {
				if (ci->id < 0)  { continue; }
			} else {
				if (ci->id >= 0) { continue; }
			}
			if (ci->showUnique && selectedUnits.size() > 1) {
				continue;
			}
			if (states[ci->id] > 0) {
				commands.push_back(*ci);
				states[ci->id] = 0;
			}
		}
	}
	if (buildIconsFirst && !gs->noHelperAIs) {
		vector<CommandDescription>::iterator ci;
		for (ci = groupCommands.begin(); ci != groupCommands.end(); ++ci) {
			commands.push_back(*ci);
		}
	}

	AvailableCommandsStruct ac;
	ac.commandPage = commandPage;
	ac.commands = commands;
	return ac;
}


void CSelectedUnits::GiveCommand(Command c, bool fromUser)
{
	GML_RECMUTEX_LOCK(sel); // GiveCommand
	GML_STDMUTEX_LOCK(group); // GiveCommand

//	logOutput.Print("Command given %i",c.id);
	if ((gu->spectating && !gs->godMode) || selectedUnits.empty()) {
		return;
	}

	if (fromUser) {		//add some statistics
		gs->players[gu->myPlayerNum]->currentStats->numCommands++;
		if (selectedGroup!=-1) {
			gs->players[gu->myPlayerNum]->currentStats->unitCommands+=grouphandlers[gu->myTeam]->groups[selectedGroup]->units.size();
		} else {
			gs->players[gu->myPlayerNum]->currentStats->unitCommands+=selectedUnits.size();
		}
	}

	if (c.id == CMD_GROUPCLEAR) {
		for(CUnitSet::iterator ui=selectedUnits.begin();ui!=selectedUnits.end();++ui){
			if((*ui)->group){
				(*ui)->SetGroup(0);
				possibleCommandsChanged=true;
			}
		}
		return;
	}
	else if (c.id == CMD_GROUPSELECT) {
		SelectGroup((*selectedUnits.begin())->group->id);
		return;
	}
	else if (c.id == CMD_GROUPADD) {
		CGroup* group=0;
		for(CUnitSet::iterator ui=selectedUnits.begin();ui!=selectedUnits.end();++ui){
			if((*ui)->group){
				group=(*ui)->group;
				possibleCommandsChanged=true;
				break;
			}
		}
		if(group){
			for(CUnitSet::iterator ui=selectedUnits.begin();ui!=selectedUnits.end();++ui){
				if(!(*ui)->group)
					(*ui)->SetGroup(group);
			}
			SelectGroup(group->id);
		}
		return;
	}
	else if (c.id == CMD_AISELECT) {
		if (gs->noHelperAIs) {
			logOutput.Print("GroupAI and LuaUI control is disabled");
			return;
		}
		if(c.params[0]!=0){
			map<AIKey,string>::iterator aai;
			int a=0;
			for(aai=grouphandlers[gu->myTeam]->lastSuitedAis.begin();aai!=grouphandlers[gu->myTeam]->lastSuitedAis.end() && a<c.params[0]-1;++aai){
				a++;
			}
			CGroup* group=grouphandlers[gu->myTeam]->CreateNewGroup(aai->first);

			for(CUnitSet::iterator ui=selectedUnits.begin();ui!=selectedUnits.end();++ui){
				(*ui)->SetGroup(group);
			}
			SelectGroup(group->id);
		}
		return;
	}
	else if (c.id == CMD_TIMEWAIT) {
		waitCommandsAI.AddTimeWait(c);
		return;
	}
	else if (c.id == CMD_DEATHWAIT) {
		if (gs->activeAllyTeams <= 2) {
			waitCommandsAI.AddDeathWait(c);
		} else {
			logOutput.Print("DeathWait can only be used when there are 2 Ally Teams");
		}
		return;
	}
	else if (c.id == CMD_SQUADWAIT) {
		waitCommandsAI.AddSquadWait(c);
		return;
	}
	else if (c.id == CMD_GATHERWAIT) {
		waitCommandsAI.AddGatherWait(c);
		return;
	}

//	FIXME:  selectedUnitsAI.GiveCommand(c);

	if ((selectedGroup != -1) && grouphandlers[gu->myTeam]->groups[selectedGroup]->ai) {
		grouphandlers[gu->myTeam]->groups[selectedGroup]->GiveCommand(c);
		return;
	}

	SendCommand(c);

	if (!selectedUnits.empty()) {
		CUnitSet::iterator ui = selectedUnits.begin();

		int soundIdx = (*ui)->unitDef->sounds.ok.getRandomIdx();
		if (soundIdx >= 0) {
			sound->PlayUnitReply(
				(*ui)->unitDef->sounds.ok.getID(soundIdx), (*ui),
				(*ui)->unitDef->sounds.ok.getVolume(soundIdx), true);
		}
	}
}


void CSelectedUnits::AddUnit(CUnit* unit)
{
	// if unit is being transported by eg. Hulk or Atlas
	// then we should not be able to select it
	CTransportUnit *trans=unit->GetTransporter();
	if (trans != NULL && !trans->unitDef->isFirePlatform) {
		return;
	}

	if (unit->noSelect) {
		return;
	}

	GML_RECMUTEX_LOCK(sel); // AddUnit

	selectedUnits.insert(unit);
	AddDeathDependence(unit);
	selectionChanged = true;
	possibleCommandsChanged = true;

	if (!(unit->group) || unit->group->id != selectedGroup)
		selectedGroup = -1;

	PUSH_CODE_MODE;
	ENTER_MIXED;
	unit->commandAI->selected = true;
	POP_CODE_MODE;
}


void CSelectedUnits::RemoveUnit(CUnit* unit)
{
	GML_RECMUTEX_LOCK(sel); //RemoveUnit

	selectedUnits.erase(unit);
	DeleteDeathDependence(unit);
	selectionChanged=true;
	possibleCommandsChanged=true;
	selectedGroup=-1;
	PUSH_CODE_MODE;
	ENTER_MIXED;
	unit->commandAI->selected=false;
	POP_CODE_MODE;
}


void CSelectedUnits::ClearSelected()
{
	GML_RECMUTEX_LOCK(sel); // ClearSelected

	CUnitSet::iterator ui;
	ENTER_MIXED;
	for(ui=selectedUnits.begin();ui!=selectedUnits.end();++ui){
		(*ui)->commandAI->selected=false;
		DeleteDeathDependence(*ui);
	}
	ENTER_UNSYNCED;

	selectedUnits.clear();
	selectionChanged=true;
	possibleCommandsChanged=true;
	selectedGroup=-1;
}


void CSelectedUnits::SelectGroup(int num)
{
	GML_RECMUTEX_LOCK(sel); // SelectGroup
	GML_STDMUTEX_LOCK(group); // SelectGroup. not needed? only reading group

	ClearSelected();
	selectedGroup=num;
	CGroup* group=grouphandlers[gu->myTeam]->groups[num];

	CUnitSet::iterator ui;
	ENTER_MIXED;
	for(ui=group->units.begin();ui!=group->units.end();++ui){
		(*ui)->commandAI->selected=true;
		selectedUnits.insert(*ui);
		AddDeathDependence(*ui);
	}
	ENTER_UNSYNCED;

	selectionChanged=true;
	possibleCommandsChanged=true;
}


void CSelectedUnits::Draw()
{
	glDisable(GL_TEXTURE_2D);
	glDepthMask(false);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND); // for line smoothing
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	glLineWidth(cmdColors.UnitBoxLineWidth());

	GML_RECMUTEX_LOCK(sel); // Draw
	GML_STDMUTEX_LOCK(group); // Draw

	if (cmdColors.unitBox[3] > 0.0f) {
		glColor4fv(cmdColors.unitBox);

		const CUnitSet* unitSet;
		if (selectedGroup != -1) {
			unitSet = &grouphandlers[gu->myTeam]->groups[selectedGroup]->units;
		} else {
			unitSet = &selectedUnits;
		}

		glBegin(GL_QUADS);
		CUnitSet::const_iterator ui;
		for (ui = unitSet->begin(); ui != unitSet->end(); ++ui) {
			const CUnit* unit = *ui;
			if (unit->isIcon) {
				continue;
			}

			glVertexf3(unit->drawPos + float3( unit->xsize * 4, 0,  unit->ysize * 4));
			glVertexf3(unit->drawPos + float3(-unit->xsize * 4, 0,  unit->ysize * 4));
			glVertexf3(unit->drawPos + float3(-unit->xsize * 4, 0, -unit->ysize * 4));
			glVertexf3(unit->drawPos + float3( unit->xsize * 4, 0, -unit->ysize * 4));
		}
		glEnd();
	}

	// highlight queued build sites if we are about to build something
	// (or old-style, whenever the shift key is being held down)
	if (cmdColors.buildBox[3] > 0.0f) {
		//GML_RECMUTEX_LOCK(gui); // Draw. Not needed because of draw thread.
		if (!selectedUnits.empty() &&
				((cmdColors.BuildBoxesOnShift() && keys[SDLK_LSHIFT]) ||
				 ((guihandler->inCommand >= 0) &&
					(guihandler->inCommand < guihandler->commands.size()) &&
					(guihandler->commands[guihandler->inCommand].id < 0)))) {
			GML_STDMUTEX_LOCK(cai); // Draw
			bool myColor = true;
			glColor4fv(cmdColors.buildBox);
			std::list<CBuilderCAI*>::const_iterator bi;
			for (bi = uh->builderCAIs.begin(); bi != uh->builderCAIs.end(); ++bi) {
				CBuilderCAI* builder = *bi;
				if (builder->owner->team == gu->myTeam) {
					if (!myColor) {
						glColor4fv(cmdColors.buildBox);
						myColor = true;
					}
					builder->DrawQuedBuildingSquares();
				}
				else if (gs->AlliedTeams(builder->owner->team, gu->myTeam)) {
					if (myColor) {
						glColor4fv(cmdColors.allyBuildBox);
						myColor = false;
					}
					builder->DrawQuedBuildingSquares();
				}
			}
		}
	}

	glLineWidth(1.0f);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(true);
	glEnable(GL_TEXTURE_2D);
}


void CSelectedUnits::DependentDied(CObject *o)
{
	GML_RECMUTEX_LOCK(sel); // DependentDied, maybe superfluous - too late anyway

	selectedUnits.erase((CUnit*)o);
	selectionChanged=true;
	possibleCommandsChanged=true;
}


void CSelectedUnits::NetSelect(vector<int>& s,int player)
{
	netSelected[player] = s;
}


void CSelectedUnits::NetOrder(Command &c, int playerID)
{
	selectedUnitsAI.GiveCommandNet(c, playerID);

	if (netSelected[playerID].size() > 0) {
		eoh->PlayerCommandGiven(netSelected[playerID], c, playerID);
	}
}


void CSelectedUnits::AiOrder(int unitid, const Command &c, int playerID)
{
	CUnit* unit = uh->units[unitid];
	if (unit == NULL) {
		return;
	}

	const CPlayer* player = gs->players[playerID];
	if (player == NULL) {
		return;
	}
	if (!player->CanControlTeam(unit->team)) {
		logOutput.Print("Invalid order from player %i for (unit %i, team %i)",
		                playerID, unitid, unit->team);
		return;
	}
	
	unit->commandAI->GiveCommand(c, false);
}


bool CSelectedUnits::CommandsChanged()
{
	return possibleCommandsChanged;
}


/******************************************************************************/
//
//  GetDefaultCmd() and friends
//

static bool targetIsEnemy = false;
static const CUnit* targetUnit = NULL;
static const CFeature* targetFeature = NULL;


static inline bool CanDamage(const UnitDef* ud)
{
	return ((ud->canAttack && !ud->weapons.empty()) || ud->canKamikaze);
}


static inline bool IsBetterLeader(const UnitDef* newDef, const UnitDef* oldDef)
{
	// There is a lot more that could be done here to make better
	// selections, but the users may prefer simplicity over smarts.

	if (targetUnit) {
		if (targetIsEnemy) {
			const bool newCanDamage = CanDamage(newDef);
			const bool oldCanDamage = CanDamage(oldDef);
			if ( newCanDamage && !oldCanDamage) { return true;  }
			if (!newCanDamage &&  oldCanDamage) { return false; }
			if (!CanDamage(targetUnit->unitDef)) {
				if ( newDef->canReclaim && !oldDef->canReclaim) { return true;  }
				if (!newDef->canReclaim &&  oldDef->canReclaim) { return false; }
			}
		}
		else { // targetIsAlly
			if (targetUnit->health < targetUnit->maxHealth) {
				if ( newDef->canRepair && !oldDef->canRepair) { return true;  }
				if (!newDef->canRepair &&  oldDef->canRepair) { return false; }
			}
			const bool newCanLoad = (newDef->transportCapacity > 0);
			const bool oldCanLoad = (oldDef->transportCapacity > 0);
			if ( newCanLoad && !oldCanLoad) { return true;  }
			if (!newCanLoad &&  oldCanLoad) { return false; }
			if ( newDef->canGuard && !oldDef->canGuard) { return true;  }
			if (!newDef->canGuard &&  oldDef->canGuard) { return false; }
		}
	}
	else if (targetFeature) {
		if (!targetFeature->createdFromUnit.empty()) {
			if ( newDef->canResurrect && !oldDef->canResurrect) { return true;  }
			if (!newDef->canResurrect &&  oldDef->canResurrect) { return false; }
		}
		if ( newDef->canReclaim && !oldDef->canReclaim) { return true;  }
		if (!newDef->canReclaim &&  oldDef->canReclaim) { return false; }
	}

	return (newDef->speed > oldDef->speed); // CMD_MOVE?
}


// CALLINFO: 
// DrawMapStuff --> CGuiHandler::GetDefaultCommand --> GetDefaultCmd
// CMouseHandler::DrawCursor --> DrawCentroidCursor --> CGuiHandler::GetDefaultCommand --> GetDefaultCmd
// LuaUnsyncedRead::GetDefaultCommand --> CGuiHandler::GetDefaultCommand --> GetDefaultCmd
int CSelectedUnits::GetDefaultCmd(CUnit* unit, CFeature* feature)
{
	GML_RECMUTEX_LOCK(sel); // GetDefaultCmd
	GML_STDMUTEX_LOCK(group); // GetDefaultCmd
	// NOTE: the unitDef->aihint value is being ignored
	int luaCmd;
	if (eventHandler.DefaultCommand(unit, feature, luaCmd)) {
		return luaCmd;
	}

	if ((selectedGroup != -1) && grouphandlers[gu->myTeam]->groups[selectedGroup]->ai) {
		return grouphandlers[gu->myTeam]->groups[selectedGroup]->GetDefaultCmd(unit, feature);
	}

	// return the default if there are no units selected
	CUnitSet::const_iterator ui = selectedUnits.begin();
	if (ui == selectedUnits.end()) {
		return CMD_STOP;
	}

	// setup the locals for IsBetterLeader()
	targetUnit = unit;
	targetFeature = feature;
	if (targetUnit) {
		targetIsEnemy = !gs->Ally(gu->myAllyTeam, targetUnit->allyteam);
	}

	// find the best leader to pick the command
	const CUnit* leaderUnit = *ui;
	const UnitDef* leaderDef = leaderUnit->unitDef;
	for (++ui; ui != selectedUnits.end(); ++ui) {
		const CUnit* testUnit = *ui;
		const UnitDef* testDef = testUnit->unitDef;
		if (testDef != leaderDef) {
			if (IsBetterLeader(testDef, leaderDef)) {
				leaderDef = testDef;
				leaderUnit = testUnit;
			}
		}
	}

	return (leaderUnit->commandAI->GetDefaultCmd(unit, feature));
}


/******************************************************************************/

void CSelectedUnits::PossibleCommandChange(CUnit* sender)
{
	GML_RECMUTEX_LOCK(sel); // PossibleCommandChange

	if (sender == NULL || selectedUnits.find(sender) != selectedUnits.end())
		possibleCommandsChanged = true;
}

// CALLINFO: 
// CGame::Draw --> DrawCommands
// CMiniMap::DrawForReal --> DrawCommands
void CSelectedUnits::DrawCommands()
{
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_DEPTH_TEST);

	lineDrawer.Configure(cmdColors.UseColorRestarts(),
	                     cmdColors.UseRestartColor(),
	                     cmdColors.restart,
	                     cmdColors.RestartAlpha());
	lineDrawer.SetupLineStipple();

	glEnable(GL_BLEND);
	glBlendFunc((GLenum)cmdColors.QueuedBlendSrc(),
	            (GLenum)cmdColors.QueuedBlendDst());

	glLineWidth(cmdColors.QueuedLineWidth());

	GML_RECMUTEX_LOCK(sel); // DrawCommands
	GML_STDMUTEX_LOCK(group); // DrawCommands
	GML_STDMUTEX_LOCK(cai); // DrawCommands

	CUnitSet::iterator ui;
	if (selectedGroup != -1) {
		CUnitSet& groupUnits = grouphandlers[gu->myTeam]->groups[selectedGroup]->units;
		for(ui = groupUnits.begin(); ui != groupUnits.end(); ++ui) {
			(*ui)->commandAI->DrawCommands();
		}
	} else {
		for(ui = selectedUnits.begin(); ui != selectedUnits.end(); ++ui) {
			(*ui)->commandAI->DrawCommands();
		}
	}

	// draw the commands from AIs
	grouphandlers[gu->myTeam]->DrawCommands();
	waitCommandsAI.DrawCommands();

	glLineWidth(1.0f);

	glEnable(GL_DEPTH_TEST);
}


// CALLINFO:
// CTooltipConsole::Draw --> CMouseHandler::GetCurrentTooltip
// LuaUnsyncedRead::GetCurrentTooltip --> CMouseHandler::GetCurrentTooltip
// CMouseHandler::GetCurrentTooltip --> CMiniMap::GetToolTip --> GetTooltip
// CMouseHandler::GetCurrentTooltip --> GetTooltip
std::string CSelectedUnits::GetTooltip(void)
{
	GML_RECMUTEX_LOCK(sel); // tooltipconsole::draw --> mousehandler::getcurrenttooltip --> gettooltip
	GML_STDMUTEX_LOCK(group); // GetTooltip

	std::string s;
	if ((selectedGroup != -1) && grouphandlers[gu->myTeam]->groups[selectedGroup]->ai) {
		s = "Group selected";
	} else if (!selectedUnits.empty()) {
		// show the player name instead of unit name if it has FBI tag showPlayerName
		if ((*selectedUnits.begin())->unitDef->showPlayerName) {
			if (gs->Team((*selectedUnits.begin())->team)->leader >= 0)
				s = gs->players[gs->Team((*selectedUnits.begin())->team)->leader]->name.c_str();
			else
				s = "Uncontrolled";
		} else {
			s = (*selectedUnits.begin())->tooltip;
		}
	}

	if (selectedUnits.empty()) {
		return s;
	}

	const string custom = eventHandler.WorldTooltip(NULL, NULL, NULL);
	if (!custom.empty()) {
		return custom;
	}

	char tmp[500];
	int numFuel = 0;
	float maxHealth = 0.0f, curHealth = 0.0f;
	float maxFuel = 0.0f, curFuel = 0.0f;
	float exp = 0.0f, cost = 0.0f, range = 0.0f;
	float metalMake = 0.0f, metalUse = 0.0f, energyMake = 0.0f, energyUse = 0.0f;

	CUnitSet::iterator ui;
	for (ui = selectedUnits.begin(); ui != selectedUnits.end(); ++ui) {
		const CUnit* unit = *ui;
		maxHealth  += unit->maxHealth;
		curHealth  += unit->health;
		exp        += unit->experience;
		cost       += unit->metalCost + (unit->energyCost / 60.0f);
		range      += unit->maxRange;
		metalMake  += unit->metalMake;
		metalUse   += unit->metalUse;
		energyMake += unit->energyMake;
		energyUse  += unit->energyUse;
		maxFuel    += unit->unitDef->maxFuel;
		curFuel    += unit->currentFuel;
		if (unit->unitDef->maxFuel > 0) {
			numFuel++;
		}
	}
	if ((numFuel > 0) && (maxFuel > 0.0f)) {
		curFuel = curFuel / numFuel;
		maxFuel = maxFuel / numFuel;
	}
	const float num = selectedUnits.size();

	s += CTooltipConsole::MakeUnitStatsString(
	       curHealth, maxHealth,
	       curFuel,   maxFuel,
	       (exp / num), cost, (range / num),
	       metalMake,  metalUse,
	       energyMake, energyUse);

  if (gs->cheatEnabled && (selectedUnits.size() == 1)) {
  	CUnit* unit = *selectedUnits.begin();
    SNPRINTF(tmp, sizeof(tmp), "\xff\xc0\xc0\xff  [TechLevel %i]",
             unit->unitDef->techLevel);
    s += tmp;
	}

	return s;
}


void CSelectedUnits::SetCommandPage(int page)
{
	GML_RECMUTEX_LOCK(sel); // CGame::Draw --> RunLayoutCommand --> LayoutIcons --> RevertToCmdDesc --> SetCommandPage
	GML_STDMUTEX_LOCK(group); // SetCommandPage

	if(selectedGroup!=-1 && grouphandlers[gu->myTeam]->groups[selectedGroup]->ai){
		grouphandlers[gu->myTeam]->groups[selectedGroup]->lastCommandPage=page;
	}

	CUnitSet::iterator ui;
	for (ui = selectedUnits.begin(); ui != selectedUnits.end(); ++ui) {
		(*ui)->commandAI->lastSelectedCommandPage = page;
	}
}


void CSelectedUnits::SendSelection(void)
{
	GML_RECMUTEX_LOCK(sel); // SendSelection

	// first, convert CUnit* to unit IDs.
	std::vector<short> selectedUnitIDs(selectedUnits.size());
	std::vector<short>::iterator i = selectedUnitIDs.begin();
	CUnitSet::const_iterator ui = selectedUnits.begin();
	for(; ui != selectedUnits.end(); ++i, ++ui) *i = (*ui)->id;
	net->Send(CBaseNetProtocol::Get().SendSelect(gu->myPlayerNum, selectedUnitIDs));
	selectionChanged=false;
}


void CSelectedUnits::SendCommand(Command& c)
{
	if (selectionChanged) {		//send new selection
		SendSelection();
	}
	net->Send(CBaseNetProtocol::Get().SendCommand(gu->myPlayerNum, c.id, c.options, c.params));
}


void CSelectedUnits::SendCommandsToUnits(const vector<int>& unitIDs,
                                         const vector<Command>& commands)
{
	// NOTE: does not check for invalid unitIDs

	if (gu->spectating && !gs->godMode) {
		return; // don't waste bandwidth
	}

	const unsigned unitIDCount  = unitIDs.size();
	const unsigned commandCount = commands.size();

	if ((unitIDCount == 0) || (commandCount == 0)) {
		return;
	}

	unsigned totalParams = 0;
	for (unsigned c = 0; c < commandCount; c++) {
		totalParams += commands[c].params.size();
	}

	unsigned msgLen = 0;
	msgLen += (1 + 2 + 1); // msg type, msg size, player ID
	msgLen += 2; // unitID count
	msgLen += unitIDCount * 2;
	msgLen += 2; // command count
	msgLen += commandCount * (4 + 1 + 2); // id, options, params size
	msgLen += totalParams * 4;
	if (msgLen > 8192) {
		logOutput.Print("Discarded oversized NETMSG_AICOMMANDS packet: %i\n",
		                msgLen);
		return; // drop the oversized packet
	}
	netcode::PackPacket* packet = new netcode::PackPacket(msgLen);
	*packet << static_cast<unsigned char>(NETMSG_AICOMMANDS)
	        << static_cast<unsigned short>(msgLen)
	        << static_cast<unsigned char>(gu->myPlayerNum);
	
	*packet << static_cast<unsigned short>(unitIDCount);
	for (std::vector<int>::const_iterator it = unitIDs.begin(); it != unitIDs.end(); ++it)
	{
		*packet << static_cast<short>(*it);
	}

	*packet << static_cast<unsigned short>(commandCount);
	for (unsigned i = 0; i < commandCount; ++i) {
		const Command& cmd = commands[i];
		*packet << static_cast<unsigned int>(cmd.id)
		        << cmd.options
		        << static_cast<unsigned short>(cmd.params.size()) << cmd.params;
	}

	net->Send(boost::shared_ptr<netcode::RawPacket>(packet));
	return;
}
