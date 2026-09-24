#include "game_server/GameServer.hpp"
#include "network/NetworkManager.hpp"
#include "services/GameServices.hpp"
#include "utils/Config.hpp"
#include "utils/Database.hpp"
#include "utils/Logger.hpp"
#include "utils/Scheduler.hpp"
#include "utils/TimeConverter.hpp"
#include <atomic>
#include <csignal>
#include <iostream>
#include <thread>

std::atomic<bool> running(true);

void
signalHandler(int signal)
{
    running = false;
}

int
main()
{
    Logger logger("game-server");
    try
    {
        // Register signal handlers
        std::signal(SIGINT, signalHandler);
        std::signal(SIGTERM, signalHandler);

        // Initialize Config
        Config config;
        // Get configs for connections to servers from config.json
        auto configs = config.parseConfig();

        // Initialize EventQueue
        EventQueue eventQueueChunkServer;
        EventQueue eventQueueGameServer;
        EventQueue eventQueueGameServerPing;

        // Initialize Scheduler
        Scheduler scheduler;

        // Initialize Database
        Database database(configs, logger);

        // Initialize GameServices (no I/O in ctor) and load static data
        // in explicit dependency order (fail-fast on DB errors).
        GameServices gameServices(database, logger);
        gameServices.loadStaticData();

        // Reset online status for all characters (crash recovery) via the
        // shared CharacterManager (was a second throwaway instance).
        gameServices.getCharacterManager().resetAllOnline(database);

        // Initialize NetworkManager
        NetworkManager networkManager(eventQueueGameServer, eventQueueGameServerPing, configs, logger);

        // Event Handler
        EventHandler eventHandler(networkManager, gameServices);

        // Initialize GameServer
        GameServer gameServer(gameServices, eventHandler, eventQueueGameServer, eventQueueChunkServer, eventQueueGameServerPing, scheduler);

        // (No setGameServer late-wire: NetworkManager is fully wired in its
        // ctor; nothing holds a GameServer link.)
        // Start accepting connections
        networkManager.startAccept();

        // Start the IO Networking event loop in the main thread
        networkManager.startIOEventLoop();

        // Start Game Server main event loop in a separate thread
        gameServer.startMainEventLoop();

        // Start Scheduler loop in a separate thread
        scheduler.start();

        // Wave 1.7: game-side chunk liveness sweep. The chunk heartbeats its
        // registration every 60s; a death the disconnect event never reports
        // (missed FIN, wedged peer) previously lingered until a manual chunk
        // reboot. Sweep every 60s, drop entries silent for 180s (3 heartbeats).
        // Scheduler::run already guards task exceptions; id 1 (first task).
        // Interval comes from game_config (scheduler.chunk_sweep_sec), sampled
        // once here; 60s fallback preserves previous behavior on empty config.
        // Silence threshold likewise (scheduler.chunk_silent_threshold_ms).
        {
            const int chunkSweepSec = gameServices.getGameConfigService().getInt("scheduler.chunk_sweep_sec", 60);
            const int chunkSilentMs = gameServices.getGameConfigService().getInt(
                "scheduler.chunk_silent_threshold_ms", 180000);
            Task chunkSweepTask(
                [&gameServices, chunkSilentMs]()
                { gameServices.getChunkManager().sweepSilentChunks(chunkSilentMs); },
                chunkSweepSec,
                std::chrono::system_clock::now() + std::chrono::seconds(chunkSweepSec),
                1);
            scheduler.scheduleTask(chunkSweepTask);
        }

        // Client liveness sweep (mirrors the chunk sweep above). Ping refreshes
        // lastSeenMs; entries silent for 10min (missed FIN, wedged peer) are
        // evicted. Conservative threshold: only truly dead sessions go.
        // Scheduler::run already guards task exceptions; id 2.
        {
            const int clientSweepSec = gameServices.getGameConfigService().getInt("scheduler.client_sweep_sec", 60);
            const int clientSilentMs = gameServices.getGameConfigService().getInt(
                "scheduler.client_silent_threshold_ms", 600000);
            Task clientSweepTask(
                [&gameServices, clientSilentMs]()
                { gameServices.getClientManager().sweepSilentClients(clientSilentMs); },
                clientSweepSec,
                std::chrono::system_clock::now() + std::chrono::seconds(clientSweepSec),
                2);
            scheduler.scheduleTask(clientSweepTask);
        }

        // wait for the signal to stop the server
        while (running)
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        logger.info("Shutting down gracefully...");

        // Ordered shutdown (reverse dependency order): Scheduler first (no new
        // chunk-sweep tasks), then the event loops (drain + join workers),
        // then the queues (wake any blocked popBatch). Previously this relied
        // solely on destructor order — correct today but implicit; explicit
        // stops make the contract visible and robust to reordering.
        scheduler.stop();
        gameServer.stop();
        eventQueueGameServer.stop();
        eventQueueChunkServer.stop();
        eventQueueGameServerPing.stop();

        return 0;
    }
    catch (const std::exception &e)
    {
        logger.critical("Fatal error: " + std::string(e.what()));
        return 1;
    }
}
