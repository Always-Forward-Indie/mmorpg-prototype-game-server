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
    /// fail silently). Prefer the current registration for the hint's chunk;
    /// when the hint is unknown and exactly one chunk is registered, use it
    /// (single-chunk compat); with 0 or 2+ chunks fall back to `hint`
    /// itself. Pure registry read — unit-pinned in test_chunk_manager.cpp.
    std::shared_ptr<boost::asio::ip::tcp::socket> resolveLiveSocket(
        const std::shared_ptr<boost::asio::ip::tcp::socket> &hint) const;

    /// Multi-chunk routing: all registered chunks (copy under lock).
    std::vector<ChunkInfoStruct> getAllChunks() const;
    /// Chunk whose [pos,size) box contains (x,y). Empty when none matches
    /// (single-chunk legacy boxes are 0,0,0 — caller falls back to single).
    ChunkInfoStruct selectChunkForPosition(float x, float y) const;
    /// Chunk for a new client without position yet: position-routed when
    /// caller knows it, else first registered (deterministic by id).
    /// Empty when no chunks registered.
    ChunkInfoStruct selectChunkForNewClient() const;

    void removeChunkServerDataBySocket(const std::shared_ptr<boost::asio::ip::tcp::socket> &socket);
    void removeChunkServerDataById(int chunkId);

    /// Shutdown helper (S16a): close + release every held socket BEFORE the
    /// io_context dies. Sockets destroyed after their execution_context
    /// trigger ASan heap-use-after-free in asio internals (observed on game
    /// shutdown: ~ChunkInfoStruct ran after ~io_context). Called once from
    /// main's ordered shutdown; afterwards the maps are empty (a fresh boot
    /// re-discovers chunks via re-registration).
    void dropAllSockets();

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
