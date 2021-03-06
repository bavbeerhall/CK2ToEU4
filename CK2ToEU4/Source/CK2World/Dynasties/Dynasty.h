#ifndef CK2_DYNASTY_H
#define CK2_DYNASTY_H
#include "CoatOfArms.h"
#include "newParser.h"

namespace CK2
{
class Dynasty: commonItems::parser
{
  public:
	Dynasty() = default;
	Dynasty(std::istream& theStream, int theDynID);

	void updateDynasty(std::istream& theStream);
	void underUpdateDynasty(std::istream& theStream);

	[[nodiscard]] const auto& getCulture() const { return culture; }
	[[nodiscard]] const std::string& getReligion() const;
	[[nodiscard]] const auto& getName() const { return name; }

	[[nodiscard]] auto getID() const { return dynID; }

  private:
	void registerKeys();
	void registerUnderKeys();

	int dynID = 0;
	std::string culture;
	std::string religion;
	std::string name;
	CoatOfArms coa;
};
} // namespace CK2

#endif // CK2_DYNASTY_H
