#include "Event.hpp"
#include <string>
#include <boost/asio.hpp>
#include "data/ClientData.hpp"
#include "network/NetworkManager.hpp"
#include "utils/ResponseBuilder.hpp"
#include "utils/Logger.hpp"
#include "utils/Database.hpp"
#include "network/ChunkServerWorker.hpp"
#include "services/CharacterManager.hpp"

class EventHandler {
public:
  EventHandler(NetworkManager& networkManager, 
  ChunkServerWorker& chunkServerWorker, 
  Database& database, 
  CharacterManager& characterManager,
  Logger& logger);
  void dispatchEvent(const Event& event, ClientData& clientData);

private:
    void handlePingClientEvent(const Event& event, ClientData& clientData);
    void handleJoinClientEvent(const Event& event, ClientData& clientData);
    void handleJoinChunkEvent(const Event& event, ClientData& clientData);
    void handleLeaveClientEvent(const Event& event, ClientData& clientData);
    void handleLeaveChunkEvent(const Event& event, ClientData& clientData);
    void handleGetConnectedCharactersClientEvent(const Event& event, ClientData& clientData);
    void handleGetConnectedCharactersChunkEvent(const Event& event, ClientData& clientData);
    void handleMoveCharacterChunkEvent(const Event& event, ClientData& clientData);
    void handleMoveCharacterClientEvent(const Event& event, ClientData& clientData);
    void handleInteractChunkEvent(const Event& event, ClientData& clientData);
    void handleInteractClientEvent(const Event& event, ClientData& clientData);
    void handleDisconnectChunkEvent(const Event& event, ClientData& clientData);
    void handleDisconnectClientEvent(const Event& event, ClientData& clientData);

    NetworkManager& networkManager_;
    ChunkServerWorker& chunkServerWorker_;
    Database& database_;
    Logger& logger_;
    CharacterManager& characterManager_;
    // Other private handler methods
};