#pragma once
#include "utils/Database.hpp"
#include "utils/Logger.hpp"
#include <shared_mutex>
#include <string>
#include <unordered_map>

/**
 * @brief Сервис геймплейной конфигурации на стороне game-server.
 *
 * Читает таблицу game_config при старте (loadConfig) и при GM-команде reload().
 * Предоставляет getAll() для сериализации и отправки chunk-server'у.
 *
 * Потокобезопасен через shared_mutex: loadConfig/reload блокируют на запись,
 * getAll/get* блокируют на чтение.
 */
class GameConfigService
{
  public:
    GameConfigService(Database &db, Logger &logger);

    /**
     * @brief Загрузить конфиг из БД. Вызывается при старте и при reload().
     */
    void loadConfig();

    /**
     * @brief Runtime-перезагрузка конфига без перезапуска сервера.
     *        Эквивалентен loadConfig() — повторно читает всю таблицу.
     */
    void reload();

    /**
     * @brief Вернуть snapshot всего конфига для сериализации в JSON.
     *        Возвращает копию, чтобы держать лок минимально.
     */
    std::unordered_map<std::string, std::string> getAll() const;

  private:
    Database &db_;
    Logger &logger_;
    std::shared_ptr<spdlog::logger> log_;

    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::string> config_;
};
