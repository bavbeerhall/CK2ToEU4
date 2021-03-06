#include "EU4World.h"
#include "Log.h"
#include "OSCompatibilityLayer.h"
#include <filesystem>
#include <fstream>
namespace fs = std::filesystem;
#include "../CK2World/Characters/Character.h"
#include "../CK2World/Dynasties/Dynasty.h"
#include "../CK2World/Offmaps/Offmap.h"
#include "../CK2World/Provinces/Barony.h"
#include "../CK2World/Titles/Title.h"
#include "../Configuration/Configuration.h"
#include <cmath>

EU4::World::World(const CK2::World& sourceWorld, const Configuration& theConfiguration, const mappers::VersionParser& versionParser)
{
	LOG(LogLevel::Info) << "*** Hello EU4, let's get painting. ***";
	// Scraping localizations from CK2 so we may know proper names for our countries.
	localizationMapper.scrapeLocalizations(theConfiguration);

	// Ditto for colors - these only apply on non-eu4 countries.
	colorScraper.scrapeColors(theConfiguration.getCK2Path() + "/common/landed_titles/landed_titles.txt");

	// This is our region mapper for eu4 regions, areas and superRegions. It's a pointer because we need
	// to embed it into every cultureMapper individual mapping. It works faster that way.
	regionMapper = std::make_shared<mappers::RegionMapper>();
	regionMapper->loadRegions(theConfiguration);

	// And this is the cultureMapper. It's of vital importance.
	cultureMapper.loadRegionMapper(regionMapper);

	// This is a valid province scraper. It looks at eu4 map data and notes which eu4 provinces are in fact valid.
	// ... It's not used at all.
	provinceMapper.determineValidProvinces(theConfiguration);

	// We start conversion by importing vanilla eu4 countries, history and common sections included.
	// We'll overwrite some of them with ck2 imports.
	importVanillaCountries(theConfiguration.getEU4Path(), sourceWorld.isInvasion());

	// Which happens now. Translating incoming titles into EU4 tags, with new tags being added to our countries.
	importCK2Countries(sourceWorld);

	// Now we can deal with provinces since we know to whom to assign them. We first import vanilla province data.
	// Some of it will be overwritten, but not all.
	importVanillaProvinces(theConfiguration.getEU4Path(), sourceWorld.isInvasion());

	// We can link provinces to regionMapper's bindings, though this is not used at the moment.
	regionMapper->linkProvinces(provinces);

	// Next we import ck2 provinces and translate them ontop a significant part of all imported provinces.
	importCK2Provinces(sourceWorld);

	// With Ck2 provinces linked to those eu4 provinces they affect, we can adjust eu4 province dev values.
	alterProvinceDevelopment();

	// We then link them to their respective countries. Those countries that end up with 0 provinces are defacto dead.
	linkProvincesToCountries();

	// Country capitals are fuzzy, and need checking if we assigned them to some other country during mapping.
	verifyCapitals();

	// This step is important. CK2 data is sketchy and not every character or province has culture/religion data.
	// For those, we look at vanilla provinces and override missing bits with vanilla setup. Yeah, a bit more sunni in
	// hordeland, but it's fine.
	verifyReligionsAndCultures();

	// With all provinces and rulers religion/culture set, only now can we import advisers, which also need religion/culture set.
	// Those advisers coming without such data use the monarch's religion/culture.
	importAdvisers();

	// We're onto the finesse part of conversion now. HRE was shattered in CK2 World and now we're assigning electorates, free
	// cities, and such.
	distributeHRESubtitles(theConfiguration);

	// Rulers with multiple crowns either get PU agreements, or just annex the other crowns.
	resolvePersonalUnions();

	// Vassalages and tributaries were also set in ck2 world but we have to transcribe those into EU4 agreements.
	diplomacy.importAgreements(countries, sourceWorld.getDiplomacy(), sourceWorld.getConversionDate());

	// We're distributing permanent claims according to dejure distribution.
	distributeClaims();

	// Now for the final tweaks.
	distributeForts();

	// China
	adjustChina(sourceWorld);

	// Siberia
	siberianQuestion(theConfiguration);

	// And finally, the Dump.
	LOG(LogLevel::Info) << "---> The Dump <---";
	modFile.outname = theConfiguration.getOutputName();
	output(versionParser, theConfiguration, sourceWorld.getConversionDate(), sourceWorld.isInvasion());
	LOG(LogLevel::Info) << "*** Farewell EU4, granting you independence. ***";
}

void EU4::World::distributeClaims()
{
	Log(LogLevel::Info) << "-- Distributing DeJure Claims";
	auto counter = 0;
	std::map<int, std::set<std::string>> claimsRegister; // ck2 province, eu4 tag claims

	// Mapping all countries with all their claims.
	for (const auto& country: countries)
	{
		if (country.second->getTitle().first.empty())
			continue;
		for (const auto& DJprovince: country.second->getTitle().second->getDeJureProvinces())
		{
			claimsRegister[DJprovince.first].insert(country.first);
		}
	}

	// And then rolling through provinces and applying those claims.
	for (const auto& province: provinces)
	{
		if (!province.second->getSourceProvince())
			continue;
		const auto& claimItr = claimsRegister.find(province.second->getSourceProvince()->getID());
		if (claimItr == claimsRegister.end())
			continue;
		for (const auto& tag: claimItr->second)
		{
			if (tag == province.second->getOwner())
				continue;
			// since de jure claims are based on DE JURE land, we're adding it even for PU or vassal land.
			province.second->addPermanentClaim(tag);
			counter++;
		}
	}
	Log(LogLevel::Info) << "<> " << counter << " claims have been distributed.";
}

