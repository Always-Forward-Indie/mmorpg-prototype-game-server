// Authenticator.hpp
#pragma once

#include <string>
#include <boost/asio.hpp>
#include "game_server/ClientData.hpp" // Include the header file for ClientData


class Authenticator {
public:
    int authenticate(const std::string& login, const int& id, const int& character_id, const std::string& hash, ClientData& clientData);
    int ChunkServerWorker(int clientID, ClientData& clientData);
    void handle_write(const boost::system::error_code& error, std::size_t bytes_transferred);
    void handle_read(const boost::system::error_code& error, std::size_t bytes_transferred, boost::asio::streambuf& receive_buffer);

private:
    // Define your authentication logic here
};