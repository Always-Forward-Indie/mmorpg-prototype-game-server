#include "services/ChunkManager.hpp"
#include <spdlog/logger.h>

ChunkManager::ChunkManager(Logger &logger) : logger_(logger) {
    log_ = logger.getSystem("chunk");}

void
ChunkManager::addChunkInfo(const ChunkInfoStruct &chunkInfo)
{
    if (chunkInfo.id <= 0)
    {
        // Defense in depth (the handler rejects these with a client error):
        // id 0 is the "missing header.id" default, never a real chunk. The
        // real chunk hardcodes header.id = 1 in its handshake.
        log_->warn("[ChunkManager] ignoring chunk registration with id={} (malformed handshake?)",
            chunkInfo.id);
        return;
    }
    std::unique_lock lock(mutex_);
    ChunkInfoStruct stamped = chunkInfo;
    stamped.lastHeartbeatMs = steadyNowMs();
    // Drop the reverse entry of any previous socket for this chunk id first,
    // so a late disconnect for the old socket can never resolve to the new one.
    auto fwd = chunksById_.find(stamped.id);
    if (fwd != chunksById_.end() && fwd->second.socket &&
        fwd->second.socket != stamped.socket)
    {
        chunkIdBySocket_.erase(fwd->second.socket);
        socketGen_.erase(fwd->second.socket);
    }
    const uint64_t gen = nextGen_.fetch_add(1, std::memory_order_relaxed);
    chunksById_[stamped.id] = stamped;
    chunkIdBySocket_[stamped.socket] = stamped.id;
    genById_[stamped.id] = gen;
    if (stamped.socket)
        socketGen_[stamped.socket] = {stamped.id, gen};
    log_->info("[ChunkManager] registered chunk id={} port={} gen={}",
        stamped.id, stamped.port, gen);
}

void
ChunkManager::addListOfAllChunks(const std::vector<ChunkInfoStruct> &chunks)
{
    std::unique_lock lock(mutex_);
    const int64_t now = steadyNowMs();
    for (const auto &chunk : chunks)
    {
        if (chunk.id <= 0)
        {
            log_->warn("[ChunkManager] ignoring chunk registration with id={} (malformed handshake?)",
                chunk.id);
            continue;
        }
        auto fwd = chunksById_.find(chunk.id);
        if (fwd != chunksById_.end() && fwd->second.socket &&
            fwd->second.socket != chunk.socket)
        {
            chunkIdBySocket_.erase(fwd->second.socket);
            socketGen_.erase(fwd->second.socket);
        }
        const uint64_t gen = nextGen_.fetch_add(1, std::memory_order_relaxed);
        ChunkInfoStruct stamped = chunk;
        stamped.lastHeartbeatMs = now;
        chunksById_[chunk.id] = stamped;
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
        log_->warn("[ChunkManager] live chunk id={} removed by socket disconnect", id);
    }
    else
    {
        log_->info("[ChunkManager] stale socket disconnect ignored (chunk id={} stays live)", id);
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
        log_->warn("[ChunkManager] chunk id={} removed by id", chunkId);
    }
}

int64_t
ChunkManager::steadyNowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::vector<int>
ChunkManager::sweepSilentChunks(int64_t silentThresholdMs)
{
    std::vector<int> removed;
    const int64_t now = steadyNowMs();
    std::unique_lock lock(mutex_);
    for (auto it = chunksById_.begin(); it != chunksById_.end();)
    {
        const int id = it->first;
        const int64_t silentFor = now - it->second.lastHeartbeatMs;
        if (silentFor >= silentThresholdMs)
        {
            if (it->second.socket)
                socketGen_.erase(it->second.socket);
            chunkIdBySocket_.erase(it->second.socket);
            genById_.erase(id);
            it = chunksById_.erase(it);
            removed.push_back(id);
            log_->warn("[ChunkManager] sweep: silent chunk id={} removed (no heartbeat for {}ms)",
                id, silentFor);
        }
        else
        {
            ++it;
        }
    }
    return removed;
}