void EU4::World::siberianQuestion(const Configuration& theConfiguration)
{
	Log(LogLevel::Info) << "-- Burning Siberia";
	if (theConfiguration.getSiberia() == ConfigurationDetails::SIBERIA::LEAVE_SIBERIA)
	{
		Log(LogLevel::Info) << ">< Leaving Siberia alone due to Configuration.";
		return;
	}

	auto counter = 0;
	// We're deleting all tags with capitals in siberia at nomad or tribal level.
	for (const auto& country: countries)
	{
		if (country.second->getGovernment() != "nomad" && country.second->getGovernment() != "tribal")
			continue;
		if (country.second->getProvinces().empty())
			continue;
		if (!country.second->getCapitalID())
			continue;
		const auto& region = regionMapper->getParentRegionName(country.second->getCapitalID());
		if (!region)
			continue;
		if (region != "west_siberia_region" && region != "east_siberia_region")
			continue;

		// All checks done. Let's get deleting.
		for (const auto& province: country.second->getProvinces())
		{
			province.second->sterilize();
		}
		country.second->clearProvinces();
		country.second->clearExcommunicated();
		diplomacy.deleteAgreementsWithTag(country.first);
		++counter;
	}
	Log(LogLevel::Info) << "<> " << counter << " countries have been...";
}


void EU4::World::adjustChina(const CK2::World& sourceWorld)
{
	Log(LogLevel::Info) << "-- Invading China";

	std::string ourChinaTag;
	// Do we have a china?

	const auto& china = sourceWorld.getOffmaps().getChina();
	if (!china)
	{
		// No china. Ok, we need to run through china provinces and update ownership from ming to whatever is our chinese default.
		const auto& chinaTag = titleTagMapper.getChinaForTitle("");
		if (!chinaTag)
		{
			Log(LogLevel::Info) << ">< No China in Savegame, no china fallback!";
			return;
		}
		ourChinaTag = *chinaTag;
	}
	else
	{
		const auto& chinaTag = titleTagMapper.getChinaForTitle(china->second->getName());
		if (!chinaTag)
		{
			Log(LogLevel::Info) << ">< Ingame " << china->second->getName() << " china not recognized, no china fallback!";
			return;
		}
		ourChinaTag = *chinaTag;
	}
	const auto& countryItr = countries.find(ourChinaTag);
	if (countryItr == countries.end())
	{
		Log(LogLevel::Warning) << ourChinaTag << " is not loaded at all!";
		return;
	}
	const auto& ourChina = countryItr->second;
	if (!ourChina)
	{
		Log(LogLevel::Warning) << ourChinaTag << " has no loaded definition!";
		return;
	}

	// Move all china provinces under new tag
	for (const auto& chineseProvince: provinceMapper.getOffmapChineseProvinces())
	{
		const auto provinceItr = provinces.find(chineseProvince);
		if (provinceItr == provinces.end())
		{
			Log(LogLevel::Warning) << "Province " << chineseProvince << " is not in fact a valid province.";
			continue;
		}
		provinceItr->second->setOwner(ourChinaTag);
		provinceItr->second->setController(ourChinaTag);
		provinceItr->second->addCore(ourChinaTag);
	}

	celestialEmperorTag = ourChinaTag;
	diplomacy.updateTagsInAgreements("MNG", ourChinaTag);

	// Find western protectorate if possible
	for (const auto& country: countries)
	{
		if (country.second->getTitle().first != "e_china_west_governor")
			continue;
		const auto& westernTag = country.first;
		// Move our diplo to China
		diplomacy.updateTagsInAgreements(westernTag, ourChinaTag);
		// Move our provinces to China
		for (const auto& province: country.second->getProvinces())
		{
			province.second->addDiscoveredBy("chinese");
			province.second->setOwner(ourChinaTag);
			province.second->setController(ourChinaTag);
			province.second->addCore(ourChinaTag);
		}
		break;
	}

	// Grab the emperor
	const auto& holder = china->second->getHolder();
	if (holder.second && holder.second->getDynasty().second)
	{
		// We have all data to set emperor.
		Character emperor;
		emperor.birthDate = holder.second->getBirthDate();
		emperor.name = holder.second->getName();
		emperor.dynasty = holder.second->getDynasty().second->getName();
		emperor.adm = 6;
		emperor.dip = 4;
		emperor.mil = 6;
		std::string baseReligion;
		if (!holder.second->getReligion().empty())
			baseReligion = holder.second->getReligion();
		if (!baseReligion.empty())
		{
			const auto& religionMatch = religionMapper.getEu4ReligionForCk2Religion(baseReligion);
			if (religionMatch)
				emperor.religion = *religionMatch;
			else
				Log(LogLevel::Warning) << "Celestial emperor has no religion from: " << baseReligion;
		}
		else
			Log(LogLevel::Warning) << "Celestial emperor could not determine base religion!";
		std::string baseCulture;
		if (!holder.second->getCulture().empty())
			baseCulture = holder.second->getCulture();
		if (!baseCulture.empty())
		{
			const auto& cultureMatch = cultureMapper.cultureMatch(baseCulture, emperor.religion, 0, ourChinaTag);
			if (cultureMatch)
				emperor.culture = *cultureMatch;
			else
				Log(LogLevel::Warning) << "Celestial emperor has no culture from: " << baseCulture;
		}
		else
			Log(LogLevel::Warning) << "Celestial emperor could not determine base culture!";
		emperor.isSet = true;
		ourChina->setConversionDate(sourceWorld.getConversionDate());
		ourChina->clearHistoryLessons();
		ourChina->setMonarch(emperor);
	}
	else
	{
		Log(LogLevel::Warning) << ">< Celestial emperor has lacking definitions!";
		return;
	}

	// A standalone support for Chinese Caliphates Mod begins here. We import variables from sourceworks defining
	// weights of religions in china (sunni = 40.0, christian = 100.0), and for a total weight of 400 we distribute
	// these religions across chinese provinces. We only do so for offmap provinces as western protectorate provinces
	// may have had their religion already changed.

	auto chineseReligions = sourceWorld.getVars().getChineseReligions();
	if (chineseReligions)
	{
		const auto provinceNum = static_cast<int>(provinceMapper.getOffmapChineseProvinces().size());
		const auto provinceWeight = 400.0 / provinceNum; // religious weight of an individual province.

		for (const auto& chineseProvince: provinceMapper.getOffmapChineseProvinces())
		{
			const auto provinceItr = provinces.find(chineseProvince);
			if (provinceItr == provinces.end())
			{
				continue;
			}
			bool provinceSet = false;
			for (const auto& religiousInfluence: *chineseReligions)
			{
				if (religiousInfluence.second > provinceWeight)
				{
					if (!religiousInfluence.second)
						continue;
					// is this a valid religion?
					const auto& targetReligion = religionMapper.getEu4ReligionForCk2Religion(religiousInfluence.first);
					if (!targetReligion)
					{
						(*chineseReligions)[religiousInfluence.first] = 0;
						continue;
					}
					provinceItr->second->setReligion(*targetReligion);
					(*chineseReligions)[religiousInfluence.first] -= provinceWeight;
					if ((*chineseReligions)[religiousInfluence.first] < provinceWeight)
						(*chineseReligions)[religiousInfluence.first] = 0;
					provinceSet = true;
					break;
				}
			}
			if (!provinceSet)
				break; // We're done.
		}
	}

	Log(LogLevel::Info) << "<> China successfully Invaded";
}

