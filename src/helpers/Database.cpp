#include "helpers/Database.hpp"
#include "helpers/Config.hpp"
#include <iostream>

Database::Database() {
    connect();
    prepareDefaultQueries();
}

void Database::connect() {
    try {
        Config config;
        auto configs = config.parseConfig("config.json");
        short port = std::get<0>(configs).port;
        std::string host = std::get<0>(configs).host;
        std::string databaseName = std::get<0>(configs).dbname;
        std::string user = std::get<0>(configs).user;
        std::string password = std::get<0>(configs).password;

        std::cout << "Connecting to database..." << std::endl;
        std::cout << "Database name: " << databaseName << std::endl;
        std::cout << "User: " << user << std::endl;
        std::cout << "Host: " << host << std::endl;
        std::cout << "Port: " << port << std::endl;

        connection_ = std::make_unique<pqxx::connection>(
            "dbname=" + databaseName + " user=" + user + " password=" + password + " hostaddr=" + host + " port=" + std::to_string(port));

        if (connection_->is_open()) {
            std::cout << "Database connection established" << std::endl;
        } else {
            std::cout << "Database connection failed" << std::endl;
            // Handle the connection failure (e.g., throw an exception or exit)
        }
    } catch (const std::exception& e) {
        std::cerr << "Error while connecting to the database: " << e.what() << std::endl;
        // Handle the exception (e.g., throw it or exit the application)
    }
}

void Database::prepareDefaultQueries() {
    if (connection_->is_open()) {
        connection_->prepare("search_user", "SELECT * FROM users WHERE session_key = $1 LIMIT 1;");
        connection_->prepare("get_character", "SELECT * FROM characters WHERE id = $1 and owner_id = $2 LIMIT 1;");
        connection_->prepare("get_character_position", "SELECT x, y, z FROM character_position WHERE character_id = $1 LIMIT 1;");
        connection_->prepare("set_character_position", "SELECT x, y, z FROM character_position WHERE character_id = $1 LIMIT 1;");
    } else {
        std::cerr << "Cannot prepare queries: Database connection is not open." << std::endl;
        // Handle this situation (e.g., throw an exception or exit)
    }
}

pqxx::connection& Database::getConnection() {
    if (connection_->is_open()) {
        return *connection_;
    } else {
        throw std::runtime_error("Database connection is not open.");
    }
}