/*
* This file is part of the CMaNGOS Project. See AUTHORS file for Copyright information
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA
*/

#include "WorldState.h"
#include "World/World.h"
#include "Maps/MapManager.h"
#include "Entities/Object.h"
#include "GameEvents/GameEventMgr.h"
#include <algorithm>
#include <map>
#include <World/WorldStateDefines.h>

enum
{
    GROMGOLUC_EVENT_1   = 15312,
    GROMGOLUC_EVENT_2   = 15313,
    GROMGOLUC_EVENT_3   = 15314,
    GROMGOLUC_EVENT_4   = 15315,

    OGUC_EVENT_1        = 15318,
    OGUC_EVENT_2        = 15319,
    OGUC_EVENT_3        = 15320,
    OGUC_EVENT_4        = 15321,

    GROMGOLOG_EVENT_1   = 15322,
    GROMGOLOG_EVENT_2   = 15323,
    GROMGOLOG_EVENT_3   = 15324,
    GROMGOLOG_EVENT_4   = 15325,
};

WorldState::WorldState() : m_emeraldDragonsState(0xF), m_emeraldDragonsTimer(0), m_emeraldDragonsChosenPositions(4, 0), m_isMagtheridonHeadSpawnedHorde(false), m_isMagtheridonHeadSpawnedAlliance(false),
    m_adalSongOfBattleTimer(0), m_expansion(EXPANSION_WOTLK), m_highlordKruulSpawned(false), m_highlordKruulTimer(0), m_highlordKruulChosenPosition(0)
{
    m_transportStates[GROMGOL_UNDERCITY]    = GROMGOLUC_EVENT_1;
    m_transportStates[GROMGOL_ORGRIMMAR]    = OGUC_EVENT_1;
    m_transportStates[ORGRIMMAR_UNDERCITY]  = GROMGOLOG_EVENT_1;
    memset(m_loveIsInTheAirData.counters, 0, sizeof(LoveIsInTheAir));
}


WorldState::~WorldState()
{
}

void WorldState::Load()
{
    std::unique_ptr<QueryResult> result(CharacterDatabase.Query("SELECT Id, Data FROM world_state"));

    if (result)
    {
        // always one row
        do
        {
            Field* fields = result->Fetch();
            uint32 id = fields[0].GetUInt32();
            std::string data = fields[1].GetCppString();
            std::istringstream loadStream(data);
            switch (id)
            {
                case SAVE_ID_EMERALD_DRAGONS:
                {
                    auto curTime = World::GetCurrentClockTime();
                    uint64 respawnTime;
                    if (data.size())
                    {
                        loadStream >> m_emeraldDragonsState >> respawnTime;
                        for (uint32 i = 0; i < 4; ++i)
                            loadStream >> m_emeraldDragonsChosenPositions[i];
                        if (respawnTime)
                        {
                            TimePoint respawnTimePoint = std::chrono::time_point_cast<std::chrono::milliseconds>(Clock::from_time_t(respawnTime));
                            m_emeraldDragonsTimer = std::chrono::duration_cast<std::chrono::milliseconds>(respawnTimePoint - curTime).count();
                        }
                        else m_emeraldDragonsTimer = 0;
                    }
                    else
                    {
                        m_emeraldDragonsState = 0xF;
                        m_emeraldDragonsTimer = 0;
                    }
                    break;
                }
                case SAVE_ID_AHN_QIRAJ: // TODO:
                    break;
                case SAVE_ID_QUEL_DANAS: // TODO:
                    break;
                case SAVE_ID_HIGHLORD_KRUUL:
                {
                    auto curTime = World::GetCurrentClockTime();
                    uint64 respawnTime;
                    if (data.size())
                    {
                        loadStream >> m_highlordKruulTimer >> respawnTime;
                        if (respawnTime)
                        {
                            TimePoint respawnTimePoint = std::chrono::time_point_cast<std::chrono::milliseconds>(Clock::from_time_t(respawnTime));
                            m_highlordKruulTimer = std::chrono::duration_cast<std::chrono::milliseconds>(respawnTimePoint - curTime).count();
                            m_highlordKruulSpawned = true;
                        }
                        else m_highlordKruulTimer = 0;
                    }
                    else
                    {
                        m_highlordKruulSpawned = false;
                        m_highlordKruulTimer = 0;
                    }
                    break;
                }
                case SAVE_ID_EXPANSION_RELEASE:
                    if (data.size())
                    {
                        uint32 expansion; // need to read as number not as character
                        loadStream >> expansion;
                        m_expansion = expansion;
                    }
                    else
                        m_expansion = sWorld.getConfig(CONFIG_UINT32_EXPANSION);
                    break;
                case SAVE_ID_LOVE_IS_IN_THE_AIR:
                    if (data.size())
                    {
                        try
                        {
                            for (uint32 i = 0; i < LOVE_LEADER_MAX; ++i)
                                loadStream >> m_loveIsInTheAirData.counters[i];
                        }
                        catch (std::exception & e)
                        {
                            sLog.outError("%s", e.what());
                            memset(m_loveIsInTheAirData.counters, 0, sizeof(LoveIsInTheAir));
                        }
                    }
                    else
                        memset(m_loveIsInTheAirData.counters, 0, sizeof(LoveIsInTheAir));
                    break;
            }
        }
        while (result->NextRow());
    }
    RespawnEmeraldDragons();
    StartExpansionEvent();
}

