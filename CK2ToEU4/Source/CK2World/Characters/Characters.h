#ifndef CK2_CHARACTERS_H
#define CK2_CHARACTERS_H
#include "newParser.h"

namespace mappers
{
class PersonalityScraper;
}

namespace CK2
{
class Titles;
class Dynasties;
class Character;
class Provinces;
class Characters: commonItems::parser
{
  public:
	Characters() = default;
	Characters(std::istream& theStream);

	[[nodiscard]] const auto& getCharacters() const { return characters; }

	void linkDynasties(const Dynasties& theDynasties);
	void linkLiegesAndSpouses();
	void linkMothersAndFathers();
	void linkPrimaryTitles(const Titles& theTitles);
	void linkCapitals(const Provinces& theProvinces);
	void assignPersonalities(const mappers::PersonalityScraper& personalityScraper);

  private:
	void registerKeys();

	std::map<int, std::shared_ptr<Character>> characters;
};
} // namespace CK2

#endif // CK2_CHARACTERS_H
