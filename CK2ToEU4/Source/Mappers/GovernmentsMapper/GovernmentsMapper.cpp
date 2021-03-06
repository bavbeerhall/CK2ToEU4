#include "GovernmentsMapper.h"
#include "Log.h"
#include "ParserHelpers.h"

mappers::GovernmentsMapper::GovernmentsMapper()
{
	LOG(LogLevel::Info) << "-> Parsing government mappings.";
	registerKeys();
	parseFile("configurables/government_map.txt");
	clearRegisteredKeywords();
	LOG(LogLevel::Info) << "<> Loaded " << govMappings.size() << " governmental links.";
}

mappers::GovernmentsMapper::GovernmentsMapper(std::istream& theStream)
{
	registerKeys();
	parseStream(theStream);
	clearRegisteredKeywords();
}

void mappers::GovernmentsMapper::registerKeys()
{
	registerKeyword("link", [this](const std::string& unused, std::istream& theStream) {
		const GovernmentsMapping newMapping(theStream);
		govMappings.emplace_back(newMapping);
	});
	registerRegex("[A-Za-z0-9\\_.:-]+", commonItems::ignoreItem);
}

std::optional<std::pair<std::string, std::string>> mappers::GovernmentsMapper::matchGovernment(const std::string& ck2Government,
	 const std::string& ck2Title) const
{
	std::pair<std::string, std::string> toReturn;

	// first iterate over those that have a ck2title field, they take priority.
	for (const auto& mapping: govMappings)
	{
		if (mapping.getCK2Title().empty())
			continue;
		if (mapping.matchGovernment(ck2Government, ck2Title))
		{
			toReturn.first = mapping.getGovernment();
			toReturn.second = mapping.getReform();
			return toReturn;
		}
	}

	// Then might as well retry.
	for (const auto& mapping: govMappings)
		if (mapping.matchGovernment(ck2Government, ck2Title))
		{
			toReturn.first = mapping.getGovernment();
			toReturn.second = mapping.getReform();
			return toReturn;
		}
	return std::nullopt;
}
