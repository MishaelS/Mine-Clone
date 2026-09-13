#include "world/ChunkWorkerPool.hpp"
#include "core/TerrainNoise.hpp"
#include "worldgen/StructureGenerator.hpp"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <thread>

namespace {
    // See make_chunk()/reclaim_pending_chunk_destroys()'s own comments in
    // ChunkWorkerPool.hpp - file-scope rather than members of ChunkWorkerPool
    // itself, since generate_chunk_data() (a free function, called directly
    // by World's synchronous bootstrap path too, not just by a worker
    // thread) needs to build chunks the exact same way without needing a
    // ChunkWorkerPool instance to do it through. Safe as global state
    // because this project only ever has one World (and so one generation
    // pipeline) alive at a time.
    std::once_flag g_main_thread_flag;
    std::thread::id g_main_thread_id;

    std::mutex g_pending_destroy_mutex;
    std::vector<Chunk*> g_pending_destroy;

    void chunk_deleter(Chunk* chunk) {
        if (std::this_thread::get_id() == g_main_thread_id) {
            delete chunk; // safe here: this is the thread holding the GL context, so ~Chunk()'s UnloadMesh calls are valid
        } else {
            std::lock_guard<std::mutex> lock(g_pending_destroy_mutex);
            g_pending_destroy.push_back(chunk);
        }
    }

    // saves/<world>/chunks/<chunk_x>_<chunk_z>.chunk - the same shape as
    // World::chunk_file_path(), duplicated here (rather than exposed from
    // World) since this is a free function that has no World to call back
    // into, and it's one line.
    std::string worker_chunk_file_path(const std::string& save_directory, int chunk_x, int chunk_z) {
        return save_directory + "/chunks/" + std::to_string(chunk_x) + "_" + std::to_string(chunk_z) + ".chunk";
    }

    int chebyshev(ChunkCoord a, ChunkCoord b) {
        return std::max(std::abs(a.x - b.x), std::abs(a.z - b.z));
    }
}

std::shared_ptr<Chunk> make_chunk(Vector3 position)
{
    std::call_once(g_main_thread_flag, [] { g_main_thread_id = std::this_thread::get_id(); });
    return std::shared_ptr<Chunk>(new Chunk(position), chunk_deleter);
}

void reclaim_pending_chunk_destroys()
{
    std::vector<Chunk*> to_delete;
    {
        std::lock_guard<std::mutex> lock(g_pending_destroy_mutex);
        to_delete.swap(g_pending_destroy);
    }
    for (Chunk* chunk : to_delete) delete chunk; // main thread only - see the header's own comment
}

std::shared_ptr<Chunk> generate_chunk_data(int chunk_x, int chunk_z, uint32_t world_seed,
                                            const std::optional<std::string>& save_directory,
                                            TerrainNoise& noise)
{
    Vector3 position = {
        chunk_x * static_cast<float>(CHUNK_SIZE),
        static_cast<float>(MIN_WORLD_Y), // a Chunk's own local Y 0 sits here in world space
        chunk_z * static_cast<float>(CHUNK_SIZE),
    };
    std::shared_ptr<Chunk> chunk = make_chunk(position);

    // Same "previously-modified chunk loads its exact saved state instead
    // of regenerating" logic as World::generate_chunk() has always had -
    // see its own comment.
    bool loaded_from_disk = save_directory && chunk->load_from_file(worker_chunk_file_path(*save_directory, chunk_x, chunk_z));
    if (!loaded_from_disk) {
        chunk->generate_terrain(noise);
        chunk->generate_ores(world_seed, chunk_x, chunk_z);
        chunk->carve_caves(world_seed, chunk_x, chunk_z);
        StructureGenerator(world_seed).generate(*chunk, noise, chunk_x, chunk_z);
    }
    chunk->compute_lighting(); // never persisted - cheap to rebuild either way

    return chunk;
}

ChunkWorkerPool::ChunkWorkerPool(uint32_t seed, std::optional<std::string> directory, unsigned int worker_count)
    : world_seed(seed)
    , save_directory(std::move(directory))
{
    unsigned int count = worker_count > 0 ? worker_count : std::max(1u, std::thread::hardware_concurrency() - 1);
    workers.reserve(count);
    for (unsigned int i = 0; i < count; ++i) {
        workers.emplace_back([this] { worker_loop(); });
    }
}

ChunkWorkerPool::~ChunkWorkerPool()
{
    shutdown();
    reclaim_pending_destroys(); // anything a worker deferred right as it exited (see shutdown()'s own comment)
}

void ChunkWorkerPool::shutdown()
{
    {
        std::lock_guard<std::mutex> lock(jobs_mutex);
        if (stopped) return; // already shut down - World's explicit call plus the destructor's own would otherwise both run this
        stopped = true;
        stopping.store(true);
    }
    jobs_cv.notify_all();
    for (std::thread& worker : workers) {
        if (worker.joinable()) worker.join();
    }
    workers.clear();
}