void EU4::World::verifyCapitals()
{
	Log(LogLevel::Info) << "-- Verifying All countries Have Capitals";

	auto counter = 0;
	for (const auto& country: countries)
		if (country.second->verifyCapital(provinceMapper))
			counter++;

	Log(LogLevel::Info) << "<> " << counter << " capitals have been reassigned.";
}

void EU4::World::distributeForts()
{
	Log(LogLevel::Info) << "-- Building Forts";

	// We're doing this only for our new countries. We haven't deleted forts in ROTW.
	// Countries at 4+ provinces get a capital fort. 8+ countries get one fort per area where they own 3+ provinces.

	auto counterCapital = 0;
	auto counterOther = 0;

	for (const auto& country: countries)
	{
		if (country.second->getTitle().first.empty())
			continue;
		if (country.second->getProvinces().size() < 4)
			continue; // To small to afford forts.
		if (!country.second->getCapitalID())
			continue; // Not dealing with broken countries, thank you.
		if (!country.second->getProvinces().count(country.second->getCapitalID()))
		{
			Log(LogLevel::Warning) << country.first << " has capital province set to " << country.second->getCapitalID() << "but doesn't own it?";
			continue; // this should have been fixed earlier by verifyCapitals!
		}

		const auto& capitalAreaName = regionMapper->getParentAreaName(country.second->getCapitalID());
		if (!capitalAreaName)
			continue; // uh-huh

		const auto& capitalProvince = country.second->getProvinces().find(country.second->getCapitalID());
		capitalProvince->second->buildFort();
		counterCapital++;

		if (country.second->getProvinces().size() < 8)
			continue; // Too small for more forts.

		std::set<std::string> builtAreas = {*capitalAreaName};
		// Now it gets serious. We need a list of areas with 3+ provinces.
		std::map<std::string, int> elegibleAreas;

		for (const auto& province: country.second->getProvinces())
		{
			const auto& areaName = regionMapper->getParentAreaName(province.first);
			if (!areaName)
				continue;
			elegibleAreas[*areaName]++;
		}

		// And we put a fort in every one of those.
		for (const auto& province: country.second->getProvinces())
		{
			const auto& areaName = regionMapper->getParentAreaName(province.first);
			if (!areaName)
				continue;
			if (builtAreas.count(*areaName))
				continue;
			if (elegibleAreas[*areaName] <= 2)
				continue;

			province.second->buildFort();
			counterOther++;
			builtAreas.insert(*areaName);
		}
	}

	Log(LogLevel::Info) << "<> " << counterCapital << " capital forts and " << counterOther << " other forts have been built.";
}

