#include "services/ChunkManager.hpp"

ChunkManager::ChunkManager(Logger &logger) : logger_(logger) {
    log_ = logger.getSystem("chunk");}

void
ChunkManager::addChunkInfo(const ChunkInfoStruct &chunkInfo)
{
    std::unique_lock lock(mutex_);
    // Drop the reverse entry of any previous socket for this chunk id first,
    // so a late disconnect for the old socket can never resolve to the new one.
    auto fwd = chunksById_.find(chunkInfo.id);
    if (fwd != chunksById_.end() && fwd->second.socket &&
        fwd->second.socket != chunkInfo.socket)
    {
        chunkIdBySocket_.erase(fwd->second.socket);
        socketGen_.erase(fwd->second.socket);
    }
    const uint64_t gen = nextGen_.fetch_add(1, std::memory_order_relaxed);
    chunksById_[chunkInfo.id] = chunkInfo;
    chunkIdBySocket_[chunkInfo.socket] = chunkInfo.id;
    genById_[chunkInfo.id] = gen;
    if (chunkInfo.socket)
        socketGen_[chunkInfo.socket] = {chunkInfo.id, gen};
}

void
ChunkManager::addListOfAllChunks(const std::vector<ChunkInfoStruct> &chunks)
{
    std::unique_lock lock(mutex_);
    for (const auto &chunk : chunks)
    {
        auto fwd = chunksById_.find(chunk.id);
        if (fwd != chunksById_.end() && fwd->second.socket &&
            fwd->second.socket != chunk.socket)
        {
            chunkIdBySocket_.erase(fwd->second.socket);
            socketGen_.erase(fwd->second.socket);
        }
        const uint64_t gen = nextGen_.fetch_add(1, std::memory_order_relaxed);
        chunksById_[chunk.id] = chunk;
        chunkIdBySocket_[chunk.socket] = chunk.id;
        genById_[chunk.id] = gen;
        if (chunk.socket)
            socketGen_[chunk.socket] = {chunk.id, gen};
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
    if (!socket)
        return;
    std::unique_lock lock(mutex_);
    auto it = chunkIdBySocket_.find(socket);
    if (it == chunkIdBySocket_.end())
    {
        socketGen_.erase(socket);
        return;
    }
    const int id = it->second;
    // Generation check: a stale disconnect (previous generation, or a raw
    // address reused by an unrelated object) must never wipe the live entry.
    // In that case drop only the stale reverse mapping.
    bool live = false;
    auto genIt = socketGen_.find(socket);
    if (genIt != socketGen_.end() && genIt->second.first == id)
    {
        auto liveIt = genById_.find(id);
        live = (liveIt != genById_.end() && liveIt->second == genIt->second.second);
    }
    socketGen_.erase(socket);
    chunkIdBySocket_.erase(it);
    if (live)
    {
        chunksById_.erase(id);
        genById_.erase(id);
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
        if (it->second.socket)
            socketGen_.erase(it->second.socket);
        genById_.erase(chunkId);
        chunksById_.erase(it);
    }
}
