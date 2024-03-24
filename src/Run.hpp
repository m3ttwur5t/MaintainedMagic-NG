#pragma once

#include "Bimap.h"
#include <SimpleIni.h>

namespace MAINT
{
	void Purge();
	void LoadSavegameMapping(const std::string&);

	RE::SpellItem* Maintainify(RE::SpellItem* const& theSpell, RE::ConcreteFormFactory<RE::SpellItem, RE::FormType::Spell>* spellFactory);
	bool IsMaintainable(RE::SpellItem* const& theSpell, RE::Actor* const& theCaster);

	void ForceMaintainedSpellUpdate(RE::Actor* const&);
	void AwardPlayerExperience(RE::PlayerCharacter* const& player);
	void BuildActiveSpellsCache();
	void MaintainSpell(RE::SpellItem* const& baseSpell, RE::Actor* const& theCaster);
	void StoreSavegameMapping(const std::string&);
	void CheckUpkeepValidity(RE::Actor* const&);

	const auto lexical_cast_hex_to_int(const std::string& hex_string)
	{
		if (hex_string.substr(0, 2) != "0x") {
			throw std::invalid_argument("Input string is not a valid hexadecimal format (should start with '0x').");
		}

		std::istringstream iss(hex_string.substr(2));
		uint32_t result;
		if (!(iss >> std::hex >> result)) {
			throw std::invalid_argument("Failed to convert hexadecimal string to integer.");
		}
		return static_cast<RE::FormID>(result);
	}
	float lexical_cast_float(const std::string& float_string)
	{
		std::istringstream iss(float_string);
		float result;
		if (!(iss >> result)) {
			throw std::invalid_argument("Failed to convert string to float.");
		}
		return result;
	}

	namespace CONFIG
	{
		class Plugin
		{
		private:
			const std::string iniPath = "Data/SKSE/Plugins/MaintainedMagicNG.ini";
			CSimpleIniA Ini;

			Plugin()
			{
				Ini.SetUnicode();
				Ini.LoadFile(iniPath.c_str());
			}

		public:
			static Plugin& GetSingleton()
			{
				static Plugin instance;
				return instance;
			}

			bool HasKey(const std::string& section, const std::string& key)
			{
				return Ini.KeyExists(section.c_str(), key.c_str());
			}

			bool HasSection(const std::string& section)
			{
				return Ini.SectionExists(section.c_str());
			}

			const std::vector<std::pair<std::string, std::string>> GetAllKeyValuePairs(const std::string& section) const
			{
				std::vector<std::pair<std::string, std::string>> ret;
				CSimpleIniA::TNamesDepend keys;
				Ini.GetAllKeys(section.c_str(), keys);
				for (auto& key : keys) {
					auto val = Ini.GetValue(section.c_str(), key.pItem);
					ret.push_back({ key.pItem, val });
				}

				return ret;
			}

			void DeleteSection(const std::string& section)
			{
				Ini.Delete(section.c_str(), nullptr, true);
			}

			void DeleteKey(const std::string& section, const std::string& key)
			{
				Ini.Delete(section.c_str(), key.c_str());
			}

			std::string GetValue(const std::string& section, const std::string& key)
			{
				return Ini.GetValue(section.c_str(), key.c_str());
			}

			long GetLongValue(const std::string& section, const std::string& key)
			{
				return Ini.GetLongValue(section.c_str(), key.c_str());
			}

			bool GetBoolValue(const std::string& section, const std::string& key)
			{
				return Ini.GetBoolValue(section.c_str(), key.c_str());
			}

			void SetValue(const std::string& section, const std::string& key, const std::string& value, const std::string& comment = std::string())
			{
				Ini.SetValue(section.c_str(), key.c_str(), value.c_str(), comment.length() > 0 ? comment.c_str() : (const char*)0);
			}

			void SetBoolValue(const std::string& section, const std::string& key, const bool value, const std::string& comment = std::string())
			{
				Ini.SetBoolValue(section.c_str(), key.c_str(), value, comment.length() > 0 ? comment.c_str() : (const char*)0);
			}

			void SetLongValue(const std::string& section, const std::string& key, const long value, const std::string& comment = std::string())
			{
				Ini.SetLongValue(section.c_str(), key.c_str(), value, comment.length() > 0 ? comment.c_str() : (const char*)0);
			}

			void Save()
			{
				Ini.SaveFile(iniPath.c_str());
			}

			Plugin(Plugin const&) = delete;
			void operator=(Plugin const&) = delete;
		};
	}