void EU4::World::alterProvinceDevelopment()
{
	Log(LogLevel::Info) << "-- Scaling Imported provinces";
	// For every 12 buildings in a province we assign a dev point (barony itself counts as 3).
	// We adhere to the distribution key:
	// castle: 2/3 mil 1/3 adm
	// city: dip
	// temple: adm
	// nomad, tribal: mil
	//
	// We're ignoring hospitals at this stage.

	auto totalVanillaDev = 0;
	auto totalCK2Dev = 0;
	auto counter = 0;

	for (const auto& province: provinces)
	{
		if (!province.second->getSourceProvince())
			continue;
		totalVanillaDev += province.second->getDev();
		auto adm = 0;
		auto dip = 0;
		auto mil = 0;
		const auto& baronies = province.second->getSourceProvince()->getBaronies();
		for (const auto& barony: baronies)
		{
			if (barony.second->getType().empty())
				continue;
			const auto buildingNumber = static_cast<double>(barony.second->getBuildingCount());
			if (barony.second->getType() == "tribal" || barony.second->getType() == "nomad")
			{
				mil += lround((3 + buildingNumber) / 12);
			}
			else if (barony.second->getType() == "city")
			{
				dip += lround((3 + buildingNumber) / 12);
			}
			else if (barony.second->getType() == "temple")
			{
				adm += lround((3 + buildingNumber) / 12);
			}
			else if (barony.second->getType() == "castle")
			{
				adm += lround((3 + buildingNumber) / 48); // third to adm
				mil += lround((3 + buildingNumber) / 18); // two thirds to mil
			}
		}
		province.second->setAdm(std::max(adm, 1));
		province.second->setDip(std::max(dip, 1));
		province.second->setMil(std::max(mil, 1));
		counter++;
		totalCK2Dev += province.second->getDev();
	}

	Log(LogLevel::Info) << "<> " << counter << " provinces scaled: " << totalCK2Dev << " development imported (vanilla had " << totalVanillaDev << ").";
}

void EU4::World::importAdvisers()
{
	LOG(LogLevel::Info) << "-> Importing Advisers";
	auto counter = 0;
	for (const auto& country: countries)
	{
		country.second->initializeAdvisers(religionMapper, cultureMapper);
		counter += country.second->getAdvisers().size();
	}
	LOG(LogLevel::Info) << "<> Imported " << counter << " advisers.";
}


void EU4::World::resolvePersonalUnions()
{
	const std::set<std::string> elegibleReligions = {"catholic",
		 "orthodox",
		 "nestorian",
		 "coptic",
		 "cathar",
		 "fraticelli",
		 "waldensian",
		 "lollard",
		 "bogomilist",
		 "monothelite",
		 "iconoclast",
		 "messalian",
		 "paulician",
		 "monophysite"};
	std::map<int, std::map<std::string, std::shared_ptr<Country>>> holderTitles;
	std::map<int, std::pair<std::string, std::shared_ptr<Country>>> holderPrimaryTitle;
	std::map<int, std::shared_ptr<CK2::Character>> relevantHolders;

	// We're filling the registry first.
	for (const auto& country: countries)
	{
		if (country.second->getTitle().first.empty())
			continue;
		if (!country.second->getTitle().second->getHolder().first)
			continue;
		// we have a holder.
		const auto& holder = country.second->getTitle().second->getHolder();
		holderTitles[holder.first].insert(country);
		relevantHolders.insert(holder);
		// does he have a primary title?
		if (!holder.second->getPrimaryTitle().first.empty())
		{
			if (!holder.second->getPrimaryTitle().second->getTitle().second->getEU4Tag().first.empty())
			{
				holderPrimaryTitle[holder.first] = holder.second->getPrimaryTitle().second->getTitle().second->getEU4Tag();
			}
		}
	}

	// Now let's see what we have.
	for (const auto& holderTitle: holderTitles)
	{
		if (holderTitle.second.size() <= 1)
			continue;

		// multiple crowns. What's our primary?
		auto primaryItr = holderPrimaryTitle.find(holderTitle.first);
		std::pair<std::string, std::shared_ptr<Country>> primaryTitle;
		if (primaryItr == holderPrimaryTitle.end() || primaryItr->second.second->getProvinces().empty())
		{
			// We need to find another primary title.
			auto foundPrimary = false;
			for (const auto& title: holderTitle.second)
			{
				if (!title.second->getProvinces().empty())
				{
					primaryTitle = std::pair(title.first, title.second);
					foundPrimary = true;
					break;
				}
			}
			if (!foundPrimary)
				continue; // no helping this fellow.
		}
		else
		{
			primaryTitle = primaryItr->second;
		}

		// religion
		const auto& religion = primaryTitle.second->getReligion();
		auto heathen = false;
		if (!elegibleReligions.count(religion))
			heathen = true;

		// We now have a holder, his primary, and religion. Let's resolve multiple crowns.
		if (!heathen)
		{
			auto unionCount = 0;
			for (const auto& title: holderTitle.second)
			{
				if (title.first == primaryTitle.first)
					continue;
				if (title.second->getProvinces().empty())
					continue;
				// Craft a relation. Going up to a max of 3 unions.
				if (unionCount <= 2)
				{
					diplomacy.addAgreement(std::make_shared<Agreement>(primaryTitle.first, title.first, "union", primaryTitle.second->getConversionDate()));
					++unionCount;
				}
				else
				{
					// too many unions.
					primaryTitle.second->annexCountry(title);
				}
			}
		}
		else
		{
			// heathens annex straight up.
			for (const auto& title: holderTitle.second)
			{
				if (title.first == primaryTitle.first)
					continue;
				if (title.second->getProvinces().empty())
					continue;
				// Yum.
				primaryTitle.second->annexCountry(title);
			}
		}
	}
}


