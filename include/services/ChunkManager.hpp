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
