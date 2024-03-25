#include "Run.hpp"

namespace MAINT
{
	void BuildActiveSpellsCache()
	{
		logger::info("RebuildActiveMaintainedSpellCache()");
		static const auto& player = RE::PlayerCharacter::GetSingleton();
		if (player == nullptr) {
			logger::error("\tPlayer is NULL");
			return;
		}
		static auto& magDrainSpell = MAINT::FORMS::GetSingleton().SpelMagickaDebuffTemplate;
		uint32_t totalMag = 0;
		for (const auto& maintSpell : player->GetActorRuntimeData().addedSpells) {
			if (const auto& baseSpell = MAINT::CACHE::SpellToMaintainedSpell.getKeyOrNull(maintSpell); baseSpell != nullptr) {
				const auto& maintCost = MAINT::CACHE::MaintSpellToCost.getValue(maintSpell);
				MAINT::CACHE::ActiveMaintainedSpells.insert(baseSpell, { maintSpell, baseSpell->effects[0]->effectItem.magnitude, maintCost });
				totalMag += static_cast<uint32_t>(maintCost);
				MAINT::FORMS::GetSingleton().FlstMaintainedSpellToggle->AddForm(baseSpell);
				logger::info("\tPostLoad Cache: {}, Cost: {}", maintSpell->GetName(), maintCost);
			}
		}
		magDrainSpell->effects[0]->effectItem.magnitude = static_cast<float>(totalMag);
		logger::info("\tPostLoad MaintSpell total MAG {}", magDrainSpell->effects[0]->effectItem.magnitude);
	}

	void Purge()
	{
		for (const auto& [k, v] : MAINT::CACHE::SpellToMaintainedSpell.GetForwardMap())
			v->SetDelete(true);
		MAINT::CACHE::SpellToMaintainedSpell.clear();
		MAINT::CACHE::ActiveMaintainedSpells.clear();
	}

	void LoadSavegameMapping(const std::string& identifier)
	{
		logger::info("LoadSavegameMapping({})", identifier);
		constexpr auto getPluginNameWithLocalID = [](const std::string& part) -> std::pair<std::string, RE::FormID> {
			std::size_t tildePos = part.find("~");

			if (tildePos == std::string::npos)
				return { std::string(), lexical_cast_hex_to_int(part) };

			std::string pluginName = part.substr(0, tildePos);
			auto localFormID = lexical_cast_hex_to_int(part.substr(tildePos + 1));
			return { pluginName, localFormID };
		};
		constexpr auto getSpellIDWithCost = [](const std::string& part) -> std::pair<RE::FormID, float> {
			std::size_t tildePos = part.find("~");

			if (tildePos == std::string::npos)
				return { 0x0, lexical_cast_float(part) };

			RE::FormID spellFormID = lexical_cast_hex_to_int(part.substr(0, tildePos));
			auto upkeepCost = lexical_cast_float(part.substr(tildePos + 1));
			return { spellFormID, upkeepCost };
		};

		const auto& dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler) {
			logger::error("\tFailed to fetch TESDataHandler!");
			return;
		}

		const auto& spellFactory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::SpellItem>();
		if (!spellFactory) {
			logger::error("\tFailed to fetch IFormFactory: SPEL!");
			return;
		}