void EU4::World::distributeHRESubtitles(const Configuration& theConfiguration)
{
	if (theConfiguration.getHRE() == ConfigurationDetails::I_AM_HRE::NONE)
		return;
	LOG(LogLevel::Info) << "-> Locating Emperor";
	// Emperor may or may not be set.
	for (const auto& country: countries)
		if (country.second->isHREEmperor())
		{
			emperorTag = country.first;
			break;
		}
	if (!emperorTag.empty())
	{
		Log(LogLevel::Info) << "<> Emperor is " << emperorTag;
		setFreeCities();
		setElectors();
	}
	else
		Log(LogLevel::Info) << "<> Emperor could not be found, no HRE mechanics.";
}

void EU4::World::setElectors()
{
	LOG(LogLevel::Info) << "-> Setting Electors";
	std::vector<std::pair<int, std::shared_ptr<Country>>> bishops;	  // piety-tag
	std::vector<std::pair<int, std::shared_ptr<Country>>> duchies;	  // dev-tag
	std::vector<std::pair<int, std::shared_ptr<Country>>> republics; // dev-tag
	std::vector<std::shared_ptr<Country>> electors;

	// We need to be careful about papacy and orthodox holders
	for (const auto& country: countries)
	{
		if (country.second->isinHRE())
		{
			if (country.second->getProvinces().empty())
				continue;
			const auto& holder = country.second->getTitle().second->getHolder();
			if (country.first == "PAP" || holder.second->getPrimaryTitle().first == "k_orthodox")
			{
				// override to always be elector
				bishops.emplace_back(std::pair(99999, country.second));
				continue;
			}
			// Let's shove all hre members into appropriate categories.
			if (country.second->getGovernment() == "theocracy")
			{
				bishops.emplace_back(std::pair(lround(holder.second->getPiety()), country.second));
			}
			else if (country.second->getGovernment() == "monarchy")
			{
				duchies.emplace_back(std::pair(country.second->getDevelopment(), country.second));
			}
			else if (country.second->getGovernment() == "republic")
			{
				republics.emplace_back(std::pair(country.second->getDevelopment(), country.second));
			} // skipping tribal and similar.
		}
	}

	std::sort(bishops.rbegin(), bishops.rend());
	std::sort(duchies.rbegin(), duchies.rend());
	std::sort(republics.rbegin(), republics.rend());

	for (const auto& bishop: bishops)
	{
		if (electors.size() >= 3)
			break;
		electors.emplace_back(bishop.second);
	}
	for (const auto& republic: republics)
	{
		if (electors.size() >= 3)
			break;
		electors.emplace_back(republic.second);
	}
	for (const auto& duchy: duchies)
	{
		if (electors.size() >= 7)
			break;
		electors.emplace_back(duchy.second);
	}

	for (const auto& elector: electors)
		elector->setElector();
	LOG(LogLevel::Info) << "<> There are " << electors.size() << " electors recognized.";
}

void EU4::World::setFreeCities()
{
	LOG(LogLevel::Info) << "-> Setting Free Cities";
	// How many free cities do we already have?
	auto freeCityNum = 0;
	for (const auto& country: countries)
	{
		if (country.second->isinHRE() && country.second->getGovernment() == "republic" && country.second->getProvinces().size() == 1 &&
			 country.second->getTitle().second->getGeneratedLiege().first.empty() && freeCityNum < 8)
		{
			country.second->overrideReforms("free_city");
			++freeCityNum;
		}
	}
	// Can we turn some minors into free cities?
	if (freeCityNum < 8)
	{
		for (const auto& country: countries)
		{
			if (country.second->isinHRE() && country.second->getGovernment() != "republic" && !country.second->isHREEmperor() &&
				 country.second->getGovernmentReforms().empty() && country.second->getProvinces().size() == 1 &&
				 country.second->getTitle().second->getGeneratedLiege().first.empty() && freeCityNum < 8)
			{
				if (country.first == "HAB")
					continue; // For Iohannes who is sensitive about Austria.
				// GovernmentReforms being empty ensures we're not converting special governments and targeted tags into free cities.
				country.second->overrideReforms("free_city");
				country.second->setGovernment("republic");
				++freeCityNum;
			}
		}
	}
	LOG(LogLevel::Info) << "<> There are " << freeCityNum << " free cities.";
}


void EU4::World::linkProvincesToCountries()
{
	// Some of the provinces have linked countries, but new world won't. We need to insert links both ways there.
	for (const auto& province: provinces)
	{
		if (province.second->getOwner().empty())
			continue; // this is the uncolonized case
		const auto& countryItr = countries.find(province.second->getOwner());
		if (countryItr != countries.end())
		{
			// registering owner in province
			if (province.second->getTagCountry().first.empty())
				province.second->registerTagCountry(std::pair(countryItr->first, countryItr->second));
			// registering province in owner.
			countryItr->second->registerProvince(std::pair(province.first, province.second));
		}
		else
		{
			Log(LogLevel::Warning) << "Province " << province.first << " owner " << province.second->getOwner() << " has no country!";
		}
	}
}