void ChunkWorkerPool::resort_jobs_locked(ChunkCoord observer_chunk)
{
    for (Job& job : jobs) {
        job.priority_distance = chebyshev({job.chunk_x, job.chunk_z}, observer_chunk);
    }
    std::sort(jobs.begin(), jobs.end(), [](const Job& a, const Job& b) {
        return a.priority_distance < b.priority_distance;
    });
}

void ChunkWorkerPool::submit_gen_jobs(const std::vector<ChunkCoord>& coords, ChunkCoord observer_chunk)
{
    if (coords.empty()) return;
    {
        std::lock_guard<std::mutex> lock(jobs_mutex);
        for (const ChunkCoord& coord : coords) {
            Job job;
            job.kind = Job::Kind::Generate;
            job.chunk_x = coord.x;
            job.chunk_z = coord.z;
            jobs.push_back(std::move(job));
        }
        resort_jobs_locked(observer_chunk);
    }
    jobs_cv.notify_all();
}

void ChunkWorkerPool::submit_mesh_jobs(std::vector<MeshJobInput> mesh_jobs, ChunkCoord observer_chunk)
{
    if (mesh_jobs.empty()) return;
    {
        std::lock_guard<std::mutex> lock(jobs_mutex);
        for (MeshJobInput& input : mesh_jobs) {
            Job job;
            job.kind = Job::Kind::Mesh;
            job.chunk_x = input.chunk_x;
            job.chunk_z = input.chunk_z;
            job.mesh_chunks = std::move(input.chunks);
            jobs.push_back(std::move(job));
        }
        resort_jobs_locked(observer_chunk);
    }
    jobs_cv.notify_all();
}

std::vector<ChunkWorkerPool::GenResult> ChunkWorkerPool::drain_gen_results(size_t max_count)
{
    std::vector<GenResult> out;
    std::lock_guard<std::mutex> lock(results_mutex);
    while (!gen_results.empty() && out.size() < max_count) {
        out.push_back(std::move(gen_results.front()));
        gen_results.pop_front();
    }
    return out;
}

std::vector<ChunkWorkerPool::MeshResult> ChunkWorkerPool::drain_mesh_results(size_t max_count)
{
    std::vector<MeshResult> out;
    std::lock_guard<std::mutex> lock(results_mutex);
    while (!mesh_results.empty() && out.size() < max_count) {
        out.push_back(std::move(mesh_results.front()));
        mesh_results.pop_front();
    }
    return out;
}

void ChunkWorkerPool::reclaim_pending_destroys()
{
    reclaim_pending_chunk_destroys();
}

void ChunkWorkerPool::worker_loop()
{
    // Private to this thread - never World's own shared TerrainNoise (see
    // generate_chunk_data()'s own comment). Built lazily, only once this
    // thread actually has a Generate job to do.
    std::optional<TerrainNoise> local_noise;

    while (true) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(jobs_mutex);
            jobs_cv.wait(lock, [this] { return stopping.load() || !jobs.empty(); });
            if (jobs.empty()) {
                if (stopping.load()) return;
                continue;
            }
            job = std::move(jobs.front());
            jobs.pop_front();
        }

        if (job.kind == Job::Kind::Generate) {
            if (!local_noise.has_value()) local_noise.emplace(world_seed);
            std::shared_ptr<Chunk> chunk = generate_chunk_data(job.chunk_x, job.chunk_z, world_seed, save_directory, *local_noise);

            std::lock_guard<std::mutex> lock(results_mutex);
            gen_results.push_back({job.chunk_x, job.chunk_z, std::move(chunk)});
        } else {
            // Shared-lock this chunk plus every present neighbor's own
            // data_mutex for the whole build_mesh_data() call, in a
            // consistent order (here: ascending pointer value) across every
            // worker - any single total order works to prevent a lock-order
            // deadlock between two concurrent mesh jobs that share a
            // neighbor chunk, and pointer identity is a cheap one that
            // doesn't require decoding chunk coordinates back out of a
            // Chunk (which doesn't store its own). See Chunk::data_mutex()'s
            // own comment for the full discipline this is one half of.
            std::vector<Chunk*> present;
            present.reserve(9);
            for (const std::shared_ptr<Chunk>& c : job.mesh_chunks) {
                if (c) present.push_back(c.get());
            }
            std::sort(present.begin(), present.end());

            std::vector<std::shared_lock<std::shared_mutex>> locks;
            locks.reserve(present.size());
            for (Chunk* c : present) locks.emplace_back(c->data_mutex());

            Chunk* self = job.mesh_chunks[0].get();
            ChunkMeshBuildResult mesh_data = self->build_mesh_data(
                job.mesh_chunks[1].get(), job.mesh_chunks[2].get(), job.mesh_chunks[3].get(), job.mesh_chunks[4].get(),
                job.mesh_chunks[5].get(), job.mesh_chunks[6].get(), job.mesh_chunks[7].get(), job.mesh_chunks[8].get());

            locks.clear(); // release every data_mutex before publishing the result - nothing below still needs them

            std::lock_guard<std::mutex> lock(results_mutex);
            mesh_results.push_back({job.chunk_x, job.chunk_z, job.mesh_chunks[0], std::move(mesh_data)});
        }
    }
}