void WorldState::Save(SaveIds saveId)
{
    switch (saveId)
    {
        case SAVE_ID_EMERALD_DRAGONS:
        {
            uint64 time;
            if (m_emeraldDragonsTimer)
            {
                auto curTime = World::GetCurrentClockTime();
                auto respawnTime = std::chrono::milliseconds(m_emeraldDragonsTimer) + curTime;
                time = uint64(Clock::to_time_t(respawnTime));
            }
            else time = 0;
            std::string dragonsData = std::to_string(m_emeraldDragonsState) + " " + std::to_string(time);
            for (uint32 i = 0; i < 4; ++i)
                dragonsData += " " + std::to_string(m_emeraldDragonsChosenPositions[i]);
            CharacterDatabase.PExecute("DELETE FROM world_state WHERE Id='%u'", SAVE_ID_EMERALD_DRAGONS);
            CharacterDatabase.PExecute("INSERT INTO world_state(Id,Data) VALUES('%u','%s')", SAVE_ID_EMERALD_DRAGONS, dragonsData.data());
            break;
        }
        case SAVE_ID_EXPANSION_RELEASE:
        {
            std::string expansionData = std::to_string(m_expansion);
            SaveHelper(expansionData, SAVE_ID_EXPANSION_RELEASE);
            break;
        }
        // TODO: Add saving for AQ and QD
        case SAVE_ID_AHN_QIRAJ:
        case SAVE_ID_QUEL_DANAS:
            break;
        case SAVE_ID_LOVE_IS_IN_THE_AIR:
        {
            std::string loveData;
            for (uint32 i = 0; i < LOVE_LEADER_MAX; ++i)
            {
                if (i != 0)
                    loveData += " ";
                loveData += std::to_string(m_loveIsInTheAirData.counters[i]);
            }
            SaveHelper(loveData, SAVE_ID_LOVE_IS_IN_THE_AIR);
            break;
        }
    }
}

void WorldState::SaveHelper(std::string& stringToSave, SaveIds saveId)
{
    CharacterDatabase.PExecute("DELETE FROM world_state WHERE Id='%u'", saveId);
    CharacterDatabase.PExecute("INSERT INTO world_state(Id,Data) VALUES('%u','%s')", saveId, stringToSave.data());
}

