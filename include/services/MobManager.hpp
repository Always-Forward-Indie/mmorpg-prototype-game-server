#pragma once
#include "data/DataStructs.hpp"
#include "utils/Database.hpp"
#include "utils/Generators.hpp"
#include "utils/Logger.hpp"

class MobManager
{
  public:
    /// Explicit dependencies (no Database): loading stays in loadMobs(Database&).
    MobManager(Logger &logger);
    void loadMobs(Database &database);

    /**
     * @brief Replace the in-memory mob catalog (pure, no DB).
     */
    void setMobsList(const std::vector<MobDataStruct> &mobs);

    std::map<int, MobDataStruct> getMobs() const;
    std::vector<MobDataStruct> getMobsAsVector() const;
    MobDataStruct getMobById(int mobId) const;

    std::map<int, MobAttributeStruct> getMobsAttributes() const;

  private:
    Logger &logger_;
    std::shared_ptr<spdlog::logger> log_;

    // Store the mobs in memory as map with mobId as key
    std::map<int, MobDataStruct> mobs_;
};