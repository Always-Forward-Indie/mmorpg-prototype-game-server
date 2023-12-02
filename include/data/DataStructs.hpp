#pragma once
#include <string>
#include <boost/asio.hpp>

struct PositionStruct
{
    float positionX = 0;
    float positionY = 0;
    float positionZ = 0;
};

struct CharacterDataStruct
{
    int characterId = 0;
    int characterLevel = 0;
    std::string characterName = "";
    std::string characterClass = "";
    std::string characterRace = "";
    PositionStruct characterPosition;
};

struct ClientDataStruct
{
    int clientId = 0;
    std::string hash = "";
    std::shared_ptr<boost::asio::ip::tcp::socket> socket;
    CharacterDataStruct characterData;
};

struct MessageStruct
{
    std::string status = "";
    std::string message = "";
};