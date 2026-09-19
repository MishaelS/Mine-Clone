#pragma once

#include "world/Chunk.hpp"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

class TerrainNoise;

// Chunk-grid coordinates, packed the same shape as World::ChunkCoordinates
// but kept as its own type here rather than shared: ChunkWorkerPool doesn't
// need (and shouldn't depend on) anything else from World.hpp, and a
// second 2-int struct is cheaper than adding a dependency edge the wrong
// way (World depends on ChunkWorkerPool, never the reverse).
struct ChunkCoord { int x, z; };

// Constructs a Chunk via shared_ptr, using a deleter that only actually
// runs ~Chunk() (which calls UnloadMesh on its GPU mesh(es) - an OpenGL
// call, valid only on the thread holding the GL context) when called from
// the thread that made the very first Chunk this way for this process.
// That's assumed to be the main/GL thread - true in this project, since
// GameEngine only ever owns at most one World (and so at most one
// generation/meshing pipeline) at a time, and every World is constructed
// from the main thread before its own ChunkWorkerPool ever dispatches a
// background job. A Chunk whose last shared_ptr reference is dropped on
// some other thread (a worker finishing a job for a chunk World has since
// unloaded - see ChunkWorkerPool's own class comment) is queued instead of
// freed immediately; reclaim_pending_chunk_destroys() actually deletes
// those, and must only ever be called from the main thread.
std::shared_ptr<Chunk> make_chunk(Vector3 position);

// Actually destroys every Chunk make_chunk()'s deleter deferred because it
// would otherwise have run off the main thread since the last call - see
// make_chunk()'s own comment. Cheap (nothing left to compute, just
// teardown), but must run on the main thread, since it's what actually
// invokes ~Chunk() (including its UnloadMesh calls) for those chunks.
// World::integrate_worker_results() calls this once a frame via
// ChunkWorkerPool::reclaim_pending_destroys().
void reclaim_pending_chunk_destroys();

// A chunk's terrain generation (terrain + caves + structures, or a disk
// load in their place, then compute_lighting()) - the pure-CPU work
// World::generate_chunk() used to do synchronously and inline, factored out
// so a background worker can run it too. `noise` must be a TerrainNoise
// instance private to whichever thread is calling this - never World's own
// shared instance (see PerlinNoise.hpp's "single-threaded" caveat on
// fractal(): two threads driving the same FastNoise2 fractal node
// concurrently would race on its internal octave/gain state). Returns a
// freshly made_chunk() Chunk, not yet inserted into any World - the caller
// does that once the result comes back (on the main thread).
std::shared_ptr<Chunk> generate_chunk_data(int chunk_x, int chunk_z, uint32_t world_seed,
                                            const std::optional<std::string>& save_directory,
                                            TerrainNoise& noise);

// Runs chunk generation and mesh building on background worker threads, so
// World's main-thread call sites only ever submit work and later collect
// finished results - the whole point being that generating/meshing an
// entire newly-in-range ring of chunks no longer has to happen within a
// single tick/frame (see World::update_chunk_states()'s own comment on why
// that used to stall). Jobs are kept ordered by Chebyshev distance from the
// most recently submitted observer position, nearest first - the same
// distance-priority idea real Minecraft's own chunk tickets use, so a
// player's immediate surroundings finish before anything further out.
//
// Every Chunk this pool ever hands back was built with make_chunk() (see
// its own comment) rather than std::make_shared, specifically so a mesh
// job's own shared_ptr references (kept alive independently of World::
// chunks - see MeshJobInput) can't cause ~Chunk()'s GL calls to run off the
// main thread if World unloads that chunk while the job is still in
// flight. World must call reclaim_pending_destroys() once a frame for that
// mechanism to actually free anything.
class ChunkWorkerPool {
public:
    // `worker_count` overrides the default (half the hardware threads,
    // capped at 4, minimum 1) - 0 means "use the default". Spawns every
    // worker thread immediately; each sits idle (blocked on the job queue's
    // condition variable) until work is submitted.
    ChunkWorkerPool(uint32_t world_seed, std::optional<std::string> save_directory, unsigned int worker_count = 0);