	namespace CACHE
	{
		inline BiMap<RE::SpellItem*, RE::SpellItem*> SpellToMaintainedSpell;
		inline BiMap<RE::SpellItem*, std::tuple<RE::SpellItem*, float, float>> ActiveMaintainedSpells;
		inline BiMap<RE::SpellItem*, float> MaintSpellToCost;
	}

	class FORMS
	{
	public:
		static const RE::FormID FORMID_OFFSET_BASE = 0xFF080000;
		RE::FormID CurrentOffset;

		void SetOffset(RE::FormID offset)
		{
			CurrentOffset = offset;
		}
		void LoadOffset(const CONFIG::Plugin& config, const std::string& saveFile)
		{
			RE::FormID off = 0x0;
			for (const auto& [k, v] : config.GetAllKeyValuePairs(std::format("MAP:{}", saveFile))) {
				RE::FormID cur = lexical_cast_hex_to_int(v);
				if (cur > off)
					off = cur;
			}
			off &= ~FORMID_OFFSET_BASE;
			CurrentOffset = off;
			logger::info("Local OFFSET: 0x{:08X}", CurrentOffset);
			logger::info("Global OFFSET: 0x{:08X}", FORMID_OFFSET_BASE);
		}
		RE::FormID NextFormID() const
		{
			static RE::FormID current = 0;
			return FORMID_OFFSET_BASE + (++current) + CurrentOffset;
		}
		static FORMS& GetSingleton()
		{
			static FORMS instance;
			return instance;
		}

		//RE::BGSEquipSlot* EquipSlotRight = RE::TESForm::LookupByID<RE::BGSEquipSlot>(0x13F42);
		RE::BGSEquipSlot* EquipSlotVoice = RE::TESForm::LookupByID<RE::BGSEquipSlot>(0x25BEE);
		RE::BGSKeyword* KywdMaintainedSpell = RE::TESDataHandler::GetSingleton()->LookupForm<RE::BGSKeyword>(0x801, "MaintainedMagic.esp"sv);
		RE::BGSKeyword* KywdExcludeFromSystem = RE::TESDataHandler::GetSingleton()->LookupForm<RE::BGSKeyword>(0x80A, "MaintainedMagic.esp"sv);
		RE::SpellItem* SpelMagickaDebuffTemplate = RE::TESDataHandler::GetSingleton()->LookupForm<RE::SpellItem>(0x802, "MaintainedMagic.esp"sv);
		RE::TESGlobal* GlobMaintainModeEnabled = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESGlobal>(0x805, "MaintainedMagic.esp"sv);
		RE::BGSListForm* FlstMaintainedSpellToggle = RE::TESDataHandler::GetSingleton()->LookupForm<RE::BGSListForm>(0x80B, "MaintainedMagic.esp"sv);
		RE::SpellItem* SpelMindCrush = RE::TESDataHandler::GetSingleton()->LookupForm<RE::SpellItem>(0x80D, "MaintainedMagic.esp"sv);

	private:
		// Private constructor to prevent instantiation
		FORMS() {}

		// Delete copy constructor and assignment operator to prevent cloning
		FORMS(const FORMS&) = delete;
		FORMS& operator=(const FORMS&) = delete;
	};

	class UpdatePCHook
	{
	public:
		static void Install()
		{
			REL::Relocation<std::uintptr_t> pcVTable{ RE::VTABLE_PlayerCharacter[0] };
			UpdatePC = pcVTable.write_vfunc(0xAD, UpdatePCMod);
		}

		static void ResetEffCheckTimer()
		{
			TimerActiveEffCheck = 0.0f;
		}

	private:
		UpdatePCHook()
		{
			TimerActiveEffCheck = TimerExperienceAward = 0.0f;
		}

		static void UpdatePCMod(RE::PlayerCharacter* pc, float delta)
		{
			UpdatePC(pc, delta);
			TimerActiveEffCheck += delta;
			TimerExperienceAward += delta;
			if (TimerActiveEffCheck >= 2.5f) {
				MAINT::ForceMaintainedSpellUpdate(pc);
				MAINT::CheckUpkeepValidity(pc);
				TimerActiveEffCheck = 0.0f;
			}
			if (TimerExperienceAward >= 300) {
				MAINT::AwardPlayerExperience(pc);
				TimerExperienceAward = 0.0f;
			}
		}

		static inline REL::Relocation<decltype(UpdatePCMod)> UpdatePC;

		static inline std::atomic<float> TimerActiveEffCheck;
		static inline std::atomic<float> TimerExperienceAward;
	};
}
