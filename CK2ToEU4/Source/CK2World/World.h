#ifndef CK2_WORLD_H
#define CK2_WORLD_H
#include "newParser.h"
#include "../Mappers/ProvinceTitleMapper/ProvinceTitleMapper.h"
#include "Provinces/Provinces.h"
#include "../Common/Version.h"
#include "Date.h"
#include "Characters/Characters.h"

class Configuration;

namespace CK2
{
	class World: commonItems::parser
	{
	public:
		World(std::shared_ptr<Configuration> theConfiguration);
				
	private:
		void verifySave(const std::string& saveGamePath);
		bool uncompressSave(const std::string& saveGamePath);

		date endDate = date("1444.11.11");
		date startDate = date("1.1.1");
		Version CK2Version;

		struct saveData
		{
			bool compressed = false;
			std::string metadata;
			std::string gamestate;
		};
		saveData saveGame;

		mappers::ProvinceTitleMapper provinceTitleMapper;
		Provinces provinces;
		Characters characters;
		
	};
}

#endif // CK2_WORLD_H
