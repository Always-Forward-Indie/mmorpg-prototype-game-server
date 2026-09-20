#pragma once
#include "data/DataStructs.hpp"
#include "utils/Generators.hpp"
#include "utils/Logger.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <utility>
#include <vector>

class ChunkManager
{
  public:
    explicit ChunkManager(Logger &logger);

    void addChunkInfo(const ChunkInfoStruct &chunkInfo);
    void addListOfAllChunks(const std::vector<ChunkInfoStruct> &chunks);

    ChunkInfoStruct getChunkById(int chunkId) const;
    ChunkInfoStruct getChunkBySocket(const std::shared_ptr<boost::asio::ip::tcp::socket> &socket) const;

    /// Resolve the socket to answer a chunk-originated request on, at send
    /// time. Events queue async work: by the time a game→chunk runtime
    /// response is built, the chunk may have reconnected and the captured
    /// `hint` socket may be stale (writes into a half-open stale socket
    /// fail silently). Prefer the current registration for the hint's chunk
    /// (default chunk id 1 when the hint is unknown); fall back to `hint`
    /// itself; null when neither names a socket. Callers still get
    /// sendResponse's closed-socket error for dead sockets. Pure registry
    /// read — unit-pinned in test_chunk_manager.cpp.
    std::shared_ptr<boost::asio::ip::tcp::socket> resolveLiveSocket(
        const std::shared_ptr<boost::asio::ip::tcp::socket> &hint) const;

    void removeChunkServerDataBySocket(const std::shared_ptr<boost::asio::ip::tcp::socket> &socket);
    void removeChunkServerDataById(int chunkId);

    /// Drop chunks silent longer than silentThresholdMs (no heartbeat /
    /// re-registration since). Game-side liveness net for deaths the
    /// disconnect event never reports (missed TCP FIN, wedged peer).
    /// Returns removed ids. Warns per removal for alert grep.
    std::vector<int> sweepSilentChunks(int64_t silentThresholdMs);

    /// steady_clock ms. Public for tests.
    static int64_t steadyNowMs();

  private:
    Logger &logger_;
    std::shared_ptr<spdlog::logger> log_;
    mutable std::shared_mutex mutex_;

    std::unordered_map<int, ChunkInfoStruct> chunksById_;
    std::unordered_map<std::shared_ptr<boost::asio::ip::tcp::socket>, int> chunkIdBySocket_;
    // Generation stamps: async disconnect events for a STALE socket must
    // never wipe a live re-registration (same chunk id, new socket, possibly
    // even a reused raw address). socketGen_ maps socket -> (chunkId, gen);
    // genById_ holds the live generation per chunk id. Removal proceeds only
    // on generation match; otherwise only the stale reverse entry is dropped.
    std::unordered_map<std::shared_ptr<boost::asio::ip::tcp::socket>,
        std::pair<int, uint64_t>>
        socketGen_;
    std::unordered_map<int, uint64_t> genById_;
    std::atomic<uint64_t> nextGen_{1};
};
