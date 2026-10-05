// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/threading/job_graph_uve.h"

#include <deque>
#include <unordered_set>
#include <utility>
#include <vector>

#include "uve/logging/logging_macros_uve.h"

namespace UVE::Threading {

JobGraphObjectHandleUVE JobGraphUVE::AddJobUVE(JobUVE job) {
    if (m_executed) {
        UVE_ERROR("JobGraphUVE: AddJobUVE() called after ExecuteUVE() - ignored");
        return kInvalidJobGraphObjectHandleUVE;
    }
    const auto handle = static_cast<JobGraphObjectHandleUVE>(m_objects.size());
    auto object = std::make_unique<ObjectUVE>();
    object->job = std::move(job);
    m_objects.push_back(std::move(object));
    return handle;
}

bool JobGraphUVE::CanReachUVE(const JobGraphObjectHandleUVE from, const JobGraphObjectHandleUVE to) const {
    std::unordered_set<JobGraphObjectHandleUVE> visited;
    std::deque<JobGraphObjectHandleUVE> frontier{from};
    while (!frontier.empty()) {
        const JobGraphObjectHandleUVE current = frontier.front();
        frontier.pop_front();
        if (current == to) {
            return true;
        }
        if (!visited.insert(current).second) {
            continue;
        }
        for (const JobGraphObjectHandleUVE successor : m_objects[current]->successors) {
            frontier.push_back(successor);
        }
    }
    return false;
}

bool JobGraphUVE::AddDependencyUVE(const JobGraphObjectHandleUVE dependent, const JobGraphObjectHandleUVE dependency) {
    if (m_executed) {
        UVE_ERROR("JobGraphUVE: AddDependencyUVE() called after ExecuteUVE() - ignored");
        return false;
    }
    if (dependent == dependency || dependent >= m_objects.size() || dependency >= m_objects.size()) {
        UVE_ERROR("JobGraphUVE: AddDependencyUVE() received an invalid or self-referential handle pair");
        return false;
    }
    // The graph edge runs dependency -> dependent (dependency's completion unblocks dependent),
    // so a cycle exists iff dependent can already reach dependency along existing edges.
    if (CanReachUVE(dependent, dependency)) {
        UVE_ERROR("JobGraphUVE: AddDependencyUVE() rejected - would create a cycle");
        return false;
    }
    m_objects[dependency]->successors.push_back(dependent);
    return true;
}

void JobGraphUVE::ExecuteUVE(IThreadPoolUVE& pool) {
    if (m_executed) {
        UVE_ERROR("JobGraphUVE: ExecuteUVE() called more than once - ignored");
        return;
    }
    m_executed = true;
    if (m_objects.empty()) {
        return;
    }

    for (const std::unique_ptr<ObjectUVE>& object : m_objects) {
        for (const JobGraphObjectHandleUVE successor : object->successors) {
            m_objects[successor]->remainingDependencies.fetch_add(1, std::memory_order_relaxed);
        }
    }
    // Snapshot which objects start with zero dependencies BEFORE submitting anything. This must
    // not be folded into the submission loop below: the moment the first root object is submitted,
    // a worker thread can start running it concurrently with this function still walking later
    // indices - and that worker's own cascade (SubmitObjectUVE's fetch_sub) can decrement a later
    // object's remainingDependencies to zero before this loop ever reaches it. A live
    // `.load() == 0` check at that point would then submit the same object a second time, racing
    // against the cascade's own (correctly deduplicated, fetch_sub-return-value-gated) submission
    // of it. Deciding "is this a root" from a snapshot taken while nothing is running yet removes
    // that race entirely: every object's fate is either "submitted here, once" (it started at zero)
    // or "submitted exactly once by whichever cascade decrement brings it to zero" (it didn't) -
    // never both.
    std::vector<bool> isRootObject(m_objects.size(), false);
    for (std::size_t index = 0; index < m_objects.size(); ++index) {
        isRootObject[index] = (m_objects[index]->remainingDependencies.load(std::memory_order_relaxed) == 0);
    }
    // Every object is pending exactly once, counted before any of them can possibly start -
    // matching JobCounterUVE's own "increment before the job can run" contract.
    for (std::size_t index = 0; index < m_objects.size(); ++index) {
        m_completionCounter.IncrementUVE();
    }
    for (std::size_t index = 0; index < m_objects.size(); ++index) {
        if (isRootObject[index]) {
            SubmitObjectUVE(pool, index);
        }
    }
}

void JobGraphUVE::SubmitObjectUVE(IThreadPoolUVE& pool, const JobGraphObjectHandleUVE index) {
    pool.SubmitUVE([this, &pool, index] {
        ObjectUVE& object = *m_objects[index];
        if (object.job) {
            object.job();
        }
        for (const JobGraphObjectHandleUVE successor : object.successors) {
            // fetch_sub returns the pre-decrement value; the one thread that observes it drop
            // from 1 to 0 is the one whose dependency completion actually unblocks this successor
            // (every other predecessor of the same successor sees a value > 1 and does nothing),
            // so exactly one submission happens no matter how many predecessors finish at once.
            if (m_objects[successor]->remainingDependencies.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                SubmitObjectUVE(pool, successor);
            }
        }
        m_completionCounter.DecrementAndNotifyUVE();
    });
}

void JobGraphUVE::WaitUVE() {
    m_completionCounter.WaitUVE();
}

} // namespace UVE::Threading