template <typename KeyType, typename ValueType> std::pair<KeyType, ValueType> get_max(const std::map<KeyType, ValueType>& x)
{
	using pairtype = std::pair<KeyType, ValueType>;
	return *std::max_element(x.begin(), x.end(), [](const pairtype& p1, const pairtype& p2) {
		return p1.second < p2.second;
	});
}

void EU4::World::verifyReligionsAndCultures()
{
	// We are checking every country if it lacks primary religion and culture. This is an issue for hordeland mainly.
	// For those lacking setups, we'll do a provincial census and inherit those values.
	for (const auto& country: countries)
	{
		// It's possible to get non-christian countries excommunicated through broken setups. Let's clear those immediately.
		if (country.second->isExcommunicated())
		{
			const auto& religion = country.second->getReligion();
			if (religion != "catholic" || religion != "fraticelli")
				country.second->clearExcommunicated();
		}
		// And then proceed on checking the missing boxes.
		if (!country.second->getReligion().empty() && !country.second->getPrimaryCulture().empty() && !country.second->getTechGroup().empty() &&
			 !country.second->getGFX().empty())
			continue;
		if (country.second->getProvinces().empty())
			continue; // No point.

		std::map<std::string, int> religiousCensus;
		std::map<std::string, int> culturalCensus;
		for (const auto& province: country.second->getProvinces())
		{
			if (province.second->getReligion().empty())
			{
				Log(LogLevel::Warning) << "Province " << province.first << " has no religion set!";
				continue;
			}
			if (province.second->getCulture().empty())
			{
				Log(LogLevel::Warning) << "Province " << province.first << " has no culture set!";
				continue;
			}
			religiousCensus[province.second->getReligion()] += 1;
			culturalCensus[province.second->getCulture()] += 1;
		}
		if (country.second->getPrimaryCulture().empty())
		{
			auto max = get_max(culturalCensus);
			Log(LogLevel::Debug) << country.first << " overriding blank culture with: " << max.first;
			country.second->setPrimaryCulture(max.first);
		}
		if (country.second->getReligion().empty())
		{
			auto max = get_max(religiousCensus);
			Log(LogLevel::Debug) << country.first << " overriding blank religion with: " << max.first;
			country.second->setReligion(max.first);
		}
		if (country.second->getTechGroup().empty())
		{
			const auto& techMatch = cultureMapper.getTechGroup(country.second->getPrimaryCulture());
			if (techMatch)
			{
				Log(LogLevel::Debug) << country.first << " overriding blank tech group with: " << *techMatch;
				country.second->setTechGroup(*techMatch);
			}
			else
			{
				country.second->setTechGroup("western");
				Log(LogLevel::Warning) << country.first << " could not determine technological group, substituting western!";
			}
		}
		if (country.second->getGFX().empty())
		{
			const auto& gfxMatch = cultureMapper.getGFX(country.second->getPrimaryCulture());
			if (gfxMatch)
			{
				Log(LogLevel::Debug) << country.first << " overriding blank gfx with: " << *gfxMatch;
				country.second->setTechGroup(*gfxMatch);
			}
			else
			{
				country.second->setTechGroup("westerngfx");
				Log(LogLevel::Warning) << country.first << " could not determine GFX, substituting westerngfx!";
			}
		}
	}
}

void EU4::World::importVanillaProvinces(const std::string& eu4Path, bool invasion)
{
	LOG(LogLevel::Info) << "-> Importing Vanilla Provinces";
	// ---- Loading history/provinces
	std::set<std::string> fileNames;
	Utils::GetAllFilesInFolder(eu4Path + "/history/provinces/", fileNames);
	for (const auto& fileName: fileNames)
	{
		const auto minusLoc = fileName.find(" - ");
		const auto id = std::stoi(fileName.substr(0, minusLoc));
		auto newProvince = std::make_shared<Province>(id, eu4Path + "/history/provinces/" + fileName);
		provinces.insert(std::pair(id, newProvince));
	}
	if (invasion)
	{
		Utils::GetAllFilesInFolder("configurables/sunset/history/provinces/", fileNames);
		for (const auto& fileName: fileNames)
		{
			const auto minusLoc = fileName.find(" - ");
			auto id = 0;
			if (minusLoc != std::string::npos)
			{
				id = std::stoi(fileName.substr(0, minusLoc));
			}
			else
			{
				id = std::stoi(fileName);
			}
			const auto& provinceItr = provinces.find(id);
			if (provinceItr != provinces.end())
				provinceItr->second->updateWith("configurables/sunset/history/provinces/" + fileName);
		}
	}
	LOG(LogLevel::Info) << ">> Loaded " << provinces.size() << " province definitions.";
}

void EU4::World::importCK2Countries(const CK2::World& sourceWorld)
{
	LOG(LogLevel::Info) << "-> Importing CK2 Countries";

	// countries holds all tags imported from EU4. We'll now overwrite some and
	// add new ones from ck2 titles.
	for (const auto& title: sourceWorld.getIndepTitles())
	{
		if (title.first.find("e_") != 0)
			continue;
		importCK2Country(title, sourceWorld);
	}
	for (const auto& title: sourceWorld.getIndepTitles())
	{
		if (title.first.find("k_") != 0)
			continue;
		importCK2Country(title, sourceWorld);
	}
	for (const auto& title: sourceWorld.getIndepTitles())
	{
		if (title.first.find("d_") != 0)
			continue;
		importCK2Country(title, sourceWorld);
	}
	for (const auto& title: sourceWorld.getIndepTitles())
	{
		if (title.first.find("c_") != 0)
			continue;
		importCK2Country(title, sourceWorld);
	}
	LOG(LogLevel::Info) << ">> " << countries.size() << " total countries recognized.";
}