void WorldState::HandleGameObjectUse(GameObject* go, Unit* user)
{
    switch (go->GetEntry())
    {
        case OBJECT_MAGTHERIDONS_HEAD:
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            if (Player* player = dynamic_cast<Player*>(user))
            {
                if (player->GetTeam() == HORDE)
                {
                    m_isMagtheridonHeadSpawnedHorde = true;
                    m_guidMagtheridonHeadHorde = go->GetObjectGuid();
                    BuffMagtheridonTeam(HORDE);
                }
                else
                {
                    m_isMagtheridonHeadSpawnedAlliance = true;
                    m_guidMagtheridonHeadAlliance = go->GetObjectGuid();
                    BuffMagtheridonTeam(ALLIANCE);
                }
            }
            break;
        }
        case OBJECT_EVENT_TRAP_THRALL:
        {
            HandleExternalEvent(CUSTOM_EVENT_LOVE_IS_IN_THE_AIR_LEADER, LOVE_LEADER_THRALL);
            SendWorldstateUpdate(m_loveIsInTheAirMutex, GetLoveIsInTheAirCounter(LOVE_LEADER_THRALL), WORLD_STATE_LOVE_IS_IN_THE_AIR_THRALL);
            uint32 hordeSum = GetLoveIsInTheAirCounter(LOVE_LEADER_CAIRNE) + GetLoveIsInTheAirCounter(LOVE_LEADER_THRALL) + GetLoveIsInTheAirCounter(LOVE_LEADER_SYLVANAS);
            SendWorldstateUpdate(m_loveIsInTheAirMutex, hordeSum, WORLD_STATE_LOVE_IS_IN_THE_AIR_TOTAL_HORDE);
            break;
        }
        case OBJECT_EVENT_TRAP_CAIRNE:
        {
            HandleExternalEvent(CUSTOM_EVENT_LOVE_IS_IN_THE_AIR_LEADER, LOVE_LEADER_CAIRNE);
            SendWorldstateUpdate(m_loveIsInTheAirMutex, GetLoveIsInTheAirCounter(LOVE_LEADER_CAIRNE), WORLD_STATE_LOVE_IS_IN_THE_AIR_CAIRNE);
            uint32 hordeSum = GetLoveIsInTheAirCounter(LOVE_LEADER_CAIRNE) + GetLoveIsInTheAirCounter(LOVE_LEADER_THRALL) + GetLoveIsInTheAirCounter(LOVE_LEADER_SYLVANAS);
            SendWorldstateUpdate(m_loveIsInTheAirMutex, hordeSum, WORLD_STATE_LOVE_IS_IN_THE_AIR_TOTAL_HORDE);
            break;
        }
        case OBJECT_EVENT_TRAP_SYLVANAS:
        {
            HandleExternalEvent(CUSTOM_EVENT_LOVE_IS_IN_THE_AIR_LEADER, LOVE_LEADER_SYLVANAS);
            SendWorldstateUpdate(m_loveIsInTheAirMutex, GetLoveIsInTheAirCounter(LOVE_LEADER_SYLVANAS), WORLD_STATE_LOVE_IS_IN_THE_AIR_SYLVANAS);
            uint32 hordeSum = GetLoveIsInTheAirCounter(LOVE_LEADER_CAIRNE) + GetLoveIsInTheAirCounter(LOVE_LEADER_THRALL) + GetLoveIsInTheAirCounter(LOVE_LEADER_SYLVANAS);
            SendWorldstateUpdate(m_loveIsInTheAirMutex, hordeSum, WORLD_STATE_LOVE_IS_IN_THE_AIR_TOTAL_HORDE);
            break;
        }
        case OBJECT_EVENT_TRAP_BOLVAR:
        {
            HandleExternalEvent(CUSTOM_EVENT_LOVE_IS_IN_THE_AIR_LEADER, LOVE_LEADER_BOLVAR);
            SendWorldstateUpdate(m_loveIsInTheAirMutex, GetLoveIsInTheAirCounter(LOVE_LEADER_BOLVAR), WORLD_STATE_LOVE_IS_IN_THE_AIR_BOLVAR);
            uint32 allianceSum = GetLoveIsInTheAirCounter(LOVE_LEADER_BOLVAR) + GetLoveIsInTheAirCounter(LOVE_LEADER_TYRANDE) + GetLoveIsInTheAirCounter(LOVE_LEADER_MAGNI);
            SendWorldstateUpdate(m_loveIsInTheAirMutex, allianceSum, WORLD_STATE_LOVE_IS_IN_THE_AIR_TOTAL_ALLIANCE);
            break;
        }
        case OBJECT_EVENT_TRAP_MAGNI:
        {
            HandleExternalEvent(CUSTOM_EVENT_LOVE_IS_IN_THE_AIR_LEADER, LOVE_LEADER_MAGNI);
            SendWorldstateUpdate(m_loveIsInTheAirMutex, GetLoveIsInTheAirCounter(LOVE_LEADER_MAGNI), WORLD_STATE_LOVE_IS_IN_THE_AIR_MAGNI);
            uint32 allianceSum = GetLoveIsInTheAirCounter(LOVE_LEADER_BOLVAR) + GetLoveIsInTheAirCounter(LOVE_LEADER_TYRANDE) + GetLoveIsInTheAirCounter(LOVE_LEADER_MAGNI);
            SendWorldstateUpdate(m_loveIsInTheAirMutex, allianceSum, WORLD_STATE_LOVE_IS_IN_THE_AIR_TOTAL_ALLIANCE);
            break;
        }
        case OBJECT_EVENT_TRAP_TYRANDE:
        {
            HandleExternalEvent(CUSTOM_EVENT_LOVE_IS_IN_THE_AIR_LEADER, LOVE_LEADER_TYRANDE);
            SendWorldstateUpdate(m_loveIsInTheAirMutex, GetLoveIsInTheAirCounter(LOVE_LEADER_TYRANDE), WORLD_STATE_LOVE_IS_IN_THE_AIR_TYRANDE);
            uint32 allianceSum = GetLoveIsInTheAirCounter(LOVE_LEADER_BOLVAR) + GetLoveIsInTheAirCounter(LOVE_LEADER_TYRANDE) + GetLoveIsInTheAirCounter(LOVE_LEADER_MAGNI);
            SendWorldstateUpdate(m_loveIsInTheAirMutex, allianceSum, WORLD_STATE_LOVE_IS_IN_THE_AIR_TOTAL_ALLIANCE);
            break;
        }
        default:
            break;
    }
}

