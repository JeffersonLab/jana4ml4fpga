#include "MmapEvioSource.h"

#include <JANA/JApplication.h>
#include <JANA/JEvent.h>

#include <rawdataparser/EVIOBlockedEvent.h>
#include <rawdataparser/EVIOBlockedEventParser.h>
#include <rawdataparser/swap_bank.h>
#include <services/log/Log_service.h>

MmapEvioSource::MmapEvioSource(std::string resource_name, JApplication* app)
    : JEventSource(std::move(resource_name), app) {
    SetTypeName(NAME_OF_THIS);
    SetCallbackStyle(CallbackStyle::ExpertMode);  // use Emit() instead of legacy GetEvent()
    EnableProcessParallel(true);                  // parse on the parallel map arrow
}

void MmapEvioSource::Open() {
    auto app = GetApplication();
    m_log = app->GetService<Log_service>()->logger("evio");

    app->SetDefaultParameter("daq:srs_window_raw:ntsamples", m_parser_config.NSAMPLES_GEMSRS,
                             "Number of GEM SRS time samples per APV");
    app->SetDefaultParameter("evio:prefetch_mb", m_prefetch_mb,
                             "mmap reader: madvise(WILLNEED) read-ahead depth in MB (0 = off)");

    m_reader = std::make_unique<MmapEvioReader>(GetResourceName());
    m_reader->set_prefetch_mb(m_prefetch_mb);
    m_log->info("Opened EVIO file (mmap, parallel parse): {}", GetResourceName());
}

void MmapEvioSource::Close() {
    m_reader.reset();
    m_log->info("Closed EVIO file: {}", GetResourceName());
}

JEventSource::Result MmapEvioSource::Emit(JEvent& event) {
    // SEQUENTIAL section - keep it at "walk + memcpy" only.
    auto* block = new EVIOBlockedEvent();
    auto result = m_reader->NextEvent(*block);

    if (result == MmapEvioReader::Result::kEndOfFile) {
        delete block;
        return Result::FailureFinished;
    }
    if (result == MmapEvioReader::Result::kError) {
        delete block;
        throw JException("MmapEvioSource: corrupt EVIO structure in %s", GetResourceName().c_str());
    }

    event.Insert(block);  // event owns the raw bank until recycle
    return Result::Success;
}

void MmapEvioSource::ProcessParallel(JEvent& event) const {
    // PARALLEL section (map arrow). The raw bank was inserted by Emit().
    auto* block = const_cast<EVIOBlockedEvent*>(event.GetSingle<EVIOBlockedEvent>());
    if (block == nullptr || block->data.empty()) {
        return;  // nothing to parse (should not happen for emitted events)
    }

    // HDEVIO contract: the parser expects NATIVE-endian words while swap_needed
    // stays true (legacy quirk #8) - perform the structural swap here, in parallel.
    if (block->swap_needed) {
        swap_bank(block->data.data(), block->data.data(),
                  static_cast<uint32_t>(block->data.size()));
    }

    EVIOBlockedEventParser parser;
    parser.Configure(m_parser_config);
    auto event_sptr = event.shared_from_this();
    parser.ParseEVIOBlockedEvent(*block, event_sptr);
}

std::string MmapEvioSource::GetDescription() {
    return "mmap-based EVIO file source with parallel deserialization";
}

template <>
double JEventSourceGeneratorT<MmapEvioSource>::CheckOpenable(std::string resource_name) {
    // Selected explicitly by the CDAQfileMT plugin when -Pevio:parallel=1;
    // claims .evio files strongly in that case (the serial generator is not
    // registered then, so there is no ambiguity).
    return (resource_name.find(".evio") != std::string::npos) ? 0.9 : 0.0;
}
