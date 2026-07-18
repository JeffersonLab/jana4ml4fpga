// CDAQfileMT - EVIO file source plugin with a runtime-switchable PARALLEL parse path.
//
// Usage: load INSTEAD of the (untouched) CDAQfile plugin:
//   -Pplugins=CDAQfileMT,...           -> serial source, identical to CDAQfile (default)
//   -Pplugins=CDAQfileMT,... -Pevio:parallel=1
//                                      -> block topology: sequential arrow does only the
//                                         raw block read; EVIO parsing runs on the
//                                         PARALLEL disentangler arrow (scales with nthreads)
//
// Why: JANA profiling showed the serial source arrow (read+parse inside GetEvent)
// caps the whole pipeline at ~154 Hz regardless of thread count (MT plan, DECISIONS #10).
//
// Tunables (parallel mode): evio:block_queue_threshold (default 32 blocks).
#include <JANA/JApplication.h>
#include <JANA/JEventSourceGeneratorT.h>
#include <JANA/Engine/JTopologyBuilder.h>
#include <JANA/Engine/JBlockSourceArrow.h>
#include <JANA/Engine/JBlockDisentanglerArrow.h>
#include <JANA/Engine/JEventProcessorArrow.h>
#include <JANA/Services/JComponentManager.h>

#include "ParallelEvioBlockSource.h"
#include "SerialEvioFileSource.h"

namespace {

std::shared_ptr<JArrowTopology> ConfigureParallelEvioTopology(
        std::shared_ptr<JArrowTopology> topology, size_t block_queue_threshold,
        bool block_mode, size_t event_queue_threshold, size_t max_events_per_item) {

    // File names: the CLI added them via app->Add(name); the component manager
    // resolved them into (unused) serial sources - take their resource names.
    std::vector<std::string> filenames;
    for (auto* src : topology->component_manager->get_evt_srces()) {
        filenames.push_back(src->GetResourceName());
    }
    // ParallelEvioBlockSource pops from the back - reverse to keep CLI file order
    std::reverse(filenames.begin(), filenames.end());

    auto source = new ParallelEvioBlockSource(filenames);

    auto block_queue = new JMailbox<EVIOBlockedEvent*>;
    auto event_queue = new JMailbox<std::shared_ptr<JEvent>>;
    block_queue->set_threshold(block_queue_threshold);
    event_queue->set_threshold(event_queue_threshold);

    auto block_source_arrow = new JBlockSourceArrow<EVIOBlockedEvent>(
        "evio_block_read", source, block_queue);
    auto disentangler_arrow = new JBlockDisentanglerArrow<EVIOBlockedEvent>(
        "evio_parse", source, block_queue, event_queue, topology->event_pool);
    auto processor_arrow = new JEventProcessorArrow(
        "processors", event_queue, nullptr, topology->event_pool);

    if (block_mode) {
        // mmap_block: one work item = one physical EVIO block (or evio:events_per_item
        // events). The disentangler reserves chunksize*max_events_per_block slots on
        // the event queue, so max_events_per_block must cover the largest work item
        // and the event queue threshold must be at least that big.
        block_source_arrow->set_chunksize(1);   // one item = up to ~64 MB memcpy
        disentangler_arrow->set_chunksize(1);
        disentangler_arrow->set_max_events_per_block(max_events_per_item);

        // Workers hold an arrow assignment for checkin_time (default 500 ms!),
        // sleeping with unbounded exponential backoff on ComeBackLater. With bursty
        // block-granular flow this parks nearly all workers asleep while work waits
        // (measured: 0.2 cores busy on 24 threads). Short checkin makes workers
        // re-shop for runnable arrows promptly.
        auto checkin = std::chrono::microseconds(200);
        block_source_arrow->set_checkin_time(checkin);
        disentangler_arrow->set_checkin_time(checkin);
        processor_arrow->set_checkin_time(checkin);
    } else {
        // Per-event modes: amortize scheduler transitions (global scheduler lock):
        // with the default chunksize of 1, every item costs several scheduler
        // round-trips, capping the pipeline at a few kHz regardless of thread count.
        block_source_arrow->set_chunksize(64);
        disentangler_arrow->set_chunksize(8);
    }

    for (auto* proc : topology->component_manager->get_evt_procs()) {
        processor_arrow->add_processor(proc);
    }

    topology->arrows.push_back(block_source_arrow);
    topology->arrows.push_back(disentangler_arrow);
    topology->arrows.push_back(processor_arrow);
    topology->sources.push_back(block_source_arrow);
    topology->sinks.push_back(processor_arrow);

    block_source_arrow->attach(disentangler_arrow);
    disentangler_arrow->attach(processor_arrow);

    return topology;
}

}  // namespace