void EU4::World::importCK2Country(const std::pair<std::string, std::shared_ptr<CK2::Title>>& title, const CK2::World& sourceWorld)
{
	// Grabbing the capital, if possible
	int eu4CapitalID = 0;
	const auto& ck2CapitalID = title.second->getHolder().second->getCapitalProvince().first;
	if (ck2CapitalID)
	{
		const auto& capitalMatch = provinceMapper.getEU4ProvinceNumbers(ck2CapitalID);
		if (!capitalMatch.empty())
			eu4CapitalID = *capitalMatch.begin();
	}

	// Mapping the title to a tag
	const auto& tag = titleTagMapper.getTagForTitle(title.first, title.second->getBaseTitle().first, eu4CapitalID);
	if (!tag)
		throw std::runtime_error("Title " + title.first + " could not be mapped!");

	// Locating appropriate existing country
	const auto& countryItr = countries.find(*tag);
	if (countryItr != countries.end())
	{
		countryItr->second->initializeFromTitle(*tag,
			 title.second,
			 governmentsMapper,
			 religionMapper,
			 cultureMapper,
			 provinceMapper,
			 colorScraper,
			 localizationMapper,
			 rulerPersonalitiesMapper,
			 sourceWorld.getConversionDate());
		title.second->registerEU4Tag(std::pair(*tag, countryItr->second));
	}
	else
	{
		// Otherwise create the country
		auto newCountry = std::make_shared<Country>();
		newCountry->initializeFromTitle(*tag,
			 title.second,
			 governmentsMapper,
			 religionMapper,
			 cultureMapper,
			 provinceMapper,
			 colorScraper,
			 localizationMapper,
			 rulerPersonalitiesMapper,
			 sourceWorld.getConversionDate());
		title.second->registerEU4Tag(std::pair(*tag, newCountry));
		countries.insert(std::pair(*tag, newCountry));
	}
}


void EU4::World::importCK2Provinces(const CK2::World& sourceWorld)
{
	LOG(LogLevel::Info) << "-> Importing CK2 Provinces";
	auto counter = 0;
	// CK2 provinces map to a subset of eu4 provinces. We'll only rewrite those we are responsible for.
	for (const auto& province: provinces)
	{
		const auto& ck2Provinces = provinceMapper.getCK2ProvinceNumbers(province.first);
		// Provinces we're not affecting will not be in this list.
		if (ck2Provinces.empty())
			continue;
		// Next, we find what province to use as its initializing source.
		const auto& sourceProvince = determineProvinceSource(ck2Provinces, sourceWorld);
		if (!sourceProvince)
		{
			Log(LogLevel::Warning) << "MISMAP into province: " << province.first;
			continue; // bailing for mismaps.
		}
		if (sourceProvince->first == -1)
		{
			province.second->sterilize(); // sterilizing wastelands
		}
		else
		{
			province.second->initializeFromCK2(sourceProvince->second, cultureMapper, religionMapper);
		}
		// And finally, initialize it.
		counter++;
	}
	LOG(LogLevel::Info) << ">> " << sourceWorld.getProvinces().size() << " CK2 provinces imported into " << counter << " EU4 provinces.";
}

void EU4::World::importVanillaCountries(const std::string& eu4Path, bool invasion)
{
	LOG(LogLevel::Info) << "-> Importing Vanilla Countries";
	// ---- Loading common/countries/
	std::ifstream eu4CountriesFile(fs::u8path(eu4Path + "/common/country_tags/00_countries.txt"));
	if (!eu4CountriesFile.is_open())
		throw std::runtime_error("Could not open " + eu4Path + "/common/country_tags/00_countries.txt!");
	loadCountriesFromSource(eu4CountriesFile, eu4Path, true);
	eu4CountriesFile.close();
	if (Utils::DoesFileExist("blankMod/output/common/country_tags/01_special_tags.txt"))
	{
		std::ifstream blankCountriesFile(fs::u8path("blankMod/output/common/country_tags/01_special_tags.txt"));
		if (!blankCountriesFile.is_open())
			throw std::runtime_error("Could not open blankMod/output/common/country_tags/01_special_tags.txt!");
		loadCountriesFromSource(blankCountriesFile, "blankMod/output/", false);
		blankCountriesFile.close();
	}
	if (invasion)
	{
		std::ifstream sunset(fs::u8path("configurables/sunset/common/country_tags/zz_countries.txt"));
		if (!sunset.is_open())
			throw std::runtime_error("Could not open configurables/sunset/common/country_tags/zz_countries.txt!");
		loadCountriesFromSource(sunset, "configurables/sunset/", true);
		sunset.close();
	}

	LOG(LogLevel::Info) << ">> Loaded " << countries.size() << " countries.";

	LOG(LogLevel::Info) << "-> Importing Vanilla Country History";
	// ---- Loading history/countries/
	std::set<std::string> fileNames;
	Utils::GetAllFilesInFolder(eu4Path + "/history/countries/", fileNames);
	for (const auto& fileName: fileNames)
	{
		auto tag = fileName.substr(0, 3);
		countries[tag]->loadHistory(eu4Path + "/history/countries/" + fileName);
	}
	// Now our special tags.
	fileNames.clear();
	Utils::GetAllFilesInFolder("blankMod/output/history/countries/", fileNames);
	for (const auto& fileName: fileNames)
	{
		auto tag = fileName.substr(0, 3);
		countries[tag]->loadHistory("blankMod/output/history/countries/" + fileName);
	}
	if (invasion)
	{
		fileNames.clear();
		Utils::GetAllFilesInFolder("configurables/sunset/history/countries/", fileNames);
		for (const auto& fileName: fileNames)
		{
			auto tag = fileName.substr(0, 3);
			countries[tag]->loadHistory("configurables/sunset/history/countries/" + fileName);
		}
	}
	LOG(LogLevel::Info) << ">> Loaded " << fileNames.size() << " history files.";
}

