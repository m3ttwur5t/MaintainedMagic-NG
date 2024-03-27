#include "Run.hpp"

namespace MAINT
{
	static bool IsMaintainable(RE::SpellItem* const& theSpell, RE::Actor* const& theCaster)
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

	static RE::SpellItem* CreateMaintainSpell(RE::SpellItem* const& theSpell)
	{
		static auto const& spellFactory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::SpellItem>();
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

	static RE::SpellItem* CreateDebuffSpell(RE::SpellItem* const& theSpell, float const& magnitude)
	{
		static auto const& spellFactory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::SpellItem>();
		const auto& fileString = theSpell->GetFile(0) ? theSpell->GetFile(0)->GetFilename() : "VIRTUAL";
		logger::info("Debuffify({}, 0x{:08X}~{})", theSpell->GetName(), theSpell->GetFormID(), fileString);

		auto debuffSpell = spellFactory->Create();
		debuffSpell->SetFormID(MAINT::FORMS::GetSingleton().NextFormID(), false);

		debuffSpell->fullName = std::format("Maintained {}", theSpell->GetFullName());

		debuffSpell->data = RE::SpellItem::Data{ theSpell->data };

		debuffSpell->avEffectSetting = theSpell->avEffectSetting;
		debuffSpell->boundData = theSpell->boundData;
		debuffSpell->descriptionText = theSpell->descriptionText;

		debuffSpell->equipSlot = MAINT::FORMS::GetSingleton().EquipSlotVoice;

		debuffSpell->data.spellType = RE::MagicSystem::SpellType::kAbility;
		debuffSpell->SetDelivery(RE::MagicSystem::Delivery::kSelf);
		debuffSpell->SetCastingType(RE::MagicSystem::CastingType::kConstantEffect);

		debuffSpell->AddKeyword(MAINT::FORMS::GetSingleton().KywdMaintainedSpell);
		debuffSpell->effects.emplace_back(MAINT::FORMS::GetSingleton().SpelMagickaDebuffTemplate->effects.front());
		debuffSpell->effects.back()->effectItem.magnitude = magnitude;

		return debuffSpell;
	}

	static void BuildActiveSpellsCache()
	{
		logger::info("BuildActiveSpellsCache()");
		static const auto& player = RE::PlayerCharacter::GetSingleton();
		if (player == nullptr) {
			logger::error("\tPlayer is NULL");
			return;
		}
		for (const auto& playerSpell : player->GetActorRuntimeData().addedSpells) {
			if (!MAINT::CACHE::SpellToMaintainedSpell.containsKey(playerSpell))
				continue;
			MAINT::FORMS::GetSingleton().FlstMaintainedSpellToggle->AddForm(playerSpell);
		}

		auto const& activeEffects = player->AsMagicTarget()->GetActiveEffectList();
		for (auto const& [baseSpell, maintData] : MAINT::CACHE::SpellToMaintainedSpell.GetForwardMap()) {
			auto const& [maintSpell, debuffSpell] = maintData;
			for (auto const& aeff : *activeEffects) {
				if (aeff->spell == debuffSpell && aeff->GetCasterActor().get() == player && aeff->effect == debuffSpell->effects.front()) {
					logger::info("Restoring {} magnitude to {}", debuffSpell->GetName(), abs(aeff->GetMagnitude()));
					debuffSpell->effects.front()->effectItem.magnitude = abs(aeff->GetMagnitude());
					break;
				}
			}
		}
	}

	static void Purge()
	{
		logger::info("Purge()");
		for (const auto& [k, v] : MAINT::CACHE::SpellToMaintainedSpell.GetForwardMap()) {
			const auto& [maintSpell, debuffSpell] = v;
			maintSpell->SetDelete(true);
			debuffSpell->SetDelete(true);
		}
		MAINT::FORMS::GetSingleton().FlstMaintainedSpellToggle->ClearData();
		MAINT::CACHE::SpellToMaintainedSpell.clear();
	}

	static void LoadSavegameMapping(const std::string& identifier)
	{
		logger::info("LoadSavegameMapping({})", identifier);
		constexpr auto getPluginNameWithLocalID = [](const std::string& part) -> std::pair<std::string, RE::FormID> {
			std::size_t tildePos = part.find("~");

			if (tildePos == std::string::npos)
				return { std::string(), 0x0 };

			std::string pluginName = part.substr(0, tildePos);
			auto localFormID = lexical_cast_hex_to_formid(part.substr(tildePos + 1));
			return { pluginName, localFormID };
		};
		constexpr auto getSpellIDWithDebuffID = [](const std::string& part) -> std::pair<RE::FormID, RE::FormID> {
			std::size_t tildePos = part.find("~");

			if (tildePos == std::string::npos)
				return { 0x0, 0x0 };
			try {
				auto spellFormID = lexical_cast_hex_to_formid(part.substr(0, tildePos));
				auto debuffFormID = lexical_cast_hex_to_formid(part.substr(tildePos + 1));
				return { spellFormID, debuffFormID };
			} catch (...) {
				return { 0x0, 0x0 };
			}
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
			const auto& [maintSpellFormID, debuffSpellFormID] = getSpellIDWithDebuffID(v);

			const auto& baseSpell = dataHandler->LookupForm<RE::SpellItem>(formid, plugin);
			if (!baseSpell)
				continue;

			const auto& infSpell = CreateMaintainSpell(baseSpell);
			if (!infSpell) {
				logger::error("\tFailed to create Maintained Spell: {}", baseSpell->GetName());
				return;
			}
			if (maintSpellFormID != 0x0)
				infSpell->SetFormID(maintSpellFormID, false);

			const auto& debuffSpell = CreateDebuffSpell(baseSpell, 0.0f);
			if (!debuffSpell) {
				logger::error("\tFailed to create Maintained Spell: {}", baseSpell->GetName());
				return;
			}
			if (debuffSpellFormID != 0x0)
				debuffSpell->SetFormID(debuffSpellFormID, false);
			MAINT::CACHE::SpellToMaintainedSpell.insert(baseSpell, { infSpell, debuffSpell });
		}
	}

	static auto CalculateUpkeepCost(RE::SpellItem* const& baseSpell, RE::Actor* const& theCaster)
	{
		
		logger::info("CalculateUpkeepCost()");
		const auto& baseCost = baseSpell->CalculateMagickaCost(theCaster);
		const auto& baseDuration = max(static_cast<uint32_t>(1u), baseSpell->effects.front()->GetDuration());
		auto mult = baseDuration < NEUTRAL_DURATION ? powf(NEUTRAL_DURATION / baseDuration, 2.0f) : sqrt(NEUTRAL_DURATION / baseDuration);

		float baseDur = static_cast<float>(baseSpell->effects.front()->GetDuration());
		for (const auto& aeff : *theCaster->AsMagicTarget()->GetActiveEffectList()) {
			if (aeff->spell == baseSpell && aeff->GetCasterActor().get() == theCaster && aeff->effect == baseSpell->effects.front()) {
				const auto& durmult = sqrt(baseDur / aeff->duration);
				mult *= durmult;
				logger::info("NeutralDur {} vs BaseDur {} vs RealDur {} => Cost Mult: {}%", NEUTRAL_DURATION, baseDur, aeff->duration, mult);
				break;
			}
		}

		return roundf(baseCost * mult);
	}

	static void MaintainSpell(RE::SpellItem* const& baseSpell, RE::Actor* const& theCaster)
	{
		logger::info("MaintainSpell({}, 0x{:08X})", baseSpell->GetName(), baseSpell->GetFormID());

		if (!IsMaintainable(baseSpell, theCaster)) {
			RE::DebugNotification(std::format("Cannot maintain {}.", baseSpell->GetName()).c_str());
			return;
		}

		const auto& baseCost = baseSpell->CalculateMagickaCost(theCaster);
		auto magCost = CalculateUpkeepCost(baseSpell, theCaster);

		if (magCost > theCaster->AsActorValueOwner()->GetActorValue(RE::ActorValue::kMagicka) + baseCost) {
			RE::DebugNotification(std::format("Need {} Magicka to maintain {}.", static_cast<uint32_t>(magCost), baseSpell->GetName()).c_str());
			return;
		}

		if (MAINT::CACHE::SpellToMaintainedSpell.containsKey(baseSpell)) {
			logger::info("\tActor already has constant version of {}", baseSpell->GetName());
			return;
		}

		const auto& maintSpell = CreateMaintainSpell(baseSpell);
		const auto& debuffSpell = CreateDebuffSpell(baseSpell, magCost);

		logger::info("\tRemoving Base Effect of {}", baseSpell->GetName());
		auto handle = theCaster->GetHandle();
		theCaster->AsMagicTarget()->DispelEffect(baseSpell, handle);
		theCaster->AsActorValueOwner()->RestoreActorValue(RE::ACTOR_VALUE_MODIFIERS::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kMagicka, baseCost);

		logger::info("\tAdding Constant Effect with Maintain Cost of {}", magCost);
		theCaster->AddSpell(maintSpell);
		theCaster->AddSpell(debuffSpell);
		MAINT::CACHE::SpellToMaintainedSpell.insert(baseSpell, { maintSpell, debuffSpell });

		MAINT::FORMS::GetSingleton().FlstMaintainedSpellToggle->AddForm(baseSpell);
		RE::DebugNotification(std::format("Maintaining {} for {} Magicka.", baseSpell->GetName(), static_cast<uint32_t>(magCost)).c_str());
	}

	static void StoreSavegameMapping(const std::string& identifier)
	{
		logger::info("StoreSavegameMapping({})", identifier);
		static auto& ini = MAINT::CONFIG::Plugin::GetSingleton();
		const auto subSection = std::format("MAP:{}", identifier);
		ini.DeleteSection(subSection);
		for (const auto& [baseSpell, maintData] : MAINT::CACHE::SpellToMaintainedSpell.GetForwardMap()) {
			const auto& [maintSpell, debuffSpell] = maintData;
			auto keyString = std::format("{}~0x{:08X}", baseSpell->GetFile(0)->GetFilename(), baseSpell->GetLocalFormID());
			auto rightHandSide = std::format("0x{:08X}~0x{:08X}", maintSpell->GetFormID(), debuffSpell->GetFormID());
			ini.SetValue(subSection,
				keyString,
				rightHandSide,
				std::format("# {}", baseSpell->GetName()));
		}
		ini.Save();
	}

	void AwardPlayerExperience(RE::PlayerCharacter* const& player)
	{
		for (const auto& [baseSpell, _] : MAINT::CACHE::SpellToMaintainedSpell.GetForwardMap()) {
			const auto& baseCost = baseSpell->CalculateMagickaCost(nullptr);
			player->AddSkillExperience(baseSpell->GetAssociatedSkill(), baseCost);
		}
	}

	void CheckUpkeepValidity(RE::Actor* const& theActor)
	{
		if (MAINT::CACHE::SpellToMaintainedSpell.empty()) {
			return;
		}

		const auto& av = theActor->AsActorValueOwner()->GetActorValue(RE::ActorValue::kMagicka);
		if (av >= 0) {
			return;
		}

		const auto& map = MAINT::CACHE::SpellToMaintainedSpell.GetForwardMap();
		const auto totalMagDrain = std::accumulate(map.begin(), map.end(), 0.0f,
			[&](float current, const std::pair<RE::SpellItem*, MAINT::CACHE::MaintainedSpell>& maintSpell) {
				const auto& [baseSpell, maintSpellData] = maintSpell;
				const auto& [infSpell, debuffSpell] = maintSpellData;
				return current + debuffSpell->effects.front()->GetMagnitude();
			});

		const auto& mindCrush = MAINT::FORMS::GetSingleton().SpelMindCrush;
		theActor->GetMagicCaster(RE::MagicSystem::CastingSource::kLeftHand)->CastSpellImmediate(mindCrush, false, theActor, 1.0, true, totalMagDrain, nullptr);
	}

	void ForceMaintainedSpellUpdate(RE::Actor* const& theActor)
	{
		if (MAINT::CACHE::SpellToMaintainedSpell.empty())
			return;

		std::vector<std::pair<RE::SpellItem*, MAINT::CACHE::MaintainedSpell>> toRemove;
		for (const auto& [baseSpell, maintainedSpellPair] : MAINT::CACHE::SpellToMaintainedSpell.GetForwardMap()) {
			const auto& [maintSpell, debuffSpell] = maintainedSpellPair;
			const auto& maintEffect = maintSpell->effects.front()->baseEffect;

			bool notFound = true;
			const auto& effList = theActor->AsMagicTarget()->GetActiveEffectList();
			for (const auto& e : *effList) {
				if (e->effect->baseEffect != maintEffect) {
					continue;
				}
				notFound = false;

				auto const& maintMagnitude = maintSpell->effects.front()->effectItem.magnitude;
				auto isActive = !e->flags.any(RE::ActiveEffect::Flag::kInactive, RE::ActiveEffect::Flag::kDispelled);
				const auto& magFail = e->magnitude >= 0.0f ? static_cast<int>(e->magnitude) < static_cast<int>(maintMagnitude) : false;
				const auto& durFail = e->duration >= 0.0f ? static_cast<uint32_t>(e->duration) != 0 : false;
				if (!isActive || magFail || durFail) {
					toRemove.emplace_back(std::make_pair(baseSpell, std::make_pair(maintSpell, debuffSpell)));
					break;
				}
			}
			if (notFound) {
				logger::info("{} Not found.", maintSpell->GetName());
				toRemove.emplace_back(std::make_pair(baseSpell, std::make_pair(maintSpell, debuffSpell)));
			}
		}
		if (!toRemove.empty()) {
			for (const auto& [baseSpell, maintSpellPair] : toRemove) {
				const auto& [maintSpell, debuffSpell] = maintSpellPair;
				logger::info("Dispelling missing {} (0x{:08X})", maintSpell->GetName(), maintSpell->GetFormID());

				theActor->RemoveSpell(maintSpell);
				theActor->RemoveSpell(debuffSpell);
				RE::DebugNotification(std::format("{} is no longer being maintained.", baseSpell->GetName()).c_str());

				MAINT::CACHE::SpellToMaintainedSpell.eraseKey(baseSpell);
			}
		}
		MAINT::FORMS::GetSingleton().FlstMaintainedSpellToggle->ClearData();
		for (const auto& [spl, _] : MAINT::CACHE::SpellToMaintainedSpell.GetForwardMap())
			MAINT::FORMS::GetSingleton().FlstMaintainedSpellToggle->AddForm(spl);
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
	static void Install()
	{
		auto& eventProcessor = SpellCastEventHandler::GetSingleton();
		RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink<RE::TESSpellCastEvent>(&eventProcessor);
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
	SpellCastEventHandler::Install();
	MAINT::UpdatePCHook::Install();
	return true;
}