void WorldState::HandleGameObjectRevertState(GameObject* go)
{
    switch (go->GetEntry())
    {
        case OBJECT_MAGTHERIDONS_HEAD:
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            if (go->GetObjectGuid() == m_guidMagtheridonHeadHorde)
            {
                m_isMagtheridonHeadSpawnedHorde = false;
                m_guidMagtheridonHeadHorde = ObjectGuid();
                DispelMagtheridonTeam(HORDE);
            }
            else if (go->GetObjectGuid() == m_guidMagtheridonHeadAlliance)
            {
                m_isMagtheridonHeadSpawnedAlliance = false;
                m_guidMagtheridonHeadAlliance = ObjectGuid();
                DispelMagtheridonTeam(ALLIANCE);
            }
            break;
        }
        default:
            break;
    }
}

void WorldState::HandlePlayerEnterZone(Player* player, uint32 zoneId)
{
    switch (zoneId)
    {
        case ZONEID_STORMWIND_CITY:
        case ZONEID_DARNASSUS:
        case ZONEID_IRONFORGE:
        case ZONEID_ORGRIMMAR:
        case ZONEID_THUNDER_BLUFF:
        case ZONEID_UNDERCITY:
        {
            std::lock_guard<std::mutex> guard(m_loveIsInTheAirMutex);
            m_loveIsInTheAirCapitalsPlayers.push_back(player->GetObjectGuid());
            break;
        }
        case ZONEID_HELLFIRE_PENINSULA:
        case ZONEID_HELLFIRE_RAMPARTS:
        case ZONEID_HELLFIRE_CITADEL:
        case ZONEID_BLOOD_FURNACE:
        case ZONEID_SHATTERED_HALLS:
        case ZONEID_MAGTHERIDON_LAIR:
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            if (m_isMagtheridonHeadSpawnedAlliance && player->GetTeam() == ALLIANCE)
                player->CastSpell(player, SPELL_TROLLBANES_COMMAND, TRIGGERED_OLD_TRIGGERED);
            if (m_isMagtheridonHeadSpawnedHorde && player->GetTeam() == HORDE)
                player->CastSpell(player, SPELL_NAZGRELS_FAVOR, TRIGGERED_OLD_TRIGGERED);
            m_magtheridonHeadPlayers.push_back(player->GetObjectGuid());
            break;
        }
        case ZONEID_SHATTRATH:
        case ZONEID_BOTANICA:
        case ZONEID_MECHANAR:
        case ZONEID_ARCATRAZ:
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            if (m_adalSongOfBattleTimer)
                player->CastSpell(player, SPELL_ADAL_SONG_OF_BATTLE, TRIGGERED_OLD_TRIGGERED);
            m_adalSongOfBattlePlayers.push_back(player->GetObjectGuid());
            break;
        }
        default:
            break;
    }
}

void WorldState::HandlePlayerLeaveZone(Player* player, uint32 zoneId)
{
    switch (zoneId)
    {
        case ZONEID_STORMWIND_CITY:
        case ZONEID_DARNASSUS:
        case ZONEID_IRONFORGE:
        case ZONEID_ORGRIMMAR:
        case ZONEID_THUNDER_BLUFF:
        case ZONEID_UNDERCITY:
        {
            std::lock_guard<std::mutex> guard(m_loveIsInTheAirMutex);
            auto position = std::find(m_loveIsInTheAirCapitalsPlayers.begin(), m_loveIsInTheAirCapitalsPlayers.end(), player->GetObjectGuid());
            if (position != m_loveIsInTheAirCapitalsPlayers.end())
                m_loveIsInTheAirCapitalsPlayers.erase(position);
            break;
        }
        case ZONEID_HELLFIRE_PENINSULA:
        case ZONEID_HELLFIRE_RAMPARTS:
        case ZONEID_HELLFIRE_CITADEL:
        case ZONEID_BLOOD_FURNACE:
        case ZONEID_SHATTERED_HALLS:
        case ZONEID_MAGTHERIDON_LAIR:
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            if (player->GetTeam() == ALLIANCE)
                player->RemoveAurasDueToSpell(SPELL_TROLLBANES_COMMAND);
            if (player->GetTeam() == HORDE)
                player->RemoveAurasDueToSpell(SPELL_NAZGRELS_FAVOR);
            auto position = std::find(m_magtheridonHeadPlayers.begin(), m_magtheridonHeadPlayers.end(), player->GetObjectGuid());
            if (position != m_magtheridonHeadPlayers.end()) // == myVector.end() means the element was not found
                m_magtheridonHeadPlayers.erase(position);
            break;
        }
        case ZONEID_SHATTRATH:
        case ZONEID_BOTANICA:
        case ZONEID_MECHANAR:
        case ZONEID_ARCATRAZ:
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            player->RemoveAurasDueToSpell(SPELL_ADAL_SONG_OF_BATTLE);
            auto position = std::find(m_adalSongOfBattlePlayers.begin(), m_adalSongOfBattlePlayers.end(), player->GetObjectGuid());
            if (position != m_adalSongOfBattlePlayers.end()) // == myVector.end() means the element was not found
                m_adalSongOfBattlePlayers.erase(position);
            break;
        }
        default:
            break;
    }
}

