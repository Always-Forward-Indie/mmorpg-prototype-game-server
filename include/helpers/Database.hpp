#ifndef DATABASE_HPP
#define DATABASE_HPP

#include <pqxx/pqxx>
#include <memory>
#include "Config.hpp"

class Database {
public:
    Database();

    // Establish a database connection
    void connect();

    // Prepare default queries
    void prepareDefaultQueries();

    // Get a reference to the database connection
    pqxx::connection& getConnection();

private:
    std::unique_ptr<pqxx::connection> connection_;
};

#endif // DATABASE_HPP