void EU4::World::loadCountriesFromSource(std::istream& theStream, const std::string& sourcePath, bool isVanillaSource)
{
	while (!theStream.eof())
	{
		std::string line;
		getline(theStream, line);

		if (line[0] == '#' || line.length() < 4)
			continue;
		auto tag = line.substr(0, 3);

		// All file paths are in quotes. The ones outside are commented, so we can use those as markers.
		auto quoteLoc = line.find_first_of('\"');
		auto countryLine = line.substr(quoteLoc + 1, line.length());
		quoteLoc = countryLine.find_last_of('\"');
		countryLine = countryLine.substr(0, quoteLoc);
		const auto filePath = sourcePath + "/common/" + countryLine;

		// We're soaking up all vanilla countries with all current definitions.
		const auto newCountry = std::make_shared<Country>(tag, filePath);
		if (countries.count(tag))
			countries[tag] = newCountry; // Overriding vanilla EU4 with our definitions.
		else
			countries.insert(std::make_pair(tag, newCountry));
		if (!isVanillaSource)
			specialCountryTags.insert(tag);
	}
}


std::optional<std::pair<int, std::shared_ptr<CK2::Province>>> EU4::World::determineProvinceSource(const std::vector<int>& ck2ProvinceNumbers,
	 const CK2::World& sourceWorld) const
{
	// determine ownership by province development.
	std::map<std::string, std::vector<std::shared_ptr<CK2::Province>>> theClaims; // title, offered province sources
	std::map<std::string, int> theShares;														// title, development
	std::string winner;
	auto maxDev = -1;

	for (auto ck2ProvinceID: ck2ProvinceNumbers)
	{
		const auto& ck2province = sourceWorld.getProvinces().find(ck2ProvinceID);
		if (ck2province == sourceWorld.getProvinces().end())
		{
			Log(LogLevel::Warning) << "Source province " << ck2ProvinceID << " is not in the list of known provinces!";
			continue; // Broken mapping?
		}
		auto ownerTitle = ck2province->second->getTitle().first;
		if (ownerTitle.empty())
		{
			// This is a wasteland. It means we must blank the eu4 province, no questions asked!
			return std::pair(-1, nullptr);
		}
		theClaims[ownerTitle].push_back(ck2province->second);
		theShares[ownerTitle] = lround(ck2province->second->getBuildingWeight());

		// While at it, is this province especially important? Enough so we'd sidestep regular rules?
		// Check for capital provinces
		if (ck2province->second->getTitle().second->getHolder().second->getCapitalProvince().first == ck2province->first)
		{
			// This is the someone's capital, don't assign it away if unnecessary.
			winner = ck2province->second->getTitle().first;
			maxDev = 200; // Dev can go up to 300+, so yes, assign it away if someone has overbuilt a nearby province.
		}
		// Check for a wonder. For multiple wonders, sorry, only last one will prevail.
		if (ck2province->second->getWonder().second)
		{
			// This is the someone's wonder province.
			winner = ck2province->second->getTitle().first;
			maxDev = 500;
		}
		// Check for HRE emperor
		if (ck2province->second->getTitle().second->isHREEmperor())
		{
			const auto& emperor = ck2province->second->getTitle().second->getHolder().second;
			if (emperor->getCapitalProvince().first == ck2province->first)
			{
				// This is the empire capital, never assign it away.
				winner = ck2province->second->getTitle().first;
				maxDev = 999;
			}
		}
	}
	// Let's see who the lucky winner is.
	for (const auto& share: theShares)
	{
		if (share.second > maxDev)
		{
			winner = share.first;
			maxDev = share.second;
		}
	}
	if (winner.empty())
	{
		return std::nullopt;
	}

	// Now that we have a winning title, let's find its largest province to use as a source.
	maxDev = -1; // We can have winning provinces with weight = 0;
	std::pair<int, std::shared_ptr<CK2::Province>> toReturn;
	for (const auto& province: theClaims[winner])
	{
		if (province->getBuildingWeight() > maxDev)
		{
			toReturn.first = province->getID();
			toReturn.second = province;
		}
	}
	if (!toReturn.first || !toReturn.second)
	{
		return std::nullopt;
	}
	return toReturn;
}