extern "C" {
void InitPlugin(JApplication* app) {
    InitJANAPlugin(app);

    bool parallel = false;
    app->SetDefaultParameter("evio:parallel", parallel,
                             "EVIO source mode: 0 = serial (identical to CDAQfile), "
                             "1 = parallel block parsing (block read sequential, "
                             "parse on worker threads)");

    // The serial generator is registered in BOTH modes:
    //  - serial mode: it IS the source;
    //  - parallel mode: it only satisfies source-name resolution; the custom
    //    topology below never runs these sources (Open is never called).
    app->Add(new JEventSourceGeneratorT<SerialEvioFileSource>);

    if (parallel) {
        // The pool-based parser path cannot back-pressure a bounded event pool:
        // JEventPool::get() returns nullptr when exhausted (segfault/exception in
        // the disentangler). Queues still bound memory (block_queue_threshold and
        // the event queue threshold), so unlimited in-flight events are safe here.
        app->SetParameterValue("jana:limit_total_events_in_flight", false);

        std::string reader = "hdevio";
        app->SetDefaultParameter("evio:reader", reader,
                                 "Parallel EVIO reader backend: 'hdevio', 'mmap' or 'mmap_block'");
        const bool block_mode = (reader == "mmap_block");

        // Memory-residency knobs. Approx resident bytes =
        //   block_queue_threshold * work-item size (~64 MB for whole physical blocks)
        //   + event_queue_threshold * parsed event footprint.
        size_t threshold = block_mode ? 8 : 32;
        app->SetDefaultParameter("evio:block_queue_threshold", threshold,
                                 "Parallel EVIO mode: max raw work items buffered between "
                                 "the read arrow and the parse arrow (memory cap)");

        size_t event_queue_threshold = block_mode ? 16384 : 512;
        app->SetDefaultParameter("evio:event_queue_threshold", event_queue_threshold,
                                 "Parallel EVIO mode: max parsed events queued for processing");

        // Sub-block batching (evio:events_per_item): the disentangler RESERVES
        // chunk*max_events_per_item event-queue slots for the whole duration of a
        // work-item parse, so concurrent parses = event_queue_threshold / item size.
        // Whole 4258-event blocks with a 16k queue = only ~2 concurrent parses(!);
        // 512-event items = 32-way parse concurrency with the same memory bound.
        uint32_t events_per_item = 512;
        app->SetDefaultParameter("evio:events_per_item", events_per_item,
                                 "mmap_block mode: events per parallel work item "
                                 "(0 = one whole physical EVIO block per item)");

        size_t max_events_per_item = (events_per_item > 0) ? events_per_item : 8192;
        app->SetDefaultParameter("evio:max_events_per_item", max_events_per_item,
                                 "mmap_block mode: reservation cap = largest events per work item");

        if (block_mode) {
            // Keep the JEvent pool large enough that it NEVER runs empty: in-flight
            // events = event queue backlog + (concurrent work items x item size) +
            // worker chunks. An empty pool triggers a thundering herd of fresh
            // JEvent+JFactorySet construction on all workers at once (measured:
            // 24T collapse to ~0.7 kHz). 4x the queue threshold covers the bound.
            app->SetParameterValue("jana:event_pool_size", (int)(event_queue_threshold * 4));
        }

        app->GetService<JTopologyBuilder>()->set_configure_fn(
            [threshold, block_mode, event_queue_threshold, max_events_per_item](
                std::shared_ptr<JArrowTopology> topology) {
                return ConfigureParallelEvioTopology(std::move(topology), threshold, block_mode,
                                                     event_queue_threshold, max_events_per_item);
            });
    }
}
}