void WorldState::HandlePlayerEnterArea(Player* player, uint32 areaId)
{
    switch (areaId)
    {
        case AREAID_SKYGUARD_OUTPOST:
        case AREAID_SHARTUUL_TRANSPORTER:
        case AREAID_DEATHS_DOOR:
        case AREAID_THERAMORE_ISLE:
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            m_areaPlayers[areaId].push_back(player->GetObjectGuid());
            break;
        }
        default: break;
    }
}

void WorldState::HandlePlayerLeaveArea(Player* player, uint32 areaId)
{
    switch (areaId)
    {
        case AREAID_SKYGUARD_OUTPOST:
        case AREAID_SHARTUUL_TRANSPORTER:
        case AREAID_DEATHS_DOOR:
        case AREAID_THERAMORE_ISLE:
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            auto position = std::find(m_areaPlayers[areaId].begin(), m_areaPlayers[areaId].end(), player->GetObjectGuid());
            if (position != m_areaPlayers[areaId].end()) // == myVector.end() means the element was not found
                m_areaPlayers[areaId].erase(position);
            break;
        }
        default: break;
    }
}

bool WorldState::IsConditionFulfilled(uint32 conditionId, uint32 state) const
{
    return m_transportStates.at(conditionId) == state;
}

void WorldState::HandleConditionStateChange(uint32 conditionId, uint32 state)
{
    m_transportStates[conditionId] = state;
}

void WorldState::BuffMagtheridonTeam(Team team)
{
    for (ObjectGuid& guid : m_magtheridonHeadPlayers)
    {
        if (Player* player = sObjectMgr.GetPlayer(guid))
        {
            if (team == ALLIANCE && player->GetTeam() == ALLIANCE)
            {
                player->GetMap()->AddMessage([guid](Map* map) -> void
                {
                    if (Player* player = map->GetPlayer(guid))
                        player->CastSpell(player, SPELL_TROLLBANES_COMMAND, TRIGGERED_OLD_TRIGGERED);
                });
            }
            if (team == HORDE && player->GetTeam() == HORDE)
            {
                player->GetMap()->AddMessage([guid](Map* map) -> void
                {
                    if (Player* player = map->GetPlayer(guid))
                        player->CastSpell(player, SPELL_NAZGRELS_FAVOR, TRIGGERED_OLD_TRIGGERED);
                });
            }
        }
    }
}

void WorldState::DispelMagtheridonTeam(Team team)
{
    for (ObjectGuid& guid : m_magtheridonHeadPlayers)
    {
        if (Player* player = sObjectMgr.GetPlayer(guid))
        {
            if (team == ALLIANCE && player->GetTeam() == ALLIANCE)
            {
                player->GetMap()->AddMessage([guid](Map* map) -> void
                {
                    if (Player* player = map->GetPlayer(guid))
                        player->RemoveAurasDueToSpell(SPELL_TROLLBANES_COMMAND);
                });
            }
            if (team == HORDE && player->GetTeam() == HORDE)
            {
                player->GetMap()->AddMessage([guid](Map* map) -> void
                {
                    if (Player* player = map->GetPlayer(guid))
                        player->RemoveAurasDueToSpell(SPELL_NAZGRELS_FAVOR);
                });
            }
        }
    }
}

