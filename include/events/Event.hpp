#pragma once
#include "data/DataStructs.hpp"
#include <boost/asio.hpp>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <variant>

// Define the types of data that can be sent in an event
using EventData = std::variant<
    int,
    float,
    std::string,
    nlohmann::json,
    PositionStruct,
    CharacterDataStruct,
    ClientDataStruct,
    ChunkInfoStruct,
    SpawnZoneStruct,
    std::vector<MobDataStruct>,
    MobDataStruct,
    std::vector<MobAttributeStruct>,
    MobAttributeStruct
    /* other types */>;

class Event
{
  public:
    enum EventType
    {
        PING_CLIENT,
        JOIN_CHUNK_SERVER,
        JOIN_PLAYER_CLIENT,
        GET_CHARACTER_DATA,
        DISCONNECT_CLIENT,
        DISCONNECT_CHUNK_SERVER,
        GET_CONNECTED_CHARACTERS,
        MOVE_CHARACTER,
        GET_SPAWN_ZONES,
        GET_MOBS_LIST,
        GET_MOBS_ATTRIBUTES,
        GET_MOB_DATA,
        SPAWN_MOBS_IN_ZONE,
        ZONE_MOVE_MOBS,
        MOVE_MOB
    }; // Define more event types as needed
    Event() = default; // Default constructor
    Event(EventType type, int clientID, const EventData data, std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket);
    // Event(EventType type, int clientID, const EventData data);

    // Get Event Data
    const EventData getData() const;
    // Get Client ID
    int getClientID() const;
    // Get Client Socket
    std::shared_ptr<boost::asio::ip::tcp::socket> getClientSocket() const;
    // Get Event Type
    EventType getType() const;

  private:
    int clientID;
    EventType type;
    EventData eventData;
    std::shared_ptr<boost::asio::ip::tcp::socket> currentClientSocket;
};