		const auto subSection = std::format("MAP:{}", identifier);
		auto& ini = MAINT::CONFIG::Plugin::GetSingleton();
		for (const auto& [k, v] : ini.GetAllKeyValuePairs(subSection)) {
			const auto& [plugin, formid] = getPluginNameWithLocalID(k);
			const auto& [maintSpellFormID, maintSpellCost] = getSpellIDWithCost(v);

			const auto& baseSpell = dataHandler->LookupForm<RE::SpellItem>(formid, plugin);
			if (!baseSpell)
				continue;

			const auto& infSpell = Maintainify(baseSpell, spellFactory);
			if (!infSpell) {
				logger::error("\tFailed to create Maintained Spell: {}", baseSpell->GetName());
				return;
			}

			if (infSpell->GetFormID() != maintSpellFormID) {
				bool deleteOld = false;
				if (auto existingEntry = RE::TESForm::LookupByID<RE::TESForm>(maintSpellFormID); existingEntry && existingEntry->GetFormID() != infSpell->formID) {
					if (const auto& sameSpellMaybe = existingEntry->As<RE::SpellItem>(); sameSpellMaybe && std::equal(std::begin(sameSpellMaybe->effects), std::end(sameSpellMaybe->effects), std::begin(infSpell->effects))) {
						logger::info("\tID IN USE But spell matches. Assume game was reloaded.");
						deleteOld = true;
					} else {
						logger::warn("\tID IN USE BY {} ({})! Swapping... 0x{:08X} <=> 0x{:08X}", existingEntry->GetName(), RE::FormTypeToString(existingEntry->GetFormType()), existingEntry->GetFormID(), infSpell->GetFormID());
						existingEntry->SetFormID(infSpell->formID, false);
					}
				}
				infSpell->SetFormID(maintSpellFormID, false);
			}
			MAINT::CACHE::SpellToMaintainedSpell.insert(baseSpell, infSpell);
			MAINT::CACHE::MaintSpellToCost.insert(infSpell, maintSpellCost);
		}
	}

	static auto CalculateUpkeepCost(RE::SpellItem* const& baseSpell, RE::Actor* const& theCaster)
	{
		const auto& baseCost = baseSpell->CalculateMagickaCost(theCaster);
		const auto& baseDuration = max(static_cast<uint32_t>(1u), baseSpell->effects[0]->GetDuration());
		const auto& mult = baseDuration < 60.0f ? powf(60.0f / baseDuration, 2.0f) : 60.0f / baseDuration;
		return roundf(baseCost * mult);
	}

	void MaintainSpell(RE::SpellItem* const& baseSpell, RE::Actor* const& theCaster)
	{
		logger::info("MaintainSpell({}, 0x{:08X})", baseSpell->GetName(), baseSpell->GetFormID());

		if (!IsMaintainable(baseSpell, theCaster)) {
			RE::DebugNotification(std::format("Cannot maintain {}.", baseSpell->GetName()).c_str());
			return;
		}
		auto infSpell = MAINT::CACHE::SpellToMaintainedSpell.getValueOrNull(baseSpell);

		if (!infSpell) {
			static const auto& spellFactory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::SpellItem>();
			if (!spellFactory) {
				logger::error("\tFailed to fetch IFormFactory: SPEL!");
				return;
			}
			infSpell = Maintainify(baseSpell, spellFactory);
			if (infSpell) {
				MAINT::CACHE::SpellToMaintainedSpell.insert(baseSpell, infSpell);
			} else {
				logger::error("\tFailed to create Maintained Spell: {}", baseSpell->GetName());
				return;
			}
		}

		if (theCaster->HasSpell(infSpell)) {
			logger::info("\tActor already has constant version of {}", baseSpell->GetName());
			return;
		}

		const auto& baseCost = baseSpell->CalculateMagickaCost(theCaster);
		const auto& magCost = CalculateUpkeepCost(baseSpell, theCaster);
		if (magCost > theCaster->AsActorValueOwner()->GetActorValue(RE::ActorValue::kMagicka) + baseCost) {
			RE::DebugNotification(std::format("Need {} Magicka to maintain {}.", static_cast<uint32_t>(magCost), baseSpell->GetName()).c_str());
			return;
		}

		logger::info("\tRemoving Base Effect of {}", baseSpell->GetName());
		auto handle = theCaster->GetHandle();
		theCaster->AsMagicTarget()->DispelEffect(baseSpell, handle);
		theCaster->AsActorValueOwner()->RestoreActorValue(RE::ACTOR_VALUE_MODIFIERS::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kMagicka, baseCost);

		static auto& magDrainSpell = MAINT::FORMS::GetSingleton().SpelMagickaDebuffTemplate;
		magDrainSpell->effects[0]->effectItem.magnitude += magCost;

		const float magnitude = baseSpell->effects[0]->effectItem.magnitude;

		logger::info("\tAdding Constant Effect with Maintain Cost of {}", magCost);
		theCaster->AddSpell(infSpell);
		theCaster->RemoveSpell(magDrainSpell);
		theCaster->AddSpell(magDrainSpell);

		MAINT::CACHE::ActiveMaintainedSpells.insert(baseSpell, { infSpell, magnitude, magCost });
		MAINT::FORMS::GetSingleton().FlstMaintainedSpellToggle->AddForm(baseSpell);
		RE::DebugNotification(std::format("Maintaining {} for {} Magicka.", baseSpell->GetName(), static_cast<uint32_t>(magCost)).c_str());
	}

	void StoreSavegameMapping(const std::string& identifier)
	{
		static auto& ini = MAINT::CONFIG::Plugin::GetSingleton();
		const auto subSection = std::format("MAP:{}", identifier);
		ini.DeleteSection(subSection);
		for (const auto& [baseSpell, maintData] : MAINT::CACHE::ActiveMaintainedSpells.GetForwardMap()) {
			const auto& [maintSpell, origMagnitude, maintCost] = maintData;
			auto keyString = std::format("{}~0x{:08X}", baseSpell->GetFile(0)->GetFilename(), baseSpell->GetLocalFormID());
			auto rightHandSide = std::format("0x{:08X}~{}", maintSpell->GetFormID(), maintCost);
			ini.SetValue(subSection,
				keyString,
				rightHandSide,
				std::format("# {}", baseSpell->GetName()));
		}
		ini.Save();
	}

	void AwardPlayerExperience(RE::PlayerCharacter* const& player)
	{
		for (const auto& [baseSpell, _] : MAINT::CACHE::ActiveMaintainedSpells.GetForwardMap()) {
			const auto& baseCost = baseSpell->CalculateMagickaCost(nullptr);
			player->AddSkillExperience(baseSpell->GetAssociatedSkill(), baseCost);
			//RE::ConsoleLog::GetSingleton()->Print(std::format("[MaintainedMagic] Awarded {} EXP for maintaining {}", baseCost, baseSpell->GetName()).c_str());
		}
	}

	void CheckUpkeepValidity(RE::Actor* const& theActor)
	{
		if (MAINT::CACHE::ActiveMaintainedSpells.GetForwardMap().size() == 0)
			return;

		const auto& av = theActor->AsActorValueOwner()->GetActorValue(RE::ActorValue::kMagicka);
		if (av >= 0)
			return;

		const auto& mindCrush = MAINT::FORMS::GetSingleton().SpelMindCrush;
		const static auto& magDrainSpell = MAINT::FORMS::GetSingleton().SpelMagickaDebuffTemplate;
		const auto& debuffStrength = magDrainSpell->effects.front()->effectItem.magnitude;
		//RE::ConsoleLog::GetSingleton()->Print(std::format("[MaintainedMagic] Broke upkeep. Penalty: {}", debuffStrength).c_str());
		theActor->GetMagicCaster(RE::MagicSystem::CastingSource::kLeftHand)->CastSpellImmediate(mindCrush, false, theActor, 1.0, true, debuffStrength, nullptr);
	}

	void ForceMaintainedSpellUpdate(RE::Actor* const& theActor)
	{
		if (MAINT::CACHE::ActiveMaintainedSpells.GetForwardMap().size() == 0)
			return;
		static auto& magDrainSpell = MAINT::FORMS::GetSingleton().SpelMagickaDebuffTemplate;

		static auto& ini = MAINT::CONFIG::Plugin::GetSingleton();

		std::vector<std::pair<RE::SpellItem*, RE::SpellItem*>> toRemove;
		for (const auto& pair : MAINT::CACHE::ActiveMaintainedSpells.GetForwardMap()) {
			const auto& baseSpell = pair.first;
			const auto& [maintSpell, maintMagnitude, maintCost] = pair.second;
			const auto& maintEffect = maintSpell->effects[0]->baseEffect;

			bool notFound = true;
			const auto& effList = theActor->AsMagicTarget()->GetActiveEffectList();
			for (const auto& e : *effList) {
				if (e->effect->baseEffect != maintEffect) {
					continue;
				}
				notFound = false;

				auto isActive = !e->flags.any(RE::ActiveEffect::Flag::kInactive, RE::ActiveEffect::Flag::kDispelled);
				const auto& magFail = e->magnitude >= 0.0f ? static_cast<int>(e->magnitude) < static_cast<int>(maintMagnitude) : false;
				const auto& durFail = e->duration >= 0.0f ? static_cast<uint32_t>(e->duration) != 0 : false;
				if (!isActive || magFail || durFail) {
					toRemove.emplace_back(std::make_pair(baseSpell, maintSpell));
					break;
				}
			}
			if (notFound) {
				logger::info("{} Not found.", maintSpell->GetName());
				toRemove.emplace_back(std::make_pair(baseSpell, maintSpell));
			}
		}
		if (toRemove.size() != 0) {
			for (const auto& [baseSpell, maintSpell] : toRemove) {
				logger::info("Dispelling missing {} (0x{:08X})", maintSpell->GetName(), maintSpell->GetFormID());
				const auto& [_, baseMagnitude, maintCost] = MAINT::CACHE::ActiveMaintainedSpells.getValue(baseSpell);

				theActor->RemoveSpell(maintSpell);
				RE::DebugNotification(std::format("{} is no longer being maintained.", baseSpell->GetName()).c_str());

				magDrainSpell->effects[0]->effectItem.magnitude -= maintCost;

				MAINT::CACHE::ActiveMaintainedSpells.eraseKey(baseSpell);
			}
			theActor->RemoveSpell(magDrainSpell);
			if (magDrainSpell->effects[0]->effectItem.magnitude > 0)
				theActor->AddSpell(magDrainSpell);
		}
		MAINT::FORMS::GetSingleton().FlstMaintainedSpellToggle->ClearData();
		for (const auto& [spl, data] : MAINT::CACHE::ActiveMaintainedSpells.GetForwardMap())
			MAINT::FORMS::GetSingleton().FlstMaintainedSpellToggle->AddForm(spl);
	}

	RE::SpellItem* Maintainify(RE::SpellItem* const& theSpell, RE::ConcreteFormFactory<RE::SpellItem, RE::FormType::Spell>* spellFactory)
	{
		const auto& fileString = theSpell->GetFile(0) ? theSpell->GetFile(0)->GetFilename() : "VIRTUAL";
		logger::info("Maintainify({}, 0x{:08X}~{})", theSpell->GetName(), theSpell->GetFormID(), fileString);

		auto infiniteSpell = spellFactory->Create();
		infiniteSpell->SetFormID(MAINT::FORMS::GetSingleton().NextFormID(), false);

		infiniteSpell->fullName = std::format("Maintained {}", theSpell->GetFullName());

		infiniteSpell->data = RE::SpellItem::Data{ theSpell->data };

		infiniteSpell->avEffectSetting = theSpell->avEffectSetting;
		infiniteSpell->boundData = theSpell->boundData;
		infiniteSpell->descriptionText = theSpell->descriptionText;

		infiniteSpell->equipSlot = MAINT::FORMS::GetSingleton().EquipSlotVoice;

		infiniteSpell->data.spellType = RE::MagicSystem::SpellType::kAbility;
		infiniteSpell->SetDelivery(RE::MagicSystem::Delivery::kSelf);
		infiniteSpell->SetCastingType(RE::MagicSystem::CastingType::kConstantEffect);

		for (uint32_t i = 0; i < theSpell->numKeywords; ++i)
			infiniteSpell->AddKeyword(theSpell->GetKeywordAt(i).value());
		infiniteSpell->AddKeyword(MAINT::FORMS::GetSingleton().KywdMaintainedSpell);

		for (const auto& eff : theSpell->effects)
			infiniteSpell->effects.emplace_back(eff);

		return infiniteSpell;
	}
	bool IsMaintainable(RE::SpellItem* const& theSpell, RE::Actor* const& theCaster)
	{
		if (theSpell->As<RE::ScrollItem>()) {
			logger::info("Spell is Scroll");
			return false;
		}
		if (theSpell->As<RE::EnchantmentItem>()) {
			logger::info("Spell is Enchantment");
			return false;
		}
		if (theSpell->effects.empty()) {
			logger::info("Spell has no effects");
			return false;
		}
		if (theSpell->data.castingType != RE::MagicSystem::CastingType::kFireAndForget) {
			logger::info("Spell is not FF");
			return false;
		}
		if (theSpell->effects[0]->GetDuration() <= 5.0) {
			logger::info("Spell has duration of 5 seconds or less");
			return false;
		}
		if (theSpell->CalculateMagickaCost(theCaster) <= 5.0 && theSpell->CalculateMagickaCost(nullptr) <= 5.0) {
			logger::info("Spell has cost of 5 or less");
			return false;
		}

		if (theSpell->HasKeyword(MAINT::FORMS::GetSingleton().KywdMaintainedSpell)) {
			logger::info("Spell has Maintained keyword");
			return false;
		}
		if (theSpell->HasKeyword(MAINT::FORMS::GetSingleton().KywdExcludeFromSystem)) {
			logger::info("Spell has exclusion keyword");
			return false;
		}
		if (theSpell->HasKeywordString("_m3HealerDummySpell")) {
			logger::info("Spell has Allylink keyword");
			return false;
		}
		const auto& archetype = theSpell->effects[0]->baseEffect->GetArchetype();
		if (theSpell->data.delivery != RE::MagicSystem::Delivery::kSelf) {
			if (archetype == RE::EffectSetting::Archetype::kSummonCreature)
				return true;
			logger::info("Spell does not target self, and is not summon");
			return false;
		}
		if (archetype == RE::EffectSetting::Archetype::kBoundWeapon) {
			logger::info("Spell is bound weapon");
			return false;
		}
		return true;
	}
}