void WorldState::HandleExternalEvent(uint32 eventId, uint32 param)
{
    std::lock_guard<std::mutex> guard(m_mutex);

    switch (eventId)
    {
        case CUSTOM_EVENT_YSONDRE_DIED:
        case CUSTOM_EVENT_LETHON_DIED:
        case CUSTOM_EVENT_EMERISS_DIED:
        case CUSTOM_EVENT_TAERAR_DIED:
        {
            uint32 bossId;
            switch (eventId)
            {
                case CUSTOM_EVENT_YSONDRE_DIED: bossId = 0; break;
                case CUSTOM_EVENT_LETHON_DIED:  bossId = 1; break;
                case CUSTOM_EVENT_EMERISS_DIED: bossId = 2; break;
                case CUSTOM_EVENT_TAERAR_DIED:  bossId = 3; break;
            }
            m_emeraldDragonsState |= 1 << bossId;
            if (m_emeraldDragonsState == 0xF)
                m_emeraldDragonsTimer = 30 * HOUR * IN_MILLISECONDS;
            Save(SAVE_ID_EMERALD_DRAGONS); // save to DB right away
            break;
        }
        case CUSTOM_EVENT_HIGHLORD_KRUUL_DIED:
        {
            m_highlordKruulSpawned = false;
            m_highlordKruulTimer = urand(4 * HOUR * IN_MILLISECONDS, 6 * HOUR * IN_MILLISECONDS);
            Save(SAVE_ID_HIGHLORD_KRUUL); // save to DB right away
            break;
        }
        case CUSTOM_EVENT_LOVE_IS_IN_THE_AIR_LEADER:
        {
            MANGOS_ASSERT(param < LOVE_LEADER_MAX);
            ++m_loveIsInTheAirData.counters[param];
            Save(SAVE_ID_LOVE_IS_IN_THE_AIR);
            break;
        }
        case CUSTOM_EVENT_ADALS_SONG_OF_BATTLE:
            m_adalSongOfBattleTimer = 120 * MINUTE * IN_MILLISECONDS; // Two hours duration
            BuffAdalsSongOfBattle();
            break;
    }
}

void WorldState::Update(const uint32 diff)
{
    std::lock_guard<std::mutex> guard(m_mutex);

    if (m_adalSongOfBattleTimer)
    {
        if (m_adalSongOfBattleTimer <= diff)
        {
            m_adalSongOfBattleTimer = 0;
            DispelAdalsSongOfBattle();
        }
        else m_adalSongOfBattleTimer -= diff;
    }

    if (m_emeraldDragonsTimer)
    {
        if (m_emeraldDragonsTimer <= diff)
        {
            m_emeraldDragonsTimer = 0;
            RespawnEmeraldDragons();
        }
        else m_emeraldDragonsTimer -= diff;
    }
}

void WorldState::SendWorldstateUpdate(std::mutex& mutex, uint32 value, uint32 worldStateId)
{
    std::lock_guard<std::mutex> guard(mutex);
    for (ObjectGuid& guid : m_loveIsInTheAirCapitalsPlayers)
        if (Player* player = sObjectMgr.GetPlayer(guid))
            player->SendUpdateWorldState(worldStateId, value);
}

void WorldState::SendLoveIsInTheAirWorldstateUpdate(uint32 value, uint32 worldStateId)
{
    std::lock_guard<std::mutex> guard(m_loveIsInTheAirMutex);
    for (ObjectGuid& guid : m_loveIsInTheAirCapitalsPlayers)
        if (Player* player = sObjectMgr.GetPlayer(guid))
            player->SendUpdateWorldState(worldStateId, value);
}

void WorldState::BuffAdalsSongOfBattle()
{
    for (ObjectGuid& guid : m_adalSongOfBattlePlayers)
    {
        if (Player* player = sObjectMgr.GetPlayer(guid))
        {
            player->GetMap()->AddMessage([guid](Map* map) -> void
            {
                if (Player* player = map->GetPlayer(guid))
                    player->CastSpell(player, SPELL_ADAL_SONG_OF_BATTLE, TRIGGERED_OLD_TRIGGERED);
            });
        }
    }
}

void WorldState::DispelAdalsSongOfBattle()
{
    for (ObjectGuid& guid : m_adalSongOfBattlePlayers)
    {
        if (Player* player = sObjectMgr.GetPlayer(guid))
        {
            player->GetMap()->AddMessage([guid](Map* map) -> void
            {
                if (Player* player = map->GetPlayer(guid))
                    player->RemoveAurasDueToSpell(SPELL_ADAL_SONG_OF_BATTLE);
            });
        }
    }
}

void WorldState::ExecuteOnAreaPlayers(uint32 areaId, std::function<void(Player*)> executor)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    for (ObjectGuid guid : m_areaPlayers[areaId])
        if (Player* player = sObjectMgr.GetPlayer(guid))
            executor(player);
}

// Emerald Dragons
enum EmeraldDragons : uint32
{
    NPC_YSONDRE = 14887,
    NPC_LETHON  = 14888,
    NPC_EMERISS = 14889,
    NPC_TAERAR  = 14890,
};

static float emeraldDragonSpawns[4][4] =
{
    {-10428.8f, -392.176f, 43.7411f, 0.932375f},
    {753.619f, -4012.f, 94.043f, 3.193f},
    {-2872.66f, 1884.25f, 52.7336f, 2.6529f},
    {3301.05f, -3732.57f, 173.544f, 2.9147f}
};

static uint32 pathIds[4] =
{
    1,0,0,0
};