    // Stops every worker thread (see shutdown()) and reclaims anything left
    // in pending_destroy - World's own destructor calls shutdown()
    // explicitly and early instead of relying on this, since every other
    // Chunk this pool touched (via World::chunks) must already be safe to
    // destroy - workers fully stopped, nothing mid-read - before ~World()
    // gets anywhere near tearing down its own chunk map. By the time this
    // destructor runs (as part of ~World()'s automatic member teardown),
    // shutdown() here is a no-op (idempotent - see its own comment).
    ~ChunkWorkerPool();

    ChunkWorkerPool(const ChunkWorkerPool&) = delete;
    ChunkWorkerPool& operator=(const ChunkWorkerPool&) = delete;

    // Stops every worker thread: signals them to exit once their current
    // job (if any) finishes, wakes them, and joins every thread - blocks
    // until all of them have actually stopped. Idempotent (a second call
    // after the first is a harmless no-op) so both World's explicit call
    // and the destructor's own can safely both run. Must complete before
    // anything that could destroy a Chunk this pool has touched runs -
    // otherwise a worker could still be mid-read of it (see Chunk::
    // data_mutex()) or, worse, be the thread that ends up dropping its
    // last shared_ptr reference.
    void shutdown();

    // Queues background generation for each coordinate - World is
    // responsible for its own dedup (only calling this for a coordinate
    // it isn't already waiting on, see World's own `generating` set); this
    // pool doesn't track that itself. `observer_chunk` (re-)scores every
    // job currently queued (not just this batch) by Chebyshev distance from
    // it, nearest first - see the class comment.
    void submit_gen_jobs(const std::vector<ChunkCoord>& coords, ChunkCoord observer_chunk);

    // One mesh-build job per entry. `chunks[0]` is the target chunk;
    // `chunks[1..8]` are, in order, west/east/north/south/northwest/
    // northeast/southwest/southeast (matching Chunk::build_mesh_data()'s
    // own parameter order) - any may be null, at the edge of the loaded
    // world, same as Chunk::build_mesh() has always allowed. The caller
    // (World::request_remesh()) resolves these shared_ptrs itself, so this
    // pool never needs to touch World::chunks or know how chunks are
    // stored.
    struct MeshJobInput {
        int chunk_x, chunk_z;
        std::array<std::shared_ptr<Chunk>, 9> chunks;
    };
    void submit_mesh_jobs(std::vector<MeshJobInput> jobs, ChunkCoord observer_chunk);

    struct GenResult { int chunk_x, chunk_z; std::shared_ptr<Chunk> chunk; };
    struct MeshResult { int chunk_x, chunk_z; std::shared_ptr<Chunk> chunk; ChunkMeshBuildResult mesh_data; };

    // Pops up to `max_count` finished results (fewer if that's all there
    // are, none if there aren't any yet) - called once a frame from World::
    // integrate_worker_results() with a small budget, so a large background
    // backlog can never spike a single frame's cost - see its own comment.
    std::vector<GenResult> drain_gen_results(size_t max_count);
    std::vector<MeshResult> drain_mesh_results(size_t max_count);

    // Forwards to reclaim_pending_chunk_destroys() (see its own comment) -
    // exposed here too so World only ever has to talk to worker_pool, not
    // to a second free function. Main-thread only.
    void reclaim_pending_destroys();

private:
    struct Job {
        enum class Kind { Generate, Mesh } kind;
        int chunk_x = 0, chunk_z = 0;
        std::array<std::shared_ptr<Chunk>, 9> mesh_chunks; // Kind::Mesh only
        int priority_distance = 0; // Chebyshev distance from the observer chunk as of the last (re-)sort
    };

    void worker_loop();

    // Re-scores every currently queued job's priority_distance against
    // `observer_chunk` and re-sorts ascending (nearest first) - called
    // under jobs_mutex by both submit_gen_jobs()/submit_mesh_jobs(), so a
    // batch submitted while the observer has moved further than an already-
    // queued job still gets ordered correctly relative to it, not just
    // appended to the end.
    void resort_jobs_locked(ChunkCoord observer_chunk);

    uint32_t world_seed;
    std::optional<std::string> save_directory;

    std::vector<std::thread> workers;
    std::atomic<bool> stopping{false};
    bool stopped = false; // guards shutdown()'s own idempotency - not atomic, only ever touched under jobs_mutex

    std::mutex jobs_mutex;
    std::condition_variable jobs_cv;
    std::deque<Job> jobs; // kept sorted ascending by priority_distance

    std::mutex results_mutex;
    std::deque<GenResult> gen_results;
    std::deque<MeshResult> mesh_results;
};
