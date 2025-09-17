# NPC System Implementation

## Overview
Comprehensive NPC (Non-Player Character) system implementation for the MMORPG prototype game server following SOLID principles and existing architectural patterns.

## Architecture

### Components

#### 1. NPCManager (`services/NPCManager.hpp/cpp`)
- **Single Responsibility**: Manages NPC data loading and retrieval
- **Thread-safe operations** with mutex protection
- **Memory-efficient** lazy loading pattern
- **Database integration** with connection pooling
- **Error handling** with detailed logging

#### 2. NPCDataStruct (`data/DataStructs.hpp`)
- Complete NPC data structure
- Position, attributes, skills, and metadata
- NPC-specific properties (type, interactability, dialogue/quest IDs)

#### 3. Event System Integration
- **GET_NPCS_LIST**: Load all NPCs with full data
- **GET_NPCS_ATTRIBUTES**: Load NPC attributes separately
- **SPAWN_NPCS_IN_CHUNK**: Spawn NPCs when player enters chunk

### Database Queries (already implemented)
```sql
-- Basic NPC data
get_npcs: "SELECT npc.*, race.name as race FROM npc JOIN race ON npc.race_id = race.id;"

-- NPC attributes  
get_npc_attributes: "SELECT entity_attributes.*, npc_attributes.value FROM npc_attributes JOIN entity_attributes ON npc_attributes.attribute_id = entity_attributes.id WHERE npc_attributes.npc_id = $1;"

-- NPC skills
get_npc_skills: Complex query with skill data, effects, and properties

-- NPC position
get_npc_position: "SELECT x, y, z FROM npc_position WHERE npc_id = $1 LIMIT 1;"
```

## Features

### 1. Memory Management
- **RAII** pattern with smart pointers
- **Thread-safe** concurrent access
- **Exception safety** with proper cleanup
- **Memory-efficient** data structures

### 2. Performance Optimizations
- **Lazy loading** - NPCs loaded on demand
- **Chunk-based spawning** - Only relevant NPCs sent to client
- **Batch operations** for database queries
- **Connection pooling** for database access

### 3. Error Handling
- **Graceful degradation** on database errors
- **Detailed logging** for debugging
- **Transaction rollback** on failures
- **Null safety** for optional fields

### 4. Thread Safety
- **Mutex protection** for shared data
- **Atomic operations** where possible
- **Lock-free read operations** when safe
- **Deadlock prevention** patterns

## Usage

### Loading NPCs
```cpp
// Load all NPCs from database
gameServices.getNPCManager().loadNPCs();

// Get NPCs for specific chunk
auto chunkNPCs = gameServices.getNPCManager().getNPCsByChunk(chunkX, chunkY, chunkZ);

// Get specific NPC
auto npc = gameServices.getNPCManager().getNPCById(npcId);
```

### Event Flow
1. **Chunk Server Connection**: NPCs data sent to chunk server
2. **Player Login**: Character data sent, then NPC spawn event triggered  
3. **Chunk Entry**: NPCs for current chunk spawned on client
4. **Runtime**: NPCs remain persistent, interact with players

### Network Protocol
```json
// NPC List Response
{
  "header": {
    "eventType": "setNPCsList",
    "message": "Getting NPCs list success!"
  },
  "body": {
    "npcsList": [
      {
        "id": 1,
        "name": "Village Merchant", 
        "npcType": "vendor",
        "isInteractable": true,
        "posX": 100.5,
        "posY": 200.0,
        "posZ": 50.0
      }
    ]
  }
}

// NPC Spawn in Chunk
{
  "header": {
    "eventType": "spawnNPCsInChunk"
  },
  "body": {
    "chunkX": 1,
    "chunkY": 2, 
    "chunkZ": 0,
    "npcsSpawn": [...]
  }
}
```

## Integration Points

### 1. GameServices
- NPCManager added to service container
- Proper dependency injection 
- Lifecycle management

### 2. EventHandler  
- NPC events registered
- Proper event routing
- Response building

### 3. EventDispatcher
- Client event handling
- Event batching
- Queue management

## Best Practices Applied

### SOLID Principles
- **S**: NPCManager has single responsibility
- **O**: Extensible for new NPC types
- **L**: Substitutable implementations
- **I**: Focused interfaces
- **D**: Depends on abstractions

### Memory Safety
- RAII with smart pointers
- Exception-safe operations
- Proper resource cleanup
- No memory leaks

### Concurrency
- Thread-safe operations
- Proper mutex usage
- Deadlock prevention
- Atomic operations

### Error Handling
- Exception safety guarantees
- Graceful error recovery
- Detailed error logging
- Transaction safety

## Future Enhancements

1. **NPC Behavior System**
   - AI state machines
   - Pathfinding integration
   - Dynamic responses

2. **Performance Optimizations**
   - Spatial indexing for chunk queries
   - NPC culling based on distance
   - Incremental loading

3. **Advanced Features**
   - Dynamic NPC spawning
   - Conditional NPC visibility
   - NPC instance management

## Dependencies
- Database system (PostgreSQL via pqxx)
- Event system (EventHandler, EventDispatcher)
- Logging system (Logger)
- JSON parsing (nlohmann/json)
- Thread safety (std::mutex)

## Testing Recommendations
1. Unit tests for NPCManager operations
2. Integration tests for database queries  
3. Load tests for concurrent access
4. Memory leak detection
5. Performance benchmarking