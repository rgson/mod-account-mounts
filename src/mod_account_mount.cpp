#include "Chat.h"
#include "Config.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include <set>         // Required for std::set             -- might be redundant
#include <sstream>     // Required for std::istringstream   -- might be redundant
#include <string>      // Required for std::string          -- might be redundant
#include <unordered_map>

class AccountMounts : public PlayerScript
{
    bool limitrace; // Boolean to hold limit race option
    bool limitRidingSkill; // Boolean to hold the limit riding skill option
    std::set<uint32> excludedSpellIds; // Set to hold the Spell IDs to be excluded

    std::unordered_map<uint32, uint32> mountRidingSkill;

public:
    AccountMounts() : PlayerScript("AccountMounts", {
        PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_LEARN_SPELL
    })
    {
        // Retrieve limitrace option from the config file
        limitrace = sConfigMgr->GetOption<bool>("Account.Mounts.LimitRace", false);
        // Retrieve LimitRidingSkill option from config file
        limitRidingSkill = sConfigMgr->GetOption<bool>("Account.Mounts.LimitRidingSkill", false);

        // Retrieve the string of excluded Spell IDs from the config file
        std::string excludedSpellsStr = sConfigMgr->GetOption<std::string>("Account.Mounts.ExcludedSpellIDs", "");
        // Proceed only if the configuration is not "0" or empty, indicating exclusions are specified
        if (excludedSpellsStr != "0" && !excludedSpellsStr.empty())
        {
            std::istringstream spellStream(excludedSpellsStr);
            std::string spellIdStr;
            while (std::getline(spellStream, spellIdStr, ','))
            {
                uint32 spellId = static_cast<uint32>(std::stoul(spellIdStr));
                if (spellId != 0) // Ensure the spell ID is not 0, as 0 is used to indicate no exclusions
                    excludedSpellIds.insert(spellId); // Add the Spell ID to the set of exclusions
            }
        }
    }

    void OnPlayerLogin(Player* player)
    {
        TeachPlayerAccountMounts(player);
    }

    void OnPlayerLearnSpell(Player* player, uint32 spellID)
    {
        // Apprentice Riding (33388)
        // Journeyman Riding (33391)
        // Expert Riding (34090)
        // Artisan Riding (34091)
        if (limitRidingSkill && (spellID == 33388 || spellID == 33391 || spellID == 34090 || spellID == 34091))
            TeachPlayerAccountMounts(player);
    }

private:
    void TeachPlayerAccountMounts(Player* pPlayer)
    {
        if (sConfigMgr->GetOption<bool>("Account.Mounts.Enable", true))
        {
            if (sConfigMgr->GetOption<bool>("Account.Mounts.Announce", false))
                ChatHandler(pPlayer->GetSession()).SendSysMessage("This server is running the |cff4CFF00AccountMounts |rmodule.");

            if (mountRidingSkill.empty())
                BuildMountRidingSkillMap();

            uint32 playerRidingSkill = pPlayer->GetSkillValue(SKILL_RIDING);
            if (limitRidingSkill && playerRidingSkill == 0)
                return;  // No riding skill means no mounts

            std::vector<uint32> Guids;
            uint32 playerAccountID = pPlayer->GetSession()->GetAccountId();
            QueryResult result1 = CharacterDatabase.Query("SELECT `guid`, `race` FROM `characters` WHERE `account`={};", playerAccountID);

            if (!result1)
                return;

            do
            {
                Field* fields = result1->Fetch();
                uint32 race = fields[1].Get<uint8>();

                if ((Player::TeamIdForRace(race) == Player::TeamIdForRace(pPlayer->getRace())) || !limitrace)
                    Guids.push_back(fields[0].Get<uint32>());

            } while (result1->NextRow());

            std::vector<uint32> Spells;

            for (auto& i : Guids)
            {
                QueryResult result2 = CharacterDatabase.Query("SELECT `spell` FROM `character_spell` WHERE `guid`={};", i);
                if (!result2)
                    continue;

                do
                {
                    Spells.push_back(result2->Fetch()[0].Get<uint32>());
                } while (result2->NextRow());
            }

            for (auto& i : Spells)
            {
                // Check if the spell is in the excluded list before learning it
                if (excludedSpellIds.find(i) != excludedSpellIds.end())
                    continue;

                // Check if the spell is a mount
                auto sSpell = sSpellStore.LookupEntry(i);
                if (!(sSpell->Effect[0] == SPELL_EFFECT_APPLY_AURA && sSpell->EffectApplyAuraName[0] == SPELL_AURA_MOUNTED))
                    continue;

                // Check if the character has the required riding skill level
                if (limitRidingSkill && playerRidingSkill < GetRequiredRidingSkillForMount(i))
                    continue;

                // Passed all checks
                pPlayer->learnSpell(sSpell->Id);
            }
        }
    }

    void BuildMountRidingSkillMap()
    {
        // Record required riding skill level per mount
        mountRidingSkill = {
            // Class mounts
            { 5784,  75},  // Felsteed (60%)
            {23161, 150},  // Dreadsteed (100%)
            {13819,  75},  // Warhorse (60%)
            {23214, 150},  // Charger (100%)
            {34769,  75},  // Thalassian Warhorse (60%)
            {34767, 150},  // Thalassian Charger (100%)
            {48778, 150},  // Acherus Deathcharger (100%)
        };
        // Mounts learnt from items
        QueryResult result = WorldDatabase.Query(
            "SELECT spellid_2, RequiredSkillRank FROM item_template WHERE class = {} AND subclass = {} and spellid_2 > 0;",
            ITEM_CLASS_MISC,
            ITEM_SUBCLASS_JUNK_MOUNT
        );
        if (result)
        {
            do
            {
                Field* fields = result->Fetch();
                uint32 mountSpellId = fields[0].Get<uint32>();
                uint32 requiredSkillRank = fields[1].Get<uint32>();
                mountRidingSkill[mountSpellId] = requiredSkillRank;
            } while (result->NextRow());
        }
    }

    uint32 GetRequiredRidingSkillForMount(uint32 mountSpellId)
    {
        uint32 requiredRidingSkill = 300;
        auto it = mountRidingSkill.find(mountSpellId);
        if (it != mountRidingSkill.end())
            requiredRidingSkill = it->second;
        else
            LOG_WARN(
                "mod_account_mount",
                "Unknown riding skill requirement for mount with spell ID {}. Falling back to skill level {}.",
                mountSpellId,
                requiredRidingSkill
            );
        return requiredRidingSkill;
    }
};

void AddAccountMountsScripts()
{
    new AccountMounts();
}
