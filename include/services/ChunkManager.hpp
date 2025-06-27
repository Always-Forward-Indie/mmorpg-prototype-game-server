#pragma once
#include "data/DataStructs.hpp"
#include "utils/Generators.hpp"
#include "utils/Logger.hpp"
#include <memory>
#include <shared_mutex>
#include <unordered_map>

class ChunkManager
{
  public:
    explicit ChunkManager(Logger &logger);

    void addChunkInfo(const ChunkInfoStruct &chunkInfo);
    void addListOfAllChunks(const std::vector<ChunkInfoStruct> &chunks);

    ChunkInfoStruct getChunkById(int chunkId) const;
    ChunkInfoStruct getChunkBySocket(const std::shared_ptr<boost::asio::ip::tcp::socket> &socket) const;

    void removeChunkServerDataBySocket(const std::shared_ptr<boost::asio::ip::tcp::socket> &socket);
    void removeChunkServerDataById(int chunkId);

  private:
    Logger &logger_;
    mutable std::shared_mutex mutex_;

    std::unordered_map<int, ChunkInfoStruct> chunksById_;
    std::unordered_map<std::shared_ptr<boost::asio::ip::tcp::socket>, int> chunkIdBySocket_;
};
