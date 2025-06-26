#pragma once

#include <string>
#include <boost/asio.hpp>
#include "utils/Database.hpp" // Include the header file for Database
#include "utils/TerminalColors.hpp"

class Authenticator {
public:
    int authenticate(Database& database, const std::string& hash, const int& user_id);
};