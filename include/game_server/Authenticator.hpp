// Authenticator.hpp
#pragma once

#include <string>
#include <boost/asio.hpp>
#include "data/ClientData.hpp" // Include the header file for ClientData
#include "utils/Database.hpp" // Include the header file for Database
#include "utils/TerminalColors.hpp"

class Authenticator {
public:
    int authenticate(Database& database, ClientData& clientData, const std::string& hash, const int& user_id);
};