bool WorldState::IsDragonSpawned(uint32 entry)
{
    return ((1 << (entry - NPC_YSONDRE)) & m_emeraldDragonsState) == 0;
}

void WorldState::RespawnEmeraldDragons()
{
    if (m_emeraldDragonsState == 0xF)
    {
        std::vector<uint32> ids = { NPC_YSONDRE, NPC_LETHON, NPC_EMERISS, NPC_TAERAR };
        for (uint32 i = 3; i > 0; --i)
        {
            uint32 random = urand(0, i);
            m_emeraldDragonsChosenPositions[3 - i] = ids[random];
            ids.erase(std::remove(ids.begin(), ids.end(), ids[random]), ids.end());
        }
        m_emeraldDragonsChosenPositions[3] = ids[0];
        m_emeraldDragonsState = 0;
        Save(SAVE_ID_EMERALD_DRAGONS);
    }
    sMapMgr.DoForAllMapsWithMapId(0, [&](Map* map)
    {
        if (IsDragonSpawned(m_emeraldDragonsChosenPositions[0]))
        {
            if (Creature* duskwoodDragon = WorldObject::SummonCreature(TempSpawnSettings(nullptr, m_emeraldDragonsChosenPositions[0], emeraldDragonSpawns[0][0], emeraldDragonSpawns[0][1], emeraldDragonSpawns[0][2], emeraldDragonSpawns[0][3], TEMPSPAWN_DEAD_DESPAWN, 0, false, false, pathIds[0]), map, 1))
                duskwoodDragon->GetMotionMaster()->MoveWaypoint(pathIds[0]);
        }
        if (IsDragonSpawned(m_emeraldDragonsChosenPositions[1]))
            WorldObject::SummonCreature(TempSpawnSettings(nullptr, m_emeraldDragonsChosenPositions[1], emeraldDragonSpawns[1][0], emeraldDragonSpawns[1][1], emeraldDragonSpawns[1][2], emeraldDragonSpawns[1][3], TEMPSPAWN_DEAD_DESPAWN, 0, false, false, pathIds[1]), map, 1);
    });
    sMapMgr.DoForAllMapsWithMapId(1, [&](Map* map)
    {
        if (IsDragonSpawned(m_emeraldDragonsChosenPositions[2]))
            WorldObject::SummonCreature(TempSpawnSettings(nullptr, m_emeraldDragonsChosenPositions[2], emeraldDragonSpawns[2][0], emeraldDragonSpawns[2][1], emeraldDragonSpawns[2][2], emeraldDragonSpawns[2][3], TEMPSPAWN_DEAD_DESPAWN, 0, false, false, pathIds[2]), map, 1);
        if (IsDragonSpawned(m_emeraldDragonsChosenPositions[3]))
            WorldObject::SummonCreature(TempSpawnSettings(nullptr, m_emeraldDragonsChosenPositions[3], emeraldDragonSpawns[3][0], emeraldDragonSpawns[3][1], emeraldDragonSpawns[3][2], emeraldDragonSpawns[3][3], TEMPSPAWN_DEAD_DESPAWN, 0, false, false, pathIds[3]), map, 1);
    });
}

// Highlord Kruul
enum HighlordKruul : uint32
{
    NPC_HIGHLORD_KRUUL = 18338
};

static float highlordKruulSpawns[10][4] =
{
    { 1975.47f, -137.368f, 32.5316f, 1.20898f },            // Undercity
    { -9550.13f, -126.712f, 57.4972f, 1.34809f },           // Stormwind
    { -5319.087f, -482.139f, 388.332f, 5.831f },            // Ironforge
    { -14737.6f, 499.647f, 3.48665f, 5.45968f },            // Booty Bay / Stranglethorn Vale
    { -234.531f, -2585.4f, 119.897f, 4.77144f },            // Hinterlands
    { 2197.334961f, -4684.198242f, 76.044106f, 0.937313f },  // Eastern Plaguelands
    { -6668.74f, -1533.09f, 243.164f, 2.00132f },           // Searing Gorge
    { 912.479f, -4502.01f, 7.34687f, 0.256955f },           // Orgrimmar
    { 2785.4f, -3823.48f, 84.2639f, 4.5062f },               // Azshara
    { 6443.186f, -3904.882f, 668.369f, 0.937313f },         // Winterpsring
};

