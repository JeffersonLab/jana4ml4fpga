#include "EvioSroBlockSource.h"

#include <JANA/JApplication.h>
#include <JANA/JEvent.h>
#include <JANA/JEventSourceGeneratorT.h>

EvioSroBlockSource::EvioSroBlockSource(std::string resource_name, JApplication* app) : JEventSource(resource_name, app) {
    SetTypeName(NAME_OF_THIS);
    SetLevel(JEventLevel::Timeslice);
    SetCallbackStyle(CallbackStyle::ExpertMode);
}

void EvioSroBlockSource::Open() {
    GetApplication()->SetDefaultParameter("evio6_file:parse", m_parse_enabled, "false = read blocks but skip parsing (pure I/O measurement; no frames reach the output)");
    GetApplication()->SetDefaultParameter("evio6_file:lazy_parse", m_lazy_parse, "true = decode only ECAL ROC banks up front; the unfolder decodes the rest per selected frame (false = eager full decode)");
    GetApplication()->SetDefaultParameter("evio6_file:reader", m_reader_mode, "'fread' (copying baseline) or 'mmap' (zero-copy mapped input)");
    m_reader = std::make_unique<sro::SroBlockReader>(std::vector<std::string>{GetResourceName()}, m_reader_mode == "mmap");
}

void EvioSroBlockSource::Close() {
    LOG_INFO(GetLogger()) << "EvioSroBlockSource: read " << m_blocks_read << " blocks ("
                          << m_reader->TruncatedTailBlocks() << " truncated tail blocks dropped); parse stats: "
                          << m_run_stats.ToString() << LOG_END;
    m_reader.reset();
}

JEventSource::Result EvioSroBlockSource::Emit(JEvent& event) {
    if (!m_reader->ReadNextBlock(m_raw_block)) {
        return Result::FailureFinished;
    }
    m_blocks_read++;

    // The block is parsed right here, in the source, on the source thread -
    // the simplest correct topology. (Optimization phases may move parsing into
    // the unfolder's Preprocess to parallelize it; measure against this first.)
    auto* block_data = new sro::SroBlockData();
    block_data->block_number = m_raw_block.block_number;
    if (m_parse_enabled && m_lazy_parse) {
        // Lazy mode: only the read happens on the source thread. The block
        // carries its raw body - zero-copy from the mapping when mmap-read,
        // otherwise by taking the fread buffer - and SroFrameUnfolder::
        // Preprocess parses it on the parallel map arrow.
        if (m_raw_block.mapping != nullptr) {
            block_data->external_body = m_raw_block.body;
            block_data->body_owner = m_raw_block.mapping;
        } else {
            block_data->body_words = std::move(m_raw_block.words);
        }
        block_data->body_word_count = static_cast<uint32_t>(m_raw_block.body_word_count);
        block_data->event_count = m_raw_block.event_count;
        block_data->parse_pending = true;
    } else if (m_parse_enabled) {
        sro::ParseBlockBody(m_raw_block.body, m_raw_block.body_word_count, m_raw_block.event_count, *block_data);
        m_run_stats.Add(block_data->stats);
    }

    event.SetEventNumber(m_raw_block.block_number);
    event.Insert(block_data);
    return Result::Success;
}

template <>
double JEventSourceGeneratorT<EvioSroBlockSource>::CheckOpenable(std::string resource_name) {
    // Higher confidence than CDAQfile's 0.5: SRO streaming files carry ".evio."
    // with a numeric split suffix (sro_000791.evio.00000).
    if (resource_name.find(".evio") != std::string::npos) {
        return 0.6;
    }
    return 0.0;
}
