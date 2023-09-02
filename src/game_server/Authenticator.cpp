#include "game_server/Authenticator.hpp"
#include "helpers/Database.hpp"
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <string>
#include <pqxx/pqxx>
#include <iostream>
#include "helpers/Config.hpp"


// Add these using declarations for convenience
using namespace pqxx;
using namespace std;

int Authenticator::authenticate(const std::string& login, const int& user_id, const int& character_id, const std::string& hash, ClientData& clientData) {
    try {
        // Create a PostgreSQL database connection
        Database database;
        // Check if the provided login and password match a record in the database
        pqxx::work transaction(database.getConnection());

        pqxx::result getUserDBData = transaction.exec_prepared("search_user", hash);

        if (!getUserDBData.empty()) {
            int userID = 0;

            // Loop through the result set and process the data
            for (pqxx::result::size_type i = 0; i < getUserDBData.size(); ++i) {
                userID = getUserDBData[i][0].as<int>(); // Access the second column (index 1)
            }

            pqxx::result getCharacterDBData = transaction.exec_prepared("get_character", character_id, userID);
            pqxx::result getCharacterDBPosition = transaction.exec_prepared("get_character_position", character_id);

            transaction.commit(); // Commit the transaction

            // Create a ClientDataStruct with the login, password, and unique hash
            ClientDataStruct clientDataStruct;
            clientDataStruct.clientId = userID;
            clientDataStruct.hash = hash;

            if(!getCharacterDBData[0][0].is_null()) {
                clientDataStruct.characterId = getCharacterDBData[0][0].as<int>();
                clientDataStruct.characterLevel = getCharacterDBData[0][6].as<int>();
                clientDataStruct.characterName = getCharacterDBData[0][1].as<std::string>();
                clientDataStruct.characterPositionX = getCharacterDBPosition[0][1].as<std::string>();
                clientDataStruct.characterPositionY = getCharacterDBPosition[0][2].as<std::string>();
                clientDataStruct.characterPositionZ = getCharacterDBPosition[0][3].as<std::string>();
            }
            else
            {  
                // Character not found message
                std::cout << "Character with ID: " << character_id << " not found" << std::endl;
            }

            clientData.storeClientData(clientDataStruct);  // Store clientData in the ClientData class

            //ChunkServerWorker(userID, clientData);

            return userID;
        } else {
            // User not found message
            std::cout << "User with ID: " << user_id << " not found" << std::endl;
            // Authentication failed, return false
            transaction.abort(); // Rollback the transaction (optional)
            return 0;
        }
    } catch (const std::exception& e) {
        // Handle database connection or query errors
        std::cerr << "Database error: " << e.what() << std::endl;
        // You might want to send an error response back to the client or log the error
        return 0;
    }
}

// int Authenticator::ChunkServerWorker(int clientID, ClientData& clientData)
// {
//     Config config;
//     auto configs = config.parseConfig("config.json");
//     short port = std::get<2>(configs).port;
//     std::string host = std::get<2>(configs).host;

//     const ClientDataStruct* currentClientData = clientData.getClientData(clientID);

//     //connect to chunk server using asio socket
//     boost::asio::io_context io_context;
//     boost::asio::ip::tcp::socket socket(io_context);

//     boost::asio::ip::tcp::resolver resolver(io_context);
//     auto endpoints = resolver.resolve(host, std::to_string(port));

//     boost::asio::async_connect(socket, endpoints,
//         [this, currentClientData, &socket](const boost::system::error_code& ec, boost::asio::ip::tcp::endpoint) {
//             if (!ec) {
//                 // Connection successful, now let's send and receive data
//                 std::string message = "Hello, Chunk Server! " + std::to_string(currentClientData->clientId);
//                 boost::asio::async_write(socket, boost::asio::buffer(message),
//                     [this, &socket](const boost::system::error_code& ec_write, std::size_t) {
//                         if (!ec_write) {
//                             // Write completed successfully, handle reading
//                             boost::asio::streambuf receive_buffer;
//                             boost::asio::async_read_until(socket, receive_buffer, '\n',
//                                 [this, &receive_buffer, &socket](const boost::system::error_code& ec_read, std::size_t) {
//                                     if (!ec_read) {
//                                         // Read completed successfully, call handle_read
//                                         handle_read(ec_read, 0, receive_buffer);
//                                     } else {
//                                         std::cerr << "Error reading data: " << ec_read.message() << std::endl;
//                                     }
//                                 });
//                         } else {
//                             std::cerr << "Error writing data: " << ec_write.message() << std::endl;
//                         }
//                     });
//             } else {
//                 std::cerr << "Error connecting to the server: " << ec.message() << std::endl;
//             }
//         });

//     io_context.run();

//     return 0;
// }

// void Authenticator::handle_write(const boost::system::error_code& error, std::size_t bytes_transferred) {
//     if (!error) {
//         std::cout << "Data sent successfully." << std::endl;
//     } else {
//         std::cerr << "Error sending data: " << error.message() << std::endl;
//     }
// }

// void Authenticator::handle_read(const boost::system::error_code& error, std::size_t bytes_transferred, boost::asio::streambuf& buffer) {
//     if (!error) {
//         std::istream input_stream(&buffer);
//         std::string received_data;
//         std::getline(input_stream, received_data);

//         std::cout << "Received data: " << received_data << std::endl;
//     } else {
//         std::cerr << "Error receiving data: " << error.message() << std::endl;
//     }
// }
