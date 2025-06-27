#include "services/ChunkManager.hpp"

ChunkManager::ChunkManager(Logger &logger) : logger_(logger) {}

void
ChunkManager::addChunkInfo(const ChunkInfoStruct &chunkInfo)
{
    std::unique_lock lock(mutex_);
    chunksById_[chunkInfo.id] = chunkInfo;
    chunkIdBySocket_[chunkInfo.socket] = chunkInfo.id;
}

void
ChunkManager::addListOfAllChunks(const std::vector<ChunkInfoStruct> &chunks)
{
    std::unique_lock lock(mutex_);
    for (const auto &chunk : chunks)
    {
        chunksById_[chunk.id] = chunk;
        chunkIdBySocket_[chunk.socket] = chunk.id;
    }
}

ChunkInfoStruct
ChunkManager::getChunkById(int chunkId) const
{
    std::shared_lock lock(mutex_);
    auto it = chunksById_.find(chunkId);
    return it != chunksById_.end() ? it->second : ChunkInfoStruct{};
}

ChunkInfoStruct
ChunkManager::getChunkBySocket(const std::shared_ptr<boost::asio::ip::tcp::socket> &socket) const
{
    std::shared_lock lock(mutex_);
    auto it = chunkIdBySocket_.find(socket);
    if (it != chunkIdBySocket_.end())
    {
        auto chunkIt = chunksById_.find(it->second);
        if (chunkIt != chunksById_.end())
            return chunkIt->second;
    }
    return ChunkInfoStruct{};
}

void
ChunkManager::removeChunkServerDataBySocket(const std::shared_ptr<boost::asio::ip::tcp::socket> &socket)
{
    std::unique_lock lock(mutex_);
    auto it = chunkIdBySocket_.find(socket);
    if (it != chunkIdBySocket_.end())
    {
        chunksById_.erase(it->second);
        chunkIdBySocket_.erase(it);
    }
}

void
ChunkManager::removeChunkServerDataById(int chunkId)
{
    std::unique_lock lock(mutex_);
    auto it = chunksById_.find(chunkId);
    if (it != chunksById_.end())
    {
        chunkIdBySocket_.erase(it->second.socket);
        chunksById_.erase(it);
    }
}
