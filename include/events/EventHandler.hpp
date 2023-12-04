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
    void handleJoinedClientEvent(const Event& event, ClientData& clientData);
    void handleJoinToChunkEvent(const Event& event, ClientData& clientData);
    void handleMoveEvent(const Event& event, ClientData& clientData);
    void handleInteractEvent(const Event& event, ClientData& clientData);

    NetworkManager& networkManager_;
    ChunkServerWorker& chunkServerWorker_;
    Database& database_;
    Logger& logger_;
    CharacterManager& characterManager_;
    // Other private handler methods
};