void WorldState::RespawnHighlordKruul()
{
    if (!m_highlordKruulSpawned)
    {
        m_highlordKruulChosenPosition = urand(0, 9);
        m_highlordKruulSpawned = true;
        Save(SAVE_ID_HIGHLORD_KRUUL);
    }

    uint8 mapId = m_highlordKruulChosenPosition <= 6 ? 0 : 1;
    sMapMgr.DoForAllMapsWithMapId(mapId, [&](Map* map)
    {
        WorldObject::SummonCreature(TempSpawnSettings(nullptr, NPC_HIGHLORD_KRUUL, highlordKruulSpawns[m_highlordKruulChosenPosition][0], highlordKruulSpawns[m_highlordKruulChosenPosition][1], highlordKruulSpawns[m_highlordKruulChosenPosition][2], highlordKruulSpawns[m_highlordKruulChosenPosition][3], TEMPSPAWN_DEAD_DESPAWN, 0, true, false, m_highlordKruulChosenPosition), map, 1);
    });
}

bool WorldState::SetExpansion(uint8 expansion)
{
    if (expansion > EXPANSION_WOTLK)
        return false;

    m_expansion = expansion;
    if (expansion == EXPANSION_NONE)
        sGameEventMgr.StartEvent(GAME_EVENT_BEFORE_THE_STORM);
    else
        sGameEventMgr.StopEvent(GAME_EVENT_BEFORE_THE_STORM);
    if (m_expansion == EXPANSION_TBC)
        sGameEventMgr.StartEvent(GAME_EVENT_ECHOES_OF_DOOM);
    else
        sGameEventMgr.StopEvent(GAME_EVENT_ECHOES_OF_DOOM);
    sWorld.UpdateSessionExpansion(expansion);
    Save(SAVE_ID_EXPANSION_RELEASE); // save to DB right away
    return true;
}

void WorldState::StartExpansionEvent()
{
    if (m_expansion == EXPANSION_NONE)
    {
        sGameEventMgr.StartEvent(GAME_EVENT_BEFORE_THE_STORM);
        RespawnHighlordKruul();
    }
    if (m_expansion == EXPANSION_TBC)
        sGameEventMgr.StartEvent(GAME_EVENT_ECHOES_OF_DOOM);
}

void WorldState::FillInitialWorldStates(ByteBuffer& data, uint32& count, uint32 zoneId, uint32 areaId)
{
    if (sGameEventMgr.IsActiveHoliday(HOLIDAY_LOVE_IS_IN_THE_AIR))
    {
        switch (zoneId)
        {
            case ZONEID_STORMWIND_CITY:
            case ZONEID_DARNASSUS:
            case ZONEID_IRONFORGE:
            case ZONEID_ORGRIMMAR:
            case ZONEID_THUNDER_BLUFF:
            case ZONEID_UNDERCITY:
            {
                if (sGameEventMgr.IsActiveHoliday(HOLIDAY_LOVE_IS_IN_THE_AIR))
                {
                    uint32 allianceSum = GetLoveIsInTheAirCounter(LOVE_LEADER_BOLVAR) + GetLoveIsInTheAirCounter(LOVE_LEADER_TYRANDE) + GetLoveIsInTheAirCounter(LOVE_LEADER_MAGNI);
                    uint32 hordeSum = GetLoveIsInTheAirCounter(LOVE_LEADER_CAIRNE) + GetLoveIsInTheAirCounter(LOVE_LEADER_THRALL) + GetLoveIsInTheAirCounter(LOVE_LEADER_SYLVANAS);
                    FillInitialWorldStateData(data, count, WORLD_STATE_LOVE_IS_IN_THE_AIR_BOLVAR, GetLoveIsInTheAirCounter(LOVE_LEADER_BOLVAR));
                    FillInitialWorldStateData(data, count, WORLD_STATE_LOVE_IS_IN_THE_AIR_TYRANDE, GetLoveIsInTheAirCounter(LOVE_LEADER_TYRANDE));
                    FillInitialWorldStateData(data, count, WORLD_STATE_LOVE_IS_IN_THE_AIR_MAGNI, GetLoveIsInTheAirCounter(LOVE_LEADER_MAGNI));
                    FillInitialWorldStateData(data, count, WORLD_STATE_LOVE_IS_IN_THE_AIR_TOTAL_ALLIANCE, allianceSum);
                    FillInitialWorldStateData(data, count, WORLD_STATE_LOVE_IS_IN_THE_AIR_CAIRNE, GetLoveIsInTheAirCounter(LOVE_LEADER_CAIRNE));
                    FillInitialWorldStateData(data, count, WORLD_STATE_LOVE_IS_IN_THE_AIR_THRALL, GetLoveIsInTheAirCounter(LOVE_LEADER_THRALL));
                    FillInitialWorldStateData(data, count, WORLD_STATE_LOVE_IS_IN_THE_AIR_SYLVANAS, GetLoveIsInTheAirCounter(LOVE_LEADER_SYLVANAS));
                    FillInitialWorldStateData(data, count, WORLD_STATE_LOVE_IS_IN_THE_AIR_TOTAL_HORDE, hordeSum);
                }
                break;
            }
        }
    }
}
