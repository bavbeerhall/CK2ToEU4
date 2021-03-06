#ifndef EU4_COUNTRY_H
#define EU4_COUNTRY_H

#include "../../CK2World/Titles/Title.h"
#include "../../Mappers/LocalizationMapper/LocalizationMapper.h"
#include "CountryDetails.h"
#include <memory>
#include <string>

namespace mappers
{
class GovernmentsMapper;
class ReligionMapper;
class CultureMapper;
class ColorScraper;
class ProvinceMapper;
class RulerPersonalitiesMapper;
} // namespace mappers

namespace EU4
{
class Province;
class Country
{
  public:
	Country() = default;

	Country(std::string theTag, const std::string& filePath);
	void loadHistory(const std::string& filePath);
	void initializeFromTitle(std::string theTag,
		 std::shared_ptr<CK2::Title> theTitle,
		 const mappers::GovernmentsMapper& governmentsMapper,
		 const mappers::ReligionMapper& religionMapper,
		 const mappers::CultureMapper& cultureMapper,
		 const mappers::ProvinceMapper& provinceMapper,
		 const mappers::ColorScraper& colorScraper,
		 const mappers::LocalizationMapper& localizationMapper,
		 const mappers::RulerPersonalitiesMapper& rulerPersonalitiesMapper,
		 date theConversionDate);
	void initializeRulers(const mappers::ReligionMapper& religionMapper,
		 const mappers::CultureMapper& cultureMapper,
		 const mappers::RulerPersonalitiesMapper& rulerPersonalitiesMapper);
	void initializeAdvisers(const mappers::ReligionMapper& religionMapper, const mappers::CultureMapper& cultureMapper);

	void outputCommons(std::ostream& output) const;
	void outputAdvisers(std::ostream& output) const;

	[[nodiscard]] const auto& getCommonCountryFile() const { return commonCountryFile; }
	[[nodiscard]] const auto& getHistoryCountryFile() const { return historyCountryFile; }
	[[nodiscard]] const auto& getLocalizations() const { return localizations; }
	[[nodiscard]] const auto& getPrimaryCulture() const { return details.primaryCulture; }
	[[nodiscard]] const auto& getReligion() const { return details.religion; }
	[[nodiscard]] const auto& getTechGroup() const { return details.technologyGroup; }
	[[nodiscard]] const auto& getGFX() const { return details.graphicalCulture; }
	[[nodiscard]] const auto& getProvinces() const { return provinces; }
	[[nodiscard]] const auto& getTitle() const { return title; }
	[[nodiscard]] const auto& getGovernment() const { return details.government; }
	[[nodiscard]] const auto& getGovernmentReforms() const { return details.reforms; }
	[[nodiscard]] const auto& getTag() const { return tag; }
	[[nodiscard]] const auto& getAdvisers() const { return details.advisers; }
	[[nodiscard]] auto getConversionDate() const { return conversionDate; }
	[[nodiscard]] auto isExcommunicated() const { return details.excommunicated; }
	[[nodiscard]] auto isHREEmperor() const { return details.holyRomanEmperor; }
	[[nodiscard]] auto isinHRE() const { return details.inHRE; }
	[[nodiscard]] auto getCapitalID() const { return details.capital; }
	[[nodiscard]] auto getDynastyID() const { return details.dynastyID; }

	[[nodiscard]] int getDevelopment() const;

	bool verifyCapital(const mappers::ProvinceMapper& provinceMapper);

	void registerProvince(std::pair<int, std::shared_ptr<Province>> theProvince) { provinces.insert(std::move(theProvince)); }
	void setPrimaryCulture(const std::string& culture);
	void setReligion(const std::string& religion);
	void overrideReforms(const std::string& reform) { details.reforms = {reform}; }
	void setGovernment(const std::string& government) { details.government = government; }
	void setElector() { details.elector = true; }
	void setTechGroup(const std::string& tech) { details.technologyGroup = tech; }
	void setGFX(const std::string& gfx) { details.graphicalCulture = gfx; }
	void clearProvinces() { provinces.clear(); }
	void annexCountry(const std::pair<std::string, std::shared_ptr<Country>>& theCountry);
	void setMonarch(const Character& monarch) { details.monarch = monarch; }
	void clearHistoryLessons() { details.historyLessons.clear(); }
	void setConversionDate(date theDate) { conversionDate = theDate; }
	void clearExcommunicated() { details.excommunicated = false; }

	friend std::ostream& operator<<(std::ostream& output, const Country& versionParser);

  private:
	std::string tag;
	std::string commonCountryFile;
	std::string historyCountryFile;
	date conversionDate; // for dating the monarchs in history file.
	CountryDetails details;

	std::pair<std::string, std::shared_ptr<CK2::Title>> title;
	std::map<std::string, mappers::LocBlock> localizations;
	std::map<int, std::shared_ptr<Province>> provinces;
};
} // namespace EU4

#endif // EU4_COUNTRY_H