class SpellCastEventHandler : public RE::BSTEventSink<RE::TESSpellCastEvent>
{
public:
	virtual RE::BSEventNotifyControl ProcessEvent(const RE::TESSpellCastEvent* a_event, RE::BSTEventSource<RE::TESSpellCastEvent>*)
	{
		if (a_event == nullptr || a_event->object == nullptr)
			return RE::BSEventNotifyControl::kContinue;

		const auto& theCaster = a_event->object->As<RE::Actor>();
		if (theCaster != RE::PlayerCharacter::GetSingleton())
			return RE::BSEventNotifyControl::kContinue;

		if (static_cast<short>(MAINT::FORMS::GetSingleton().GlobMaintainModeEnabled->value) == 0)
			return RE::BSEventNotifyControl::kContinue;

		if (const auto& theSpell = RE::TESForm::LookupByID<RE::SpellItem>(a_event->spell)) {
			MAINT::MaintainSpell(theSpell, theCaster);
			MAINT::UpdatePCHook::ResetEffCheckTimer();
		}
		return RE::BSEventNotifyControl::kContinue;
	}

	static SpellCastEventHandler& GetSingleton()
	{
		static SpellCastEventHandler singleton;
		return singleton;
	}
};

void OnInit(SKSE::MessagingInterface::Message* const a_msg)
{
	switch (a_msg->type) {
	case SKSE::MessagingInterface::kDataLoaded:
		break;
	case SKSE::MessagingInterface::kPreLoadGame:
	case SKSE::MessagingInterface::kNewGame:
		if (a_msg->dataLen > 0) {
			char* charData = static_cast<char*>(a_msg->data);
			std::string saveFile(charData, a_msg->dataLen);
			logger::info("Load : {}", saveFile);
			MAINT::Purge();
			MAINT::FORMS::GetSingleton().LoadOffset(MAINT::CONFIG::Plugin::GetSingleton(), saveFile);
			MAINT::LoadSavegameMapping(saveFile);
		}
		break;
	case SKSE::MessagingInterface::kPostLoadGame:
		MAINT::BuildActiveSpellsCache();
		break;
	case SKSE::MessagingInterface::kSaveGame:
		if (a_msg->dataLen > 0) {
			char* charData = static_cast<char*>(a_msg->data);
			std::string saveFile(charData, a_msg->dataLen);
			std::string saveFileWithExt = std::format("{}.ess", saveFile);
			logger::info("Save : {}", saveFileWithExt);
			MAINT::StoreSavegameMapping(saveFileWithExt);
		}
		break;
	default:
		break;
	}
}

bool Load()
{
	auto& eventProcessor = SpellCastEventHandler::GetSingleton();
	RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink<RE::TESSpellCastEvent>(&eventProcessor);

	MAINT::UpdatePCHook::Install();
	return true;
}