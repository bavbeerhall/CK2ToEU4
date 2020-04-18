#ifndef CK2_TRIBUTARY_H
#define CK2_TRIBUTARY_H
#include "newParser.h"

namespace CK2
{
class Tributary: commonItems::parser
{
  public:
	Tributary() = default;
	explicit Tributary(std::istream& theStream);

	[[nodiscard]] const auto& getTributaryType() const { return tributaryType; }
	[[nodiscard]] auto getTributaryID() const { return tributaryID; }

  private:
	void registerKeys();
	std::string tributaryType;
	int tributaryID = 0;
};
} // namespace CK2

#endif // CK2_TRIBUTARY_H