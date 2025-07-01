#pragma once
#include "data/DataStructs.hpp"
#include "utils/Database.hpp"
#include "utils/Generators.hpp"
#include "utils/Logger.hpp"

class MobManager
{
  public:
    MobManager(Database &database, Logger &logger);
    void loadMobs();

    std::map<int, MobDataStruct> getMobs() const;
    std::vector<MobDataStruct> getMobsAsVector() const;
    MobDataStruct getMobById(int mobId) const;

    std::map<int, MobAttributeStruct> getMobsAttributes() const;

  private:
    Database &database_;
    Logger &logger_;

    // Store the mobs in memory as map with mobId as key
    std::map<int, MobDataStruct> mobs_;
};