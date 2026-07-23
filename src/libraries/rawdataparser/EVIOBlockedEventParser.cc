#include <sstream>
#include <thread>
#include <mutex>
#include <atomic>

#include <rawdataparser/EVIOBlockedEventParser.h>
#include <rawdataparser/EVIOFileWriter.h>

using namespace std;

namespace {
// Data format errors can repeat on every event of a bad file. Limit how many
// times a given warning is printed so logs stay usable.
bool ShouldWarn(std::atomic<int> &counter, int limit = 20) {
    int n = ++counter;
    if (n == limit) jerr << "(Further warnings of this type will be suppressed)" << endl;
    return n <= limit;
}
} // anonymous namespace

// The EBEP namespace is used for things related to the optional output EVIO file.
namespace EBEP {

// The easiest way to capture incoming data to a file and have a way to
// test it works is to do it when the parser is called. This is used symmetrically
// by all event sources, file or network.
//
// The way the parser is currently used, temporary stack objects are created
// for each buffer that is parsed. Thus, we need to have some global objects
// to maintain the state of the output file. 

    static std::once_flag outfile_initialized;
    static std::shared_ptr<EVIOFileWriter> eviowriter;

/// This gets called exactly once, but only if EVIOBlockedEventParser::ParseEVIOBlockedEvent
/// gets called. It checks the global JApplication (via japp) to see if the 
/// "EVIO:output_file" config. parameter is set and not an empty string. If so,
/// An EVIOFileWriter object will be created which will open an output file
/// and write every block passed into ParseEVIOBlockedEvent into it. 
///
/// The EVIOFileWriter object is managed by a global shared_ptr so will be closed
/// at program exit.
    void InitializeEVIOOutputFile() {

        std::string filename = "";
        japp->SetDefaultParameter("EVIO:output_file", filename, "Name of optional EVIO output file to write events to. (leave empty to not to a file)");
        if (!filename.empty()) eviowriter = std::make_shared<EVIOFileWriter>(filename);
    }
} // end EBEP namespace

//-----------------------------------------
// EVIOBlockedEventParser (Constructor)
//-----------------------------------------
EVIOBlockedEventParser::EVIOBlockedEventParser() {


}

//-----------------------------------------
// ~EVIOBlockedEventParser (Destructor)
//-----------------------------------------
EVIOBlockedEventParser::~EVIOBlockedEventParser() {
    // Potentially free any memory from the shared pointers.
    events.clear();
}

//-----------------------------------------
// ParseEVIOBlockedEvent
///
/// This is called when using a JEventSource that has been given
/// a single JEvent object to write the event into.
//-----------------------------------------
std::vector<std::shared_ptr<JEvent>>
EVIOBlockedEventParser::ParseEVIOBlockedEvent(EVIOBlockedEvent &block, std::shared_ptr<JEvent> &preallocated_event) {
    return ParseEVIOBlockedEvent(block, &preallocated_event);
}

//-----------------------------------------
// ParseEVIOBlockedEvent
//
/// This should not be called directly but instead should be
/// called via one of the variants that takes either a JEventPool
/// or a vector of preallocated JEvents.
//-----------------------------------------
std::vector<std::shared_ptr<JEvent>>
EVIOBlockedEventParser::ParseEVIOBlockedEvent(EVIOBlockedEvent &block,
                                              std::shared_ptr<JEvent> *preallocated_event) {

    // Optionally write this to output EVIO file
    std::call_once(EBEP::outfile_initialized, EBEP::InitializeEVIOOutputFile);
    if (EBEP::eviowriter.get()) EBEP::eviowriter->WriteEventBuffer(block);

    // Clear our events vector.
    events.clear();

    // Keep running pointer to the 32bit word we are currently parsing
    uint32_t *iptr = block.data.data();

    // Extract number of events in this block at the event number of the first
    uint32_t M = 1;
    uint64_t event_num = 0;

    iptr++;
    uint32_t mask = 0xFF001000;
    if ((*iptr) >> 16 == 0xFF32) {

        // CDAQ BOR. Leave M=1

    } else if ((*iptr) >> 16 == 0xFF33) {

        // CDAQ Physics event
        M = iptr[2] & 0xFF;

        // Event number taken from first ROC's trigger bank
        uint64_t eventnum_lo = iptr[6];
        uint64_t eventnum_hi = 0; // Only lower 32bits in ROC trigger info.
        event_num = (eventnum_hi << 32) + (eventnum_lo);
    } else if (((*iptr) & mask) == mask) {
        // CODA Physics event
        M = *(iptr) & 0xFF;
        uint64_t eventnum_lo = iptr[4];
        uint64_t eventnum_hi = iptr[5];
        event_num = (eventnum_hi << 32) + (eventnum_lo);
    }

    // Sanity chack that this block does not contain a ridiculous number of events
    if (M > 200) {
        std::stringstream ss;
        ss << "Too many events in EVIO block (" << M << " > 200)";
        throw JException(ss.str());
    }

    // MIGRATION note (JANA2 2026.x): the JEventPool allocation path was removed
    // together with the old pool API. The caller always provides the JEvent now
    // (per-event sources parse one block into one event; block-batching designs
    // unpack blocks upstream and call this once per event bank).
    if (preallocated_event) {
        if (M > 1) {
            _DBG_
                    << " EVIOBlockedEventParser::ParseEVIOBlockedEvent passed single JEvent for block containing more than 1 event!"
                    << std::endl;
            throw JException("Multiple events in block when only single JEvent provided to copy them in to.");
        }
        (*preallocated_event)->SetEventNumber(event_num); // 0 for BOR and EPICS events
        events.push_back(*preallocated_event);
    } else {
        throw JException("No JEvent given to parse the EVIO event block into!");
    }
    ievent_idx = 0; // start filling events at the beginning

    // Parse data in buffer to create data objects
    uint32_t *istart = block.data.data();
    uint32_t *iend = &istart[block.data.size()];
    ParseBank(istart, iend);

    return events;
}

//---------------------------------
// ParseBank
//---------------------------------
void EVIOBlockedEventParser::ParseBank(uint32_t *istart, uint32_t *iend) {
    uint32_t *iptr = istart;

    while (iptr < iend) {
        uint32_t event_len = iptr[0];
        uint32_t event_head = iptr[1];
        uint32_t tag = (event_head >> 16) & 0xFFFF;

//_DBG_ << "0x" << hex << (uint64_t)iptr << dec << ": event_len=" << event_len << "tag=" << hex << tag << dec << endl;

        switch (tag) {
            case 0x0060:
                ParseEPICSbank(iptr, iend);
                break;
            case 0x0070:
                ParseBORbank(iptr, iend);
                break;

            case 0xFFD0:
            case 0xFFD1:
            case 0xFFD2:
            case 0xFFD3:
            case 0xFFD4:
                ParseControlEvent(iptr, iend);
                break;

            case 0xFF58:
            case 0xFF78: //current_parsed_events.back()->sync_flag = true;
            case 0xFF50:
            case 0xFF70:
                ParsePhysicsBank(iptr, iend);
                break;
            case 0xFF32:
            case 0xFF33:
                ParseCDAQBank(iptr, iend);
                break;

            default:
                _DBG_ << "Unknown outer EVIO bank tag: " << std::hex << tag << std::dec << std::endl;
                DumpBinary(iptr, nullptr, 20, nullptr);
                iptr = &iptr[event_len + 1];
                if (event_len < 1) iptr = iend;
        }
    }
}


//---------------------------------
// ParseEPICSbank
//---------------------------------
void EVIOBlockedEventParser::ParseEPICSbank(uint32_t *&iptr, uint32_t *iend) {
    _DBG_ << "Parsing EPICS event ..." << std::endl;
    if (!m_config.PARSE_EPICS) {
        iptr = iend;
        return;
    }

    time_t timestamp = 0;

    // Outer bank
    uint32_t *istart = iptr;
    uint32_t epics_bank_len = *iptr++;
    if (epics_bank_len < 1) {
        _DBG_ << "bank_len<1 in EPICS event!" << std::endl;
        iptr = iend;
        return;
    }

    uint32_t *iend_epics = &iptr[epics_bank_len];

    // Advance to first daughter bank
    iptr++;

    // Get pointer to the event we should place this in and
    // also advance ievent_idx for next event.
    auto event = events[ievent_idx++];
    // DParsedEvent *pe = current_parsed_events.front();
    // pe->event_status_bits |= (1<<kSTATUS_EPICS_EVENT);

    // Loop over daughter banks
    std::vector<DEPICSvalue *> epicsvalues;
    while (iptr < iend_epics) {

        uint32_t bank_len = (*iptr) & 0xFFFF;
        uint32_t tag = ((*iptr) >> 24) & 0xFF;
        iptr++;

        if (tag == 0x61) {
            // timestamp bank
            timestamp = *iptr;
        } else if (tag == 0x62) {
            // EPICS data value
            string nameval = (const char *) iptr;
            epicsvalues.push_back(new DEPICSvalue(timestamp, nameval));
            // pe->NEW_DEPICSvalue(timestamp, nameval);
        } else {
            // Unknown tag. Bail
            _DBG_ << "Unknown tag 0x" << std::hex << tag << std::dec << " in EPICS event!" << std::endl;
            // DumpBinary(istart, iend_epics, 32, &iptr[-1]);
            throw JException("Unknown tag in EPICS bank", __FILE__, __LINE__);
        }

        iptr = &iptr[bank_len];
    }

    event->Insert(epicsvalues);

    iptr = iend_epics;
}

//---------------------------------
// ParseBORbank
//---------------------------------
void EVIOBlockedEventParser::ParseBORbank(uint32_t *&iptr, uint32_t *iend) {
    // TODO: Copy from DEVIOWorkerThread if needed (I don't think it is for JANA4ML4FPGA)
    _DBG_ << "Skipping BOR event" << std::endl;
    ievent_idx++;
    iptr = iend;
}

//---------------------------------
// ParseControlEvent
//---------------------------------
void EVIOBlockedEventParser::ParseControlEvent(uint32_t *&iptr, uint32_t *iend) {
    // Get pointer to the event we should place this in and
    // also advance ievent_idx for next event.
    auto event = events[ievent_idx++];

    time_t t = (time_t) iptr[2];
    string tstr = ctime(&t);
    if (tstr.size() > 1) tstr.erase(tstr.size() - 1);

    string type = "Control";
    switch (iptr[1] >> 16) {
        case 0XFFD0:
            type = "Sync";
            break;
        case 0XFFD1:
            type = "Prestart";
            break;
        case 0XFFD2:
            type = "Go";
            break;
        case 0XFFD3:
            type = "Pause";
            break;
        case 0XFFD4:
            type = "End";
            break;
    }

    jout << "Control event: " << type << " - " << tstr << std::endl;

    // pe->event_status_bits |= (1<<kSTATUS_CONTROL_EVENT);
    auto controlevent = new DCODAControlEvent();
    controlevent->event_type = iptr[1] >> 16;
    controlevent->unix_time = t;
    for (auto p = iptr; p != iend; p++) controlevent->words.push_back(*p);

    std::vector<DCODAControlEvent *> controlevents;
    controlevents.push_back(controlevent);
    event->Insert(controlevents);

    iptr = iend;
}

//---------------------------------
// ParsePhysicsBank
//---------------------------------
void EVIOBlockedEventParser::ParsePhysicsBank(uint32_t *&iptr, uint32_t *iend) {
    uint32_t physics_event_len = *iptr++;
    uint32_t *iend_physics_event = &iptr[physics_event_len];
    iptr++;

    // Built Trigger Bank
    uint32_t built_trigger_bank_len = *iptr;
    uint32_t *iend_built_trigger_bank = &iptr[built_trigger_bank_len + 1];
    ParseBuiltTriggerBank(iptr, iend_built_trigger_bank);
    iptr = iend_built_trigger_bank;

    // Loop over Data banks
    while (iptr < iend_physics_event) {

        uint32_t data_bank_len = *iptr;
        uint32_t *iend_data_bank = &iptr[data_bank_len + 1];

        ParseDataBank(iptr, iend_data_bank);

        iptr = iend_data_bank;
    }

    iptr = iend_physics_event;
}

//---------------------------------
// ParseCDAQBank
//---------------------------------
void EVIOBlockedEventParser::ParseCDAQBank(uint32_t *&iptr, uint32_t *iend) {
    // _DBG_<<"Skipping CDAQ event" << std::endl;
    // ievent_idx++;
    // iptr = iend;

    // if(VERBOSE>1) jout << "-- Parsing CDAQ Event" << endl;

    // Check if this is a BOR event
    if ((iptr[1] & 0xFFFF0000) == 0xFF320000) {
        iptr += 2;
        try {
            ParseBORbank(iptr, iend);
        } catch (JException &e) {
            _DBG_ << e.what() << std::endl;
        }
        return;
    }

    // Must be physics event(s)
    // for(auto pe : current_parsed_events) pe->event_status_bits |= (1<<kSTATUS_PHYSICS_EVENT) + (1<<kSTATUS_CDAQ);

    // Set flag in JEventSource_EVIOpp that this is a CDAQ file
    // event_source->IS_CDAQ_FILE = true;

    uint32_t physics_event_len = *iptr++;
    uint32_t *iend_physics_event = &iptr[physics_event_len];
    iptr++;

    // Loop over Data banks
    while (iptr < iend_physics_event) {

        uint32_t data_bank_len = *iptr;
        uint32_t *iend_data_bank = &iptr[data_bank_len + 1];

        ParseDataBank(iptr, iend_data_bank);

        iptr = iend_data_bank;
    }

    iptr = iend_physics_event;
}

//---------------------------------
// ParseBuiltTriggerBank
//---------------------------------
void EVIOBlockedEventParser::ParseBuiltTriggerBank(uint32_t *&iptr, uint32_t *iend) {
    if (!m_config.PARSE_TRIGGER) return;

    iptr++; // advance past length word
    uint32_t mask = 0xFF202000;
    if (((*iptr) & mask) != mask) {
        stringstream ss;
        ss << "Bad header word in Built Trigger Bank: " << hex << *iptr;
        throw JException(ss.str(), __FILE__, __LINE__);
    }

    uint32_t tag = (*iptr) >> 16; // 0xFF2X
    uint32_t Nrocs = (*iptr++) & 0xFF;
    uint32_t Mevents = events.size();

    // sanity check:
    if (Mevents == 0) {
        stringstream ss;
        ss << "EVIOBlockedEventParser::ParseBuiltTriggerBank() called with zero events! " << endl;
        throw JException(ss.str(), __FILE__, __LINE__);
    }


    //-------- Common data (64bit)
    uint32_t common_header64 = *iptr++;
    uint32_t common_header64_len = common_header64 & 0xFFFF;
    uint64_t *iptr64 = (uint64_t *) iptr;
    iptr = &iptr[common_header64_len];

    // First event number
    uint64_t first_event_num = *iptr64++;

    // Hi and lo 32bit words in 64bit numbers seem to be
    // switched for events read from ET, but not read from
    // file. Not sure if this is in the swapping routine
    //    if(event_source->source_type==event_source->kETSource) first_event_num = (first_event_num>>32) | (first_event_num<<32);

    // Average timestamps
    uint32_t Ntimestamps = (common_header64_len / 2) - 1;
    if (tag & 0x2) Ntimestamps--; // subtract 1 for run number/type word if present
    vector<uint64_t> avg_timestamps;
    for (uint32_t i = 0; i < Ntimestamps; i++) avg_timestamps.push_back((uint64_t)(*iptr64++));

    // run number and run type
    uint32_t run_number = 0;
    uint32_t run_type = 0;
    if (tag & 0x02) {
        run_number = (*iptr64) >> 32;
        run_type = (*iptr64) & 0xFFFFFFFF;
        iptr64++;
    }

    //-------- Common data (16bit)
    uint32_t common_header16 = *iptr++;
    uint32_t common_header16_len = common_header16 & 0xFFFF;
    uint16_t *iptr16 = (uint16_t *) iptr;
    iptr = &iptr[common_header16_len];

    vector<uint16_t> event_types;
    for (uint32_t i = 0; i < Mevents; i++) event_types.push_back(*iptr16++);

    //-------- ROC data (32bit)
    for (uint32_t iroc = 0; iroc < Nrocs; iroc++) {
        uint32_t common_header32 = *iptr++;
        uint32_t common_header32_len = common_header32 & 0xFFFF;
        uint32_t rocid = common_header32 >> 24;

        uint32_t Nwords_per_event = common_header32_len / Mevents;
        for (auto event: events) {

            DCODAROCInfo *codarocinfo = new DCODAROCInfo();
            event->Insert(codarocinfo);
            codarocinfo->rocid = rocid;

            uint64_t ts_low = *iptr++;
            uint64_t ts_high = *iptr++;
            codarocinfo->timestamp = (ts_high << 32) + ts_low;
            codarocinfo->misc.clear(); // could be recycled from previous event
            for (uint32_t i = 2; i < Nwords_per_event; i++) codarocinfo->misc.push_back(*iptr++);

            if (iptr > iend) {
                throw JException("Bad data format in ParseBuiltTriggerBank!", __FILE__, __LINE__);
            }
        }
    }

    //-------- Make DCODAEventInfo objects
    uint64_t ievent = 0;
    for (auto event: events) {

        event->SetRunNumber(run_number); // may be overwritten in JEventSource_EVIOpp::GetEvent()

        DCODAEventInfo *codaeventinfo = new DCODAEventInfo();
        event->Insert(codaeventinfo);
        codaeventinfo->run_number = run_number;
        codaeventinfo->run_type = run_type;
        codaeventinfo->event_number = first_event_num + ievent;
        codaeventinfo->event_type = event_types.empty() ? 0 : event_types[ievent];
        codaeventinfo->avg_timestamp = avg_timestamps.empty() ? 0 : avg_timestamps[ievent];
        ievent++;
    }
}

//---------------------------------
// ParseDataBank
//---------------------------------
void EVIOBlockedEventParser::ParseDataBank(uint32_t *&iptr, uint32_t *iend) {
    // Physics Event's Data Bank header
    iptr++; // advance past data bank length word
    uint32_t rocid = ((*iptr) >> 16) & 0xFFF;
    iptr++;

    // if(!ROCIDS_TO_PARSE.empty()){
    // 	if(ROCIDS_TO_PARSE.find(rocid) == ROCIDS_TO_PARSE.end()) return;
    // }

    // Loop over Data Block Banks
    while (iptr < iend) {

        uint32_t data_block_bank_len = *iptr++;
        uint32_t *iend_data_block_bank = &iptr[data_block_bank_len];
        uint32_t data_block_bank_header = *iptr++;

        // Not sure where this comes from, but it needs to be skipped if present
        while ((*iptr == 0xF800FAFA) && (iptr < iend)) iptr++;

        uint32_t det_id = (data_block_bank_header >> 16) & 0xFFF;
        switch (det_id) {

            case 20:
                if (m_config.VERBOSE > 3) _DBG_ << " -- CAEN1190  rocid=" << rocid << std::endl;
                ParseCAEN1190(rocid, iptr, iend_data_block_bank);
                break;

            case 0x55:
                if (m_config.VERBOSE > 3) _DBG_ << " -- Module Configuration  rocid=" << rocid << std::endl;
                ParseModuleConfiguration(rocid, iptr, iend_data_block_bank);
                break;

            case 0x56:
                if (m_config.VERBOSE > 3) _DBG_ << " -- Event Tag  rocid=" << rocid << std::endl;
                ParseEventTagBank(iptr, iend_data_block_bank);
                break;

            case 0:
            case 1:
            case 3:
            case 6:  // flash 250 module, MMD 2014/2/4
            case 16: // flash 125 module (CDC), DL 2014/6/19
            case 26: // F1 TDC module (BCAL), MMD 2014-07-31
                if (m_config.VERBOSE > 3) _DBG_ << " -- JLab Module  rocid=" << rocid << std::endl;
                ParseJLabModuleData(rocid, iptr, iend_data_block_bank);
                break;

            case 0x123:
            case 0x28:
                if (m_config.VERBOSE > 3) _DBG_ << " -- SSP  rocid=" << rocid << std::endl;
                ParseSSPBank(rocid, iptr, iend_data_block_bank);
                break;

                // These were implemented in the ROL for sync events
                // as 0xEE02 and 0xEE05. However, that violates the
                // spec. which reserves the top 4 bits as status bits
                // (the first "E" should really be a "1". We just check
                // other 12 bits here.
            case 0xE02:
                if (m_config.VERBOSE > 3) _DBG_ << " -- TSscaler  rocid=" << rocid << std::endl;
                ParseTSscalerBank(iptr, iend);
                break;
            case 0xE05:
                //				Parsef250scalerBank(iptr, iend);
                break;
            case 0xE10:
                Parsef250scalerBank(rocid, iptr, iend_data_block_bank);
                break;

                // The CDAQ system leave the raw trigger info in the Physics event data
                // bank. Skip it for now.
            case 0xF11:
                if (m_config.VERBOSE > 3) _DBG_ << "Raw Trigger bank  rocid=" << rocid << std::endl;
                ParseRawTriggerBank(rocid, iptr, iend_data_block_bank);
                break;

                // // When we write out single events in the offline, we also can save some
                // // higher level data objects to save disk space and speed up
                // // specialized processing (e.g. pi0 calibration)
                // case 0xD01:
                // 	ParseDVertexBank(iptr, iend_data_block_bank);
                // 	break;
                // case 0xD02:
                // 	ParseDEventRFBunchBank(iptr, iend_data_block_bank);
                // 	break;

                // case 5:
                // 	// old ROL Beni used had this but I don't think its
                // 	// been used for years. Run 10390 seems to have
                // 	// this though (???)
                // 	break;

            case 0x11:
                // attempt at GEM SRS parsing
                ParseDGEMSRSBank(rocid, iptr, iend_data_block_bank);
                break;

            case 0xDEC:
                // JLab helicity decoder board (parsed in halld_recon since 2025). Not used here - skip.
                if (m_config.VERBOSE > 3) _DBG_ << " -- Helicity Decoder (skipping)  rocid=" << rocid << std::endl;
                break;

            default: {
                // Unknown banks (e.g. from a new detector or a format change) are skipped
                // instead of aborting the whole job (same behavior as halld_recon ParseUnknownBank).
                static std::atomic<int> warn_count{0};
                if (ShouldWarn(warn_count)) {
                    jerr << "Unknown module type (" << det_id << " = 0x" << std::hex << det_id << std::dec
                         << " ) encountered. Skipping bank." << std::endl;
                    if (m_config.VERBOSE > 5) {
                        std::cout << "----- First few words to help with debugging -----" << std::endl;
                        std::cout.flush();
                        std::cerr.flush();
                        DumpBinary(&iptr[-2], iend, 32, &iptr[-1]);
                    }
                }
            }

        }

        iptr = iend_data_block_bank;
    }

}

//---------------------------------
// ParseCAEN1190
//---------------------------------
void EVIOBlockedEventParser::ParseCAEN1190(uint32_t rocid, uint32_t *&iptr, uint32_t *iend) {
    _DBG_ << " Skipping ParseCAEN1190" << std::endl;
    iptr = iend;
}

//---------------------------------
// ParseModuleConfiguration
//---------------------------------
void EVIOBlockedEventParser::ParseModuleConfiguration(uint32_t rocid, uint32_t *&iptr, uint32_t *iend) {
    if (!m_config.PARSE_CONFIG) {
        iptr = &iptr[(*iptr) + 1];
        return;
    }

    /// Parse a bank of module configuration data. These are configuration values
    /// programmed into the module at the beginning of the run that may be needed
    /// in the offline. For example, the number of samples to sum in a FADC pulse
    /// integral.
    ///
    /// The bank has one or more sections, each describing parameters applicable 
    /// to a number of modules as indicated by a 24bit slot mask.
    ///
    /// This bank should appear only once per DAQ event which, if in multi-event
    /// block mode, may have multiple L1 events. The parameters here will apply
    /// to all L1 events in the block. This method will put the config objects
    /// into each event in current_parsed_events. The config objects are duplicated
    /// as needed so each event has its own, indepenent set of config objects.

    while (iptr < iend) {
        uint32_t slot_mask = (*iptr) & 0xFFFFFF;
        uint32_t Nvals = ((*iptr) >> 24) & 0xFF;
        iptr++;

        // Events will be created in the first event (i.e. using its pool)
        // but pointers are saved so we can use them to construct identical
        // objects in all other events later
        // DParsedEvent *pe = current_parsed_events.front();

        Df250Config *f250config = NULL;
        Df125Config *f125config = NULL;
        DF1TDCConfig *f1tdcconfig = NULL;
        DCAEN1290TDCConfig *caen1290tdcconfig = NULL;

        // Loop over all parameters in this section
        for (uint32_t i = 0; i < Nvals; i++) {
            if (iptr >= iend) {
                _DBG_ << "DAQ Configuration bank corrupt! slot_mask=0x" << std::hex << slot_mask << std::dec
                      << " Nvals=" << Nvals << std::endl;
                throw JException("Corrupt DAQ config. bank");
            }

            daq_param_type ptype = (daq_param_type) ((*iptr) >> 16);
            uint16_t val = (*iptr) & 0xFFFF;

            if (m_config.VERBOSE > 6)
                std::cout << "       DAQ parameter of type: 0x" << std::hex << ptype << std::dec
                          << "  found with value: " << val << std::endl;

            // Create config object of correct type if needed and copy
            // parameter value into it.
            switch (ptype >> 8) {

                // f250
                case 0x05:
                    if (!f250config) f250config = new Df250Config(rocid, slot_mask);
                    switch (ptype) {
                        case kPARAM250_NSA            :
                            f250config->NSA = val;
                            break;
                        case kPARAM250_NSB            :
                            f250config->NSB = val;
                            break;
                        case kPARAM250_NSA_NSB        :
                            f250config->NSA_NSB = val;
                            break;
                        case kPARAM250_NPED           :
                            f250config->NPED = val;
                            break;
                        default:
                            _DBG_ << "UNKNOWN DAQ Config Parameter type: 0x" << std::hex << ptype << std::dec
                                  << std::endl;
                    }
                    break;

                    // f125
                case 0x0F:
                    if (!f125config) f125config = new Df125Config(rocid, slot_mask);
                    switch (ptype) {
                        case kPARAM125_NSA            :
                            f125config->NSA = val;
                            break;
                        case kPARAM125_NSB            :
                            f125config->NSB = val;
                            break;
                        case kPARAM125_NSA_NSB        :
                            f125config->NSA_NSB = val;
                            break;
                        case kPARAM125_NPED           :
                            f125config->NPED = val;
                            break;
                        case kPARAM125_WINWIDTH       :
                            f125config->WINWIDTH = val;
                            break;
                        case kPARAM125_PL             :
                            f125config->PL = val;
                            break;
                        case kPARAM125_NW             :
                            f125config->NW = val;
                            break;
                        case kPARAM125_NPK            :
                            f125config->NPK = val;
                            break;
                        case kPARAM125_P1             :
                            f125config->P1 = val;
                            break;
                        case kPARAM125_P2             :
                            f125config->P2 = val;
                            break;
                        case kPARAM125_PG             :
                            f125config->PG = val;
                            break;
                        case kPARAM125_IE             :
                            f125config->IE = val;
                            break;
                        case kPARAM125_H              :
                            f125config->H = val;
                            break;
                        case kPARAM125_TH             :
                            f125config->TH = val;
                            break;
                        case kPARAM125_TL             :
                            f125config->TL = val;
                            break;
                        case kPARAM125_IBIT           :
                            f125config->IBIT = val;
                            break;
                        case kPARAM125_ABIT           :
                            f125config->ABIT = val;
                            break;
                        case kPARAM125_PBIT           :
                            f125config->PBIT = val;
                            break;
                        default:
                            _DBG_ << "UNKNOWN DAQ Config Parameter type: 0x" << std::hex << ptype << std::dec
                                  << std::endl;
                    }
                    break;

                    // F1TDC
                case 0x06:
                    if (!f1tdcconfig) f1tdcconfig = new DF1TDCConfig(rocid, slot_mask);
                    switch (ptype) {
                        case kPARAMF1_REFCNT          :
                            f1tdcconfig->REFCNT = val;
                            break;
                        case kPARAMF1_TRIGWIN         :
                            f1tdcconfig->TRIGWIN = val;
                            break;
                        case kPARAMF1_TRIGLAT         :
                            f1tdcconfig->TRIGLAT = val;
                            break;
                        case kPARAMF1_HSDIV           :
                            f1tdcconfig->HSDIV = val;
                            break;
                        case kPARAMF1_BINSIZE         :
                            f1tdcconfig->BINSIZE = val;
                            break;
                        case kPARAMF1_REFCLKDIV       :
                            f1tdcconfig->REFCLKDIV = val;
                            break;
                        default:
                            _DBG_ << "UNKNOWN DAQ Config Parameter type: 0x" << std::hex << ptype << std::dec
                                  << std::endl;
                    }
                    break;

                    // caen1290
                case 0x10:
                    if (!caen1290tdcconfig) caen1290tdcconfig = new DCAEN1290TDCConfig(rocid, slot_mask);
                    switch (ptype) {
                        case kPARAMCAEN1290_WINWIDTH  :
                            caen1290tdcconfig->WINWIDTH = val;
                            break;
                        case kPARAMCAEN1290_WINOFFSET :
                            caen1290tdcconfig->WINOFFSET = val;
                            break;
                        default:
                            _DBG_ << "UNKNOWN DAQ Config Parameter type: 0x" << std::hex << ptype << std::dec
                                  << std::endl;
                    }
                    break;

                default:
                    _DBG_ << "Unknown module type: 0x" << std::hex << (ptype >> 8) << std::endl;
                    throw JException("Unknown module type in configuration bank");
            }

            iptr++;
        }

        // Make copies of all config objects for all events
        for (auto event: events) {
            if (f250config) event->Insert(new Df250Config(f250config));
            if (f125config) event->Insert(new Df125Config(f125config));
            if (f1tdcconfig) event->Insert(new DF1TDCConfig(f1tdcconfig));
            if (caen1290tdcconfig) event->Insert(new DCAEN1290TDCConfig(caen1290tdcconfig));
        }

        // Slightly more inefficient than optimal
        if (f250config) delete f250config;
        if (f125config) delete f125config;
        if (f1tdcconfig) delete f1tdcconfig;
        if (caen1290tdcconfig) delete caen1290tdcconfig;
    }
}

//---------------------------------
// ParseEventTagBank
//---------------------------------
void EVIOBlockedEventParser::ParseEventTagBank(uint32_t *&iptr, uint32_t *iend) {
    _DBG_ << " Skipping ParseEventTagBank" << std::endl;
    iptr = iend;
}

//---------------------------------
// ParseJLabModuleData
//---------------------------------
void EVIOBlockedEventParser::ParseJLabModuleData(uint32_t rocid, uint32_t *&iptr, uint32_t *iend) {
    while (iptr < iend) {

        // Get module type from next word (bits 18-21)
        uint32_t mod_id = ((*iptr) >> 18) & 0x000F;
        MODULE_TYPE type = (MODULE_TYPE) mod_id;
        //cout << "      rocid=" << rocid << "  Encountered module type: " << type << " (=" << DModuleType::GetModule(type).GetName() << ")  word=" << hex << (*iptr) << dec << endl;

        switch (type) {
            case DModuleType::FADC250:
                Parsef250Bank(rocid, iptr, iend);
                break;

            case DModuleType::FADC125:
                Parsef125Bank(rocid, iptr, iend);
                break;

            case DModuleType::F1TDC32:
                ParseF1TDCBank(rocid, iptr, iend);
                break;

            case DModuleType::F1TDC48:
                ParseF1TDCBank(rocid, iptr, iend);
                break;

            case DModuleType::TID:
                ParseTIBank(rocid, iptr, iend);
                /*
                // Ignore this data and skip over it
                while(iptr<iend && ((*iptr) & 0xF8000000) != 0x88000000) iptr++; // Skip to JLab block trailer
                iptr++; // advance past JLab block trailer
                while(iptr<iend && *iptr == 0xF8000000) iptr++; // skip filler words after block trailer
                break;
                */
                break;

            case DModuleType::UNKNOWN:
            default:
                _DBG_ << "Unknown module type (" << mod_id << ") iptr=0x" << std::hex << iptr << std::dec << std::endl;

                while (iptr < iend && ((*iptr) & 0xF8000000) != 0x88000000) iptr++; // Skip to JLab block trailer
                iptr++; // advance past JLab block trailer
                while (iptr < iend && *iptr == 0xF8000000) iptr++; // skip filler words after block trailer
                throw JException("Unknown JLab module type");
                break;
        }
    }
}

//---------------------------------
// ParseSSPBank
//---------------------------------
void EVIOBlockedEventParser::ParseSSPBank(uint32_t rocid, uint32_t *&iptr, uint32_t *iend) {
    _DBG_ << " Skipping ParseSSPBank" << std::endl;
    iptr = iend;
}

//---------------------------------
// ParseTSscalerBank
//---------------------------------
void EVIOBlockedEventParser::ParseTSscalerBank(uint32_t *&iptr, uint32_t *iend) {
    _DBG_ << " Skipping ParseTSscalerBank" << std::endl;
    iptr = iend;
}

//---------------------------------
// Parsef250scalerBank
//---------------------------------
void EVIOBlockedEventParser::Parsef250scalerBank(uint32_t rocid, uint32_t *&iptr, uint32_t *iend) {
    _DBG_ << " Skipping Parsef250scalerBank" << std::endl;
    iptr = iend;
}

//---------------------------------
// ParseRawTriggerBank
//---------------------------------
void EVIOBlockedEventParser::ParseRawTriggerBank(uint32_t rocid, uint32_t *&iptr, uint32_t *iend) {
    // CDAQ records the raw trigger bank rather than the built trigger bank.
    // Create DCODAROCInfo objects for each of these.

    if (!m_config.PARSE_TRIGGER) return;

    // On entry, iptr points to word after the bank header (i.e. word after the one with 0xFF11)

    // Loop over events. Should be one segment for each event suppress warning
    // uint32_t ievent = 0;
    for (int ievent = ievent_idx; ievent < events.size(); ievent++) {

        uint32_t segment_header = *iptr++;
        uint32_t segment_len = segment_header & 0xFFFF;
        uint32_t *iend_segment = &iptr[segment_len];

        // suppress warning, remove assignment to unused variable but increment the pointer
        //		uint32_t event_number = *iptr++;
        iptr++;
        uint64_t ts_low = *iptr++;
        uint64_t ts_high = *iptr++;

        DCODAROCInfo *codarocinfo = new DCODAROCInfo();
        codarocinfo->rocid = rocid;
        codarocinfo->timestamp = (ts_high << 32) + ts_low;
        codarocinfo->misc.clear();   // could be recycled from previous event
        std::vector<DCODAROCInfo *> codarocinfos = {codarocinfo};
        events[ievent]->Insert(codarocinfos);

        // rocid=1 is TS and produces 2 extra words for the trigger bits
        for (uint32_t i = 3; i < segment_len; i++) codarocinfo->misc.push_back(*iptr++);

        if (iptr != iend_segment) {
            throw JException("Bad raw trigger bank format");
        }
    }
}

//---------------------------------
// ParseDGEMSRSBank
//---------------------------------
void EVIOBlockedEventParser::ParseDGEMSRSBank(uint32_t rocid, uint32_t *&iptr, uint32_t *iend) {
    if (!m_config.PARSE_GEMSRS) {
        iptr = &iptr[(*iptr) + 1];
        return;
    }
    if (m_config.VERBOSE > 3) _DBG_ << "GEMSRS ROC " << rocid << std::endl;

    auto ievent = ievent_idx;  // start by pointing to first JEvent we should write to
    // auto event_iter = events.begin();
    // std::shared_ptr<JEvent> event = events.front();
    // DParsedEvent *pe = NULL;

    // fictitious slot for TT, since SRS is a separate crate but read through ROC 76
    uint32_t slot = 24;
    uint32_t apv_id = 0xFFFFFFFF;
    uint32_t fec_id = 0xFFFFFFFF;
    uint32_t itrigger = 0xFFFFFFFF;
    //uint32_t ievent_cnt = 0xFFFFFFFF;
    //uint32_t last_itrigger = itrigger;

    vector<int> rawData16bits;

    if (ievent >= events.size()) {
        // No event to write to. Should not happen, but don't index past events[].
        iptr = iend;
        return;
    }

    iptr++; //skip first word? (no idata=0) used in GEMRawDecoder::Decode
    if (iptr >= iend) {
        // Empty/truncated bank
        iptr = iend;
        return;
    }

    // This is very confusing, but based on the original code, it looks like
    // The way multiple events are identified is by having 2 magic
    // header words in a row with no data words in between. Since we
    // change how it keeps track of events here, we use a flag to 
    // indicate if the last word was a magic header.
    bool last_word_was_magic_header = false;

    while (true) { // while haven't reached event trailer

        if (((*iptr >> 8) & 0xffffff) == 0x414443) { // magic key for "Data Header" in ADC format

            if (rawData16bits.size() > 0) {
                if (m_config.VERBOSE > 7)
                    std::cout << "Previous channel: apv_id = " << apv_id << " fec_id = " << fec_id << " had "
                              << rawData16bits.size() << " 16 bit words" << std::endl;

                // Seems there can be raw data words before the first header. Ignore these.
                MakeDGEMSRSWindowRawData(events[ievent].get(), rocid, slot, itrigger, apv_id, rawData16bits);
                rawData16bits.clear();
                // first_header_seen = true;
            } else { // for first APV in event initialize DParsedEvent
                // pe = *pe_iter++;
                if (last_word_was_magic_header) {
                    FlushDGEMSRSWindowRawData(events[ievent].get());
                    ievent++;
                }
                if (ievent >= events.size()) {
                    static std::atomic<int> warn_count{0};
                    if (ShouldWarn(warn_count)) {
                        jerr << "ParseDGEMSRSBank: more SRS event boundaries than events in block."
                             << " Skipping rest of bank." << std::endl;
                    }
                    iptr = iend;
                    return;  // note: accumulator content for the overflow event is dropped with the bank
                }
            }

            // initial word is "Data Header"
            last_word_was_magic_header = true;
            apv_id = (*iptr) & 0xff; // equivalent to nadcCh in GEMRawDecoder::Decode
            iptr++; // next word is "Header Info" (reserved)
            if (iptr >= iend) break; // truncated bank: data header without info word
            fec_id = (*iptr >> 16) & 0xff; // equivalent to nfecID in GEMRawDecoder::Decode

            if (m_config.VERBOSE > 7)
                std::cout << "Data Header for APV = " << apv_id << " FEC = " << fec_id << std::endl;

            // clear vector for raw data from this APV
            rawData16bits.clear();
        } else {
            last_word_was_magic_header = false;
            unsigned int word32bit = *iptr;
            unsigned int word16bit1 = 0;
            unsigned int word16bit2 = 0;

            unsigned int data1 = ((word32bit) >> 24) & 0xff;
            unsigned int data2 = ((word32bit) >> 16) & 0xff;
            unsigned int data3 = ((word32bit) >> 8) & 0xff;
            unsigned int data4 = (word32bit) & 0xff;

            (word16bit1) = (data2 << 8) | data1;
            (word16bit2) = (data4 << 8) | data3;
            rawData16bits.push_back(word16bit1);
            rawData16bits.push_back(word16bit2);
        }

        // trailer word (cleanup data from last APV?)
        if (*iptr == 0xfafafafa) {

            // write last ADC channel out from rawData16bits vector
            if (rawData16bits.size() > 0) {
                if (m_config.VERBOSE > 7)
                    std::cout << "Previous channel: apv_id = " << apv_id << " fec_id = " << fec_id << " had "
                              << rawData16bits.size() << " 16 bit words" << std::endl;

                MakeDGEMSRSWindowRawData(events[ievent].get(), rocid, slot, itrigger, apv_id, rawData16bits);
            }

            // reset DParsedEvent with event trailer?
            // pe_iter = current_parsed_events.begin();
            // pe = NULL;
            break;
        }

        iptr++;

        // if( ievent >= events.size() ) throw JException("ParseDGEMSRSBank: More events found than indicated in block header");
        if (iptr >= iend)break; // bulletproof
    }

    FlushDGEMSRSWindowRawData(events[ievent].get());
    iptr = iend;
}

//-------------------------
// MakeDGEMSRSWindowRawData
//-------------------------
// PERF (readout optimization): rawData16bits passed by const-ref (was by value:
// ~7.7 KB copied per APV, 12x per event); staging vectors and the heap-allocated
// per-call channel array removed. Sample values and ordering are bit-identical.
void EVIOBlockedEventParser::MakeDGEMSRSWindowRawData(JEvent *event, uint32_t rocid, uint32_t slot, uint32_t itrigger,
                                                      uint32_t apv_id, const vector<int> &rawData16bits) {
    int32_t idata = 0, firstdata = 0, lastdata = 0;
    int32_t size = rawData16bits.size();

    int32_t fAPVHeaderLevel = 1500;
    int32_t fNbOfTimeSamples = m_config.NSAMPLES_GEMSRS; // hard coded maximum number of time samples

    uint8_t NCH = 128;

    int32_t fStartData = 0;
    for (idata = 0; idata < size; idata++) {
        if (rawData16bits[idata] < fAPVHeaderLevel) {
            idata++;
            if (idata < size && rawData16bits[idata] < fAPVHeaderLevel) {
                idata++;
                if (idata < size && rawData16bits[idata] < fAPVHeaderLevel) {
                    idata += 10;
                    fStartData = idata;
                    idata = size;
                }
            }
        }
    }

    // set range for data
    firstdata = fStartData;
    lastdata = firstdata + NCH;

    ///////////////////////////////////////////////////////////////////////
    // loop over time bins and store samples directly in the output objects
    ///////////////////////////////////////////////////////////////////////
    std::vector<DGEMSRSWindowRawData *> wrds;
    wrds.reserve(NCH);
    for (int ichan = 0; ichan < NCH; ichan++) {
        uint32_t channel = apv_id * 128 + ichan;
        auto *windowRawData = new DGEMSRSWindowRawData(rocid, slot, channel, itrigger, apv_id, ichan);
        windowRawData->samples.reserve(fNbOfTimeSamples);
        wrds.push_back(windowRawData);
    }

    for (int32_t timebin = 0; timebin < fNbOfTimeSamples; timebin++) {
        // Frames from unexpected/changed data formats can be shorter than
        // fStartData + Nsamples*(NCH+12) words. Never read past the end of the frame.
        if (lastdata > size) {
            static std::atomic<int> warn_count{0};
            if (ShouldWarn(warn_count)) {
                jerr << "MakeDGEMSRSWindowRawData: APV frame too short (apv_id=" << apv_id
                     << ", " << size << " words, time bin " << timebin << " needs " << lastdata
                     << "). Skipping rest of frame." << std::endl;
            }
            break;
        }

        // EXTRACT APV25 DATA FOR A GIVEN TIME BIN
        // (int -> uint16_t directly; the removed float staging round-trip was exact
        // for the 16-bit ADC value range, so values are unchanged)
        for (int32_t chNo = 0; chNo < NCH; chNo++) {
            wrds[chNo]->samples.push_back(static_cast<uint16_t>(rawData16bits[firstdata + chNo]));
        }

        firstdata = lastdata + 12;
        lastdata = firstdata + NCH;
    }

    // Accumulate; inserted once per event by FlushDGEMSRSWindowRawData()
    (void)event;
    m_srs_accumulator.insert(m_srs_accumulator.end(), wrds.begin(), wrds.end());
}

void EVIOBlockedEventParser::FlushDGEMSRSWindowRawData(JEvent* event) {
    if (!m_srs_accumulator.empty()) {
        event->Insert(m_srs_accumulator);
        m_srs_accumulator.clear();
    }
}

//-------------------------
// Parsef250Bank
//-------------------------
void EVIOBlockedEventParser::Parsef250Bank(uint32_t rocid, uint32_t *&iptr, uint32_t *iend) {
    if (!m_config.PARSE_F250) {
        iptr = iend;
        return;
    }

    int continue_on_format_error = false;

    // auto pe_iter = current_parsed_events.begin();
    // DParsedEvent *pe = NULL;
    JEvent *event = nullptr;
    auto ievent = ievent_idx;

    uint32_t slot = 0;
    uint32_t itrigger = -1;

    uint32_t *istart_pulse_data = iptr;

    // Loop over data words
    for (; iptr < iend; iptr++) {

        // Skip all non-data-type-defining words at this
        // level. When we do encounter one, the appropriate
        // case block below should handle parsing all of
        // the data continuation words and advance the iptr.
        if (((*iptr >> 31) & 0x1) == 0)continue;

        uint32_t data_type = (*iptr >> 27) & 0x0F;
        switch (data_type) {
            case 0: // Block Header
                slot = (*iptr >> 22) & 0x1F;
                if (m_config.VERBOSE > 7)
                    cout << "      FADC250 Block Header: slot=" << slot << " (0x" << hex << *iptr << dec << ")" << endl;
                break;
            case 1: // Block Trailer
                event = nullptr;
                if (m_config.VERBOSE > 7)
                    cout << "      FADC250 Block Trailer" << " (0x" << hex << *iptr << dec << ")  iptr=0x" << hex
                         << iptr << dec << endl;
                break;
            case 2: // Event Header
                itrigger = (*iptr >> 0) & 0x3FFFFF;
                if (ievent >= events.size()) {
                    jerr << "FADC250: more event headers than events in block! rocid=" << rocid
                         << " slot=" << slot << ". Skipping rest of bank." << endl;
                    iptr = iend;
                    return;
                }
                event = events[ievent++].get();
                // pe = *pe_iter++;
                if (m_config.VERBOSE > 7)
                    cout << "      FADC250 Event Header: itrigger=" << itrigger << ", rocid=" << rocid << ", slot="
                         << slot << ")" << " (0x" << hex << *iptr << dec << ")" << endl;
                break;
            case 3: // Trigger Time
            {
                uint64_t t = ((*iptr) & 0xFFFFFF) << 0;
                if (m_config.VERBOSE > 7)
                    cout << "      FADC250 Trigger time low word=" << (((*iptr) & 0xFFFFFF)) << " (0x" << hex << *iptr
                         << dec << ")" << endl;
                iptr++;
                if (((*iptr >> 31) & 0x1) == 0) {
                    t += ((*iptr) & 0xFFFFFF)
                            << 24; // from word on the street: second trigger time word is optional!!??
                    if (m_config.VERBOSE > 7)
                        cout << "      FADC250 Trigger time high word=" << (((*iptr) & 0xFFFFFF)) << " (0x" << hex
                             << *iptr << dec << ")  iptr=0x" << hex << iptr << dec << endl;
                } else {
                    iptr--;
                }
                if (m_config.VERBOSE > 7) cout << "      FADC250 Trigger Time: t=" << t << endl;
                if (event) { 
		  event->Insert(new Df250TriggerTime(rocid, slot, itrigger, t));
		  cout << "call Insert() method:      FADC250 Trigger Time: t=" << t << "      rocid="<<rocid<<" slod="<<slot<<endl;
		}
            }
                break;
            case 4: // Window Raw Data
                // iptr passed by reference and so will be updated automatically
                // cout << "      FADC250 Window Raw Data"<<" (0x"<<hex<<*iptr<<dec<<")"<<endl;
                if (event) MakeDf250WindowRawData(event, rocid, slot, itrigger, iptr, iend);
                break;
            case 5: // Window Sum
            {
                uint32_t channel = (*iptr >> 23) & 0x0F;
                uint32_t sum = (*iptr >> 0) & 0x3FFFFF;
                uint32_t overflow = (*iptr >> 22) & 0x1;
                if (m_config.VERBOSE > 7)
                    cout << "      FADC250 Window Sum" << " (0x" << hex << *iptr << dec << ")" << endl;
                if (event) event->Insert(new Df250WindowSum(rocid, slot, channel, itrigger, sum, overflow));
            }
                break;
            case 6: // Pulse Raw Data
//                MakeDf250PulseRawData(objs, rocid, slot, itrigger, iptr);
                if (m_config.VERBOSE > 7)
                    cout << "      FADC250 Pulse Raw Data" << " (0x" << hex << *iptr << dec << ")" << endl;
                break;
            case 7: // Pulse Integral
            {
                uint32_t channel = (*iptr >> 23) & 0x0F;
                uint32_t pulse_number = (*iptr >> 21) & 0x03;
                uint32_t quality_factor = (*iptr >> 19) & 0x03;
                uint32_t sum = (*iptr >> 0) & 0x7FFFF;
                uint32_t nsamples_integral = 0;  // must be overwritten later in GetObjects with value from Df125Config value
                uint32_t nsamples_pedestal = 1;  // The firmware returns an already divided pedestal
                uint32_t pedestal = 0;  // This will be replaced by the one from Df250PulsePedestal in GetObjects
                if (m_config.VERBOSE > 7)
                    cout << "      FADC250 Pulse Integral: chan=" << channel << " pulse_number=" << pulse_number
                         << " sum=" << sum << " (0x" << hex << *iptr << dec << ")" << endl;
                if (event)
                    event->Insert(
                            new Df250PulseIntegral(rocid, slot, channel, itrigger, pulse_number, quality_factor, sum,
                                                   pedestal, nsamples_integral, nsamples_pedestal));
            }
                break;
            case 8: // Pulse Time
            {
                uint32_t channel = (*iptr >> 23) & 0x0F;
                uint32_t pulse_number = (*iptr >> 21) & 0x03;
                uint32_t quality_factor = (*iptr >> 19) & 0x03;
                uint32_t pulse_time = (*iptr >> 0) & 0x7FFFF;
                if (m_config.VERBOSE > 7)
                    cout << "      FADC250 Pulse Time: chan=" << channel << " pulse_number=" << pulse_number
                         << " pulse_time=" << pulse_time << " (0x" << hex << *iptr << dec << ")" << endl;
                if (event)
                    event->Insert(new Df250PulseTime(rocid, slot, channel, itrigger, pulse_number, quality_factor,
                                                     pulse_time));
            }
                break;
            case 9: // Pulse Data (firmware instroduce in Fall 2016)
            {
                // from word 1
                uint32_t event_number_within_block = (*iptr >> 19) & 0xFF;
                uint32_t channel = (*iptr >> 15) & 0x0F;
                bool QF_pedestal = (*iptr >> 14) & 0x01;
                uint32_t pedestal = (*iptr >> 0) & 0x3FFF;
                if (m_config.VERBOSE > 7)
                    cout << "      FADC250 Pulse Data (0x" << hex << *iptr << dec << ") channel=" << channel
                         << " pedestal=" << pedestal << " event within block=" << event_number_within_block << endl;

                // event_number_within_block=0 indicates error
                if (event_number_within_block == 0) {
                    _DBG_ << "event_number_within_block==0. This indicates a bug in firmware." << endl;
                    exit(-1);
                }

                // Event headers may be supressed so determine event from hit data
                // 2/26/2023 DL  -Note sure I understand this. It looks like this extracts the
                //                relative event number from this hit whereas the other hit info
                //                keeps track of the relative event by how many header words were seen.
                //                What is really confusing is that this seems to use and increment
                //                the same pe and pe_iter used by the other data types here(??!)
                if ((event_number_within_block > events.size())) {
                    jerr << "Bad f250 event number for rocid=" << rocid << " slot=" << slot << " channel=" << channel
                         << endl;
                    throw JException("Bad f250 event number", __FILE__, __LINE__);
                }
                event = events[event_number_within_block - 1].get();
                // pe_iter = current_parsed_events.begin();
                // advance( pe_iter, event_number_within_block-1 );
                // pe = *pe_iter++;

                itrigger = event_number_within_block; // is this right?
                uint32_t pulse_number = 0;

                while ((*++iptr >> 31) == 0) {

                    if ((*iptr >> 30) != 0x01) {
                        jerr << "Bad f250 Pulse Data for rocid=" << rocid << " slot=" << slot << " channel=" << channel
                             << endl;
                        DumpBinary(istart_pulse_data, iend, ((uint64_t) &iptr[3] - (uint64_t) istart_pulse_data) / 4,
                                   iptr);
                        if (continue_on_format_error) {
                            iptr = iend;
                            return;
                        } else
                            throw JException("Bad f250 Pulse Data!", __FILE__, __LINE__);
                    }

                    // from word 2
                    uint32_t integral = (*iptr >> 12) & 0x3FFFF;
                    bool QF_NSA_beyond_PTW = (*iptr >> 11) & 0x01;
                    bool QF_overflow = (*iptr >> 10) & 0x01;
                    bool QF_underflow = (*iptr >> 9) & 0x01;
                    uint32_t nsamples_over_threshold = (*iptr >> 0) & 0x1FF;
                    if (m_config.VERBOSE > 7)
                        cout << "      FADC250 Pulse Data word 2(0x" << hex << *iptr << dec << ")  integral="
                             << integral << endl;

                    iptr++;
                    if ((*iptr >> 30) != 0x00) {
                        cout << " (*iptr >> 30) != 0x00 " << endl;
                        DumpBinary(istart_pulse_data, iend, 128, iptr);
                        if (continue_on_format_error) {
                            iptr = iend;
                            return;
                        } else
                            throw JException("Bad f250 Pulse Data!", __FILE__, __LINE__);
                    }

                    // from word 3
                    uint32_t course_time = (*iptr >> 21) & 0x1FF;//< 4 ns/count
                    uint32_t fine_time = (*iptr >> 15) & 0x3F;//< 0.0625 ns/count
                    uint32_t pulse_peak = (*iptr >> 3) & 0xFFF;
                    bool QF_vpeak_beyond_NSA = (*iptr >> 2) & 0x01;
                    bool QF_vpeak_not_found = (*iptr >> 1) & 0x01;
                    bool QF_bad_pedestal = (*iptr >> 0) & 0x01;
                    if (m_config.VERBOSE > 7)
                        cout << "      FADC250 Pulse Data word 3(0x" << hex << *iptr << dec << ")  course_time="
                             << course_time << " fine_time=" << fine_time << " pulse_peak=" << pulse_peak << endl;

                    // FIRMWARE BUG: If pulse integral was zero, this is an invalid bad pulse;
                    // skip over bogus repeated pulse time repeats, and ignore it altogether.
                    // March 18, 2020 -rtj-
                    if (integral == 0 && *iptr == *(iptr + 1)) {
                        while (*(iptr + 1) == *iptr) {
                            ++iptr;
                        }
                        jerr << "Bug #1: bad f250 Pulse Data for rocid=" << rocid << " slot=" << slot << " channel="
                             << channel << endl;
                        continue_on_format_error = true;
                        break;
                    }

                    if (event) {
                        event->Insert(new Df250PulseData(rocid, slot, channel, itrigger, event_number_within_block,
                                                         QF_pedestal, pedestal, integral, QF_NSA_beyond_PTW,
                                                         QF_overflow, QF_underflow, nsamples_over_threshold,
                                                         course_time, fine_time, pulse_peak, QF_vpeak_beyond_NSA,
                                                         QF_vpeak_not_found, QF_bad_pedestal, pulse_number++));
                    }
                }
                iptr--; // backup so when outer loop advances, it points to next data defining word

            }
                break;
            case 10: // Pulse Pedestal
            {
                uint32_t channel = (*iptr >> 23) & 0x0F;
                uint32_t pulse_number = (*iptr >> 21) & 0x03;
                uint32_t pedestal = (*iptr >> 12) & 0x1FF;
                uint32_t pulse_peak = (*iptr >> 0) & 0xFFF;
                if (m_config.VERBOSE > 7)
                    cout << "      FADC250 Pulse Pedestal chan=" << channel << " pulse_number=" << pulse_number
                         << " pedestal=" << pedestal << " pulse_peak=" << pulse_peak << " (0x" << hex << *iptr << dec
                         << ")" << endl;
                if (event)
                    event->Insert(
                            new Df250PulsePedestal(rocid, slot, channel, itrigger, pulse_number, pedestal, pulse_peak));
            }
                break;
            case 13: // Event Trailer
                // This is marked "suppressed for normal readout – debug mode only" in the
                // current manual (v2). It does not contain any data so the most we could do here
                // is return early. I'm hesitant to do that though since it would mean
                // different behavior for debug mode data as regular data.
            case 14: // Data not valid (empty module)
            case 15: // Filler (non-data) word
                if (m_config.VERBOSE > 7)
                    cout << "      FADC250 Event Trailer, Data not Valid, or Filler word (" << data_type << ")"
                         << " (0x" << hex << *iptr << dec << ")" << endl;
                break;
            default:
                if (m_config.VERBOSE > 7)
                    cout << "      FADC250 unknown data type (" << data_type << ")" << " (0x" << hex << *iptr << dec
                         << ")" << endl;
                if (m_config.VERBOSE > 7)
                    cout << "      FADC250 unknown data type (" << data_type << ")" << " (0x" << hex << *iptr << dec
                         << ")" << endl;
                jerr << "FADC250 unknown data type (" << data_type << ") (0x" << hex << *iptr << dec << ")" << endl;
                if (continue_on_format_error) {
                    iptr = iend;
                    return;
                } else
                    throw JException("Unexpected word type in fADC250 block!");
        }
    }

    // Chop off filler words
    for (; iptr < iend; iptr++) {
        if (((*iptr) & 0xf8000000) != 0xf8000000) break;
    }
}

//----------------
// MakeDf250WindowRawData
//----------------
void EVIOBlockedEventParser::MakeDf250WindowRawData(JEvent *event, uint32_t rocid, uint32_t slot, uint32_t itrigger,
                                                    uint32_t *&iptr, uint32_t *iend) {
    uint32_t channel = (*iptr >> 23) & 0x0F;
    uint32_t window_width = (*iptr >> 0) & 0x0FFF;

    Df250WindowRawData *wrd = new Df250WindowRawData(rocid, slot, channel, itrigger);
    event->Insert(wrd);

    for (uint32_t isample = 0; isample < window_width; isample += 2) {

        // Advance to next word
        iptr++;

        // Truncated data: block ends before all advertised samples (halld_recon fix ff374582)
        if (iptr >= iend) {
            static std::atomic<int> warn_count{0};
            if (ShouldWarn(warn_count))
                jerr << "fa250 window raw data are incomplete - the collection of samples has been truncated!" << endl;
            iptr--; // calling method expects us to point to last word in block
            break;
        }

        // Make sure this is a data continuation word, if not, stop here
        if (((*iptr >> 31) & 0x1) != 0x0) {
            iptr--; // calling method expects us to point to last word in block
            break;
        }

        bool invalid_1 = (*iptr >> 29) & 0x1;
        bool invalid_2 = (*iptr >> 13) & 0x1;
        uint16_t sample_1 = 0;
        uint16_t sample_2 = 0;
        if (!invalid_1)sample_1 = (*iptr >> 16) & 0x1FFF;
        if (!invalid_2)sample_2 = (*iptr >> 0) & 0x1FFF;

        // Sample 1
        wrd->samples.push_back(sample_1);
        wrd->invalid_samples |= invalid_1;
        wrd->overflow |= (sample_1 >> 12) & 0x1;

        if (((isample + 2) == window_width) && invalid_2)break; // skip last sample if flagged as invalid

        // Sample 2
        wrd->samples.push_back(sample_2);
        wrd->invalid_samples |= invalid_2;
        wrd->overflow |= (sample_2 >> 12) & 0x1;
    }

    if (m_config.VERBOSE > 7)
        cout << "      FADC250 Window Raw Data: size from header=" << window_width << " Nsamples found="
             << wrd->samples.size() << endl;
    if (window_width != wrd->samples.size()) {
        jerr << " FADC250 Window Raw Data number of samples does not match header! (" << wrd->samples.size() << " != "
             << window_width << ") for rocid=" << rocid << " slot=" << slot << " channel=" << channel << endl;
    }
}

//-------------------------
// Parsef125Bank
//-------------------------
void EVIOBlockedEventParser::Parsef125Bank(uint32_t rocid, uint32_t *&iptr, uint32_t *iend) {
    if (!m_config.PARSE_F125) {
        iptr = &iptr[(*iptr) + 1];
        return;
    }

    auto ievent = ievent_idx;
    JEvent *event = nullptr;
    // auto pe_iter = current_parsed_events.begin();
    // DParsedEvent *pe = NULL;

    uint32_t slot = 0;
    uint32_t itrigger = -1;
    uint32_t last_itrigger = -2;
    uint32_t last_pulse_time_channel = 0;
    uint32_t last_slot = -1;
    uint32_t last_channel = -1;

    // Loop over data words
    for (; iptr < iend; iptr++) {

        // Skip all non-data-type-defining words at this
        // level. When we do encounter one, the appropriate
        // case block below should handle parsing all of
        // the data continuation words and advance the iptr.
        if (((*iptr >> 31) & 0x1) == 0)continue;

        uint32_t data_type = (*iptr >> 27) & 0x0F;
        switch (data_type) {
            case 0: // Block Header
                slot = (*iptr >> 22) & 0x1F;
                if (m_config.VERBOSE > 7) cout << "      FADC125 Block Header: slot=" << slot << endl;
                break;
            case 1: // Block Trailer
                ievent = ievent_idx;
                event = nullptr;
                // pe_iter = current_parsed_events.begin();
                // pe = NULL;
                break;
            case 2: // Event Header
                //slot_event_header = (*iptr>>22) & 0x1F;
                itrigger = (*iptr >> 0) & 0x3FFFFFF;
                if (ievent >= events.size()) {
                    jerr << "FADC125: more event headers than events in block! rocid=" << rocid
                         << " slot=" << slot << ". Skipping rest of bank." << endl;
                    iptr = iend;
                    return;
                }
                event = events[ievent++].get();
                // pe = *pe_iter++;
                if (m_config.VERBOSE > 7)
                    cout << "      FADC125 Event Header: itrigger=" << itrigger << " last_itrigger=" << last_itrigger
                         << ", rocid=" << rocid << ", slot=" << slot << endl;
                break;
            case 3: // Trigger Time
            {
                uint64_t t = ((*iptr) & 0xFFFFFF) << 0;
                iptr++;
                if (((*iptr >> 31) & 0x1) == 0) {
                    t += ((*iptr) & 0xFFFFFF)
                            << 24; // from word on the street: second trigger time word is optional!!??
                } else {
                    iptr--;
                }
                if (m_config.VERBOSE > 7) cout << "      FADC125 Trigger Time (t=" << t << ")" << endl;
                if (event) event->Insert(new Df125TriggerTime(rocid, slot, itrigger, t));
            }
                break;
            case 4: // Window Raw Data
                // iptr passed by reference and so will be updated automatically
                // cout << "      FADC125 Window Raw Data"<<endl;
                if (event) MakeDf125WindowRawData(event, rocid, slot, itrigger, iptr, iend);
                break;

            case 5: // CDC pulse data (new)  (GlueX-doc-2274-v8)
            {
                // Word 1:
                uint32_t word1 = *iptr;
                uint32_t channel = (*iptr >> 20) & 0x7F;
                uint32_t pulse_number = (*iptr >> 15) & 0x1F;
                uint32_t pulse_time = (*iptr >> 4) & 0x7FF;
                uint32_t quality_factor = (*iptr >> 3) & 0x1; //time QF bit
                uint32_t overflow_count = (*iptr >> 0) & 0x7;
                if (m_config.VERBOSE > 7) {
                    cout << "      FADC125 CDC Pulse Data word1: " << hex << (*iptr) << dec << endl;
                    cout << "      FADC125 CDC Pulse Data (chan=" << channel << " pulse=" << pulse_number << " time="
                         << pulse_time << " QF=" << quality_factor << " OC=" << overflow_count << ")" << endl;
                }

                // Word 2:
                ++iptr;
                if (iptr >= iend) {
                    jerr << " Truncated f125 CDC hit (block ends before continuation word!)" << endl;
                    continue;
                }
                if (((*iptr >> 31) & 0x1) != 0) {
                    jerr << " Truncated f125 CDC hit (missing continuation word!)" << endl;
                    continue;
                }
                uint32_t word2 = *iptr;
                uint32_t pedestal = (*iptr >> 23) & 0xFF;
                uint32_t sum = (*iptr >> 9) & 0x3FFF;
                uint32_t pulse_peak = (*iptr >> 0) & 0x1FF;
                if (m_config.VERBOSE > 7) {
                    cout << "      FADC125 CDC Pulse Data word2: " << hex << (*iptr) << dec << endl;
                    cout << "      FADC125 CDC Pulse Data (pedestal=" << pedestal << " sum=" << sum << " peak="
                         << pulse_peak << ")" << endl;
                }

                // Create hit objects
                uint32_t nsamples_integral = 0;  // must be overwritten later in GetObjects with value from Df125Config value
                uint32_t nsamples_pedestal = 1;  // The firmware pedestal divided by 2^PBIT where PBIT is a config. parameter

                if (event) {
                    event->Insert(new Df125CDCPulse(rocid, slot, channel, itrigger, pulse_number        // NPK
                            , pulse_time          // le_time
                            , quality_factor      // time_quality_bit
                            , overflow_count      // overflow_count
                            , pedestal            // pedestal
                            , sum                 // integral
                            , pulse_peak          // first_max_amp
                            , word1               // word1
                            , word2               // word2
                            , nsamples_pedestal   // nsamples_pedestal
                            , nsamples_integral   // nsamples_integral
                            , false));             // emulated
                }
            }
                break;

            case 6: // FDC pulse data-integral (new)  (GlueX-doc-2274-v8)
            {
                // Word 1:
                uint32_t word1 = *iptr;
                uint32_t channel = (*iptr >> 20) & 0x7F;
                uint32_t pulse_number = (*iptr >> 15) & 0x1F;
                uint32_t pulse_time = (*iptr >> 4) & 0x7FF;
                uint32_t quality_factor = (*iptr >> 3) & 0x1; //time QF bit
                uint32_t overflow_count = (*iptr >> 0) & 0x7;
                if (m_config.VERBOSE > 7) {
                    cout << "      FADC125 FDC Pulse Data(integral) word1: " << hex << (*iptr) << dec << endl;
                    cout << "      FADC125 FDC Pulse Data (chan=" << channel << " pulse=" << pulse_number << " time="
                         << pulse_time << " QF=" << quality_factor << " OC=" << overflow_count << ")" << endl;
                }

                // Word 2:
                ++iptr;
                if (iptr >= iend) {
                    jerr << " Truncated f125 FDC hit (block ends before continuation word!)" << endl;
                    continue;
                }
                if (((*iptr >> 31) & 0x1) != 0) {
                    jerr << " Truncated f125 FDC hit (missing continuation word!)" << endl;
                    continue;
                }
                uint32_t word2 = *iptr;
                uint32_t pulse_peak = 0;
                uint32_t sum = (*iptr >> 19) & 0xFFF;
                uint32_t peak_time = (*iptr >> 11) & 0xFF;
                uint32_t pedestal = (*iptr >> 0) & 0x7FF;
                if (m_config.VERBOSE > 7) {
                    cout << "      FADC125 FDC Pulse Data(integral) word2: " << hex << (*iptr) << dec << endl;
                    cout << "      FADC125 FDC Pulse Data (integral=" << sum << " time=" << peak_time << " pedestal="
                         << pedestal << ")" << endl;
                }

                // Create hit objects
                uint32_t nsamples_integral = 0;  // must be overwritten later in GetObjects with value from Df125Config value
                uint32_t nsamples_pedestal = 1;  // The firmware pedestal divided by 2^PBIT where PBIT is a config. parameter

                if (event) {
                    event->Insert(new Df125FDCPulse(rocid, slot, channel, itrigger, pulse_number        // NPK
                            , pulse_time          // le_time
                            , quality_factor      // time_quality_bit
                            , overflow_count      // overflow_count
                            , pedestal            // pedestal
                            , sum                 // integral
                            , pulse_peak          // peak_amp
                            , peak_time           // peak_time
                            , word1               // word1
                            , word2               // word2
                            , nsamples_pedestal   // nsamples_pedestal
                            , nsamples_integral   // nsamples_integral
                            , false));             // emulated
                }
            }
                break;

            case 7: // Pulse Integral
            {
                if (m_config.VERBOSE > 7) cout << "      FADC125 Pulse Integral" << endl;
                uint32_t channel = (*iptr >> 20) & 0x7F;
                uint32_t sum = (*iptr >> 0) & 0xFFFFF;
                uint32_t quality_factor = 0;
                uint32_t nsamples_integral = 0;  // must be overwritten later in GetObjects with value from Df125Config value
                uint32_t nsamples_pedestal = 1;  // The firmware returns an already divided pedestal
                uint32_t pedestal = 0;  // This will be replaced by the one from Df250PulsePedestal in GetObjects
                uint32_t pulse_number = 0;
                if (last_slot == slot && last_channel == channel) pulse_number = 1;
                last_slot = slot;
                last_channel = channel;
                if (event)
                    event->Insert(
                            new Df125PulseIntegral(rocid, slot, channel, itrigger, pulse_number, quality_factor, sum,
                                                   pedestal, nsamples_integral, nsamples_pedestal));
            }
                break;
            case 8: // Pulse Time
            {
                if (m_config.VERBOSE > 7) cout << "      FADC125 Pulse Time" << endl;
                uint32_t channel = (*iptr >> 20) & 0x7F;
                uint32_t pulse_number = (*iptr >> 18) & 0x03;
                uint32_t pulse_time = (*iptr >> 0) & 0xFFFF;
                uint32_t quality_factor = 0;
                if (event)
                    event->Insert(new Df125PulseTime(rocid, slot, channel, itrigger, pulse_number, quality_factor,
                                                     pulse_time));
                last_pulse_time_channel = channel;
            }
                break;

            case 9: // FDC pulse data-peak (new)  (GlueX-doc-2274-v8)
            {
                // Word 1 (info of all peaks):
                uint32_t word1 = *iptr;
                uint32_t channel = (*iptr >> 20) & 0x7F;
                uint32_t pulse_number = (*iptr >> 15) & 0x1F;
                uint32_t pulse_time = (*iptr >> 4) & 0x7FF;
                uint32_t quality_factor = (*iptr >> 3) & 0x1; //time QF bit
                uint32_t overflow_count = (*iptr >> 0) & 0x7;
                if (m_config.VERBOSE > 7) {
                    cout << "      FADC125 FDC Pulse Data(peak) word1: " << hex << (*iptr) << dec << endl;
                    cout << "      FADC125 FDC Pulse Data (chan=" << channel << " pulse=" << pulse_number << " time="
                         << pulse_time << " QF=" << quality_factor << " OC=" << overflow_count << ")" << endl;
                }

                // Loop over peaks:
                for (size_t i_peak = 0; i_peak < pulse_number; i_peak++) {
                    ++iptr;
                    if (iptr >= iend) {
                        jerr << " Truncated f125 FDC hit (block ends before continuation word!)" << endl;
                        continue;
                    }
                    if (((*iptr >> 31) & 0x1) != 0) {
                        jerr << " Truncated f125 FDC hit (missing continuation word!)" << endl;
                        continue;
                    }

                    // Word2 - each peak information
                    uint32_t word2 = *iptr;
                    uint32_t pulse_peak = (*iptr >> 19) & 0xFFF;
                    uint32_t sum = 0;
                    uint32_t peak_time = (*iptr >> 11) & 0xFF;
                    uint32_t pedestal = (*iptr >> 0) & 0x7FF;
                    if (m_config.VERBOSE > 7) {
                        cout << "      FADC125 FDC Pulse Data(peak) word2: " << hex << (*iptr) << dec << endl;
                        cout << "      FADC125 FDC Pulse Data (integral=" << sum << " time=" << peak_time
                             << " pedestal=" << pedestal << ")" << endl;
                    }

                    // Create hit objects
                    uint32_t nsamples_integral = 0;  // must be overwritten later in GetObjects with value from Df125Config value
                    uint32_t nsamples_pedestal = 1;  // The firmware pedestal divided by 2^PBIT where PBIT is a config. parameter

                    if (event) {

                        // The following is a temporary fix. In late 2017 the CDC group started
                        // using data type 9 (i.e. FDC pulse peak). This caused many conflicts
                        // with plugins downstream that were built around there being a Df125CDCPulse
                        // object associated with the DCDCDigiHit. In order to quickly solve
                        // the issue as the run was starting, this fix was made to produce Df125CDCPulse
                        // object from this data iff rocid<30 indicating the data came from the
                        // CDC.
                        if (rocid < 30) {

                            event->Insert(
                                    new Df125CDCPulse(rocid, slot, channel, itrigger, pulse_number        // NPK
                                            , pulse_time          // le_time
                                            , quality_factor      // time_quality_bit
                                            , overflow_count      // overflow_count
                                            , pedestal            // pedestal
                                            , sum                 // integral
                                            , pulse_peak          // peak_amp
                                            , word1               // word1
                                            , word2               // word2
                                            , nsamples_pedestal   // nsamples_pedestal
                                            , nsamples_integral   // nsamples_integral
                                            , false));             // emulated

                        } else {

                            event->Insert(
                                    new Df125FDCPulse(rocid, slot, channel, itrigger, pulse_number        // NPK
                                            , pulse_time          // le_time
                                            , quality_factor      // time_quality_bit
                                            , overflow_count      // overflow_count
                                            , pedestal            // pedestal
                                            , sum                 // integral
                                            , pulse_peak          // peak_amp
                                            , peak_time           // peak_time
                                            , word1               // word1
                                            , word2               // word2
                                            , nsamples_pedestal   // nsamples_pedestal
                                            , nsamples_integral   // nsamples_integral
                                            , false));             // emulated
                        }
                    }
                }
            }
                break;

            case 10: // Pulse Pedestal (consistent with Beni's hand-edited version of Cody's document)
            {
                if (m_config.VERBOSE > 7) cout << "      FADC125 Pulse Pedestal" << endl;
                //channel = (*iptr>>20) & 0x7F;
                uint32_t channel = last_pulse_time_channel; // not enough bits to hold channel number so rely on proximity to Pulse Time in data stream (see "FADC125 dataformat 250 modes.docx")
                uint32_t pulse_number = (*iptr >> 21) & 0x03;
                uint32_t pedestal = (*iptr >> 12) & 0x1FF;
                uint32_t pulse_peak = (*iptr >> 0) & 0xFFF;
                uint32_t nsamples_pedestal = 1;  // The firmware returns an already divided pedestal
                if (event)
                    event->Insert(
                            new Df125PulsePedestal(rocid, slot, channel, itrigger, pulse_number, pedestal, pulse_peak,
                                                   nsamples_pedestal));
            }
                break;

            case 13: // Event Trailer
            case 14: // Data not valid (empty module)
            case 15: // Filler (non-data) word
                if (m_config.VERBOSE > 7) cout << "      FADC125 ignored data type: " << data_type << endl;
                break;
            default:
                if (m_config.VERBOSE > 7)
                    cout << "      FADC125 unknown data type (" << data_type << ")" << " (0x" << hex << *iptr << dec
                         << ")" << endl;
                throw JException("Unexpected word type in fADC125 block!");

        }
    }

    // Chop off filler words
    for (; iptr < iend; iptr++) {
        if (((*iptr) & 0xf8000000) != 0xf8000000) break;
    }
}

//----------------
// MakeDf125WindowRawData
//----------------
void EVIOBlockedEventParser::MakeDf125WindowRawData(JEvent *event, uint32_t rocid, uint32_t slot, uint32_t itrigger,
                                                    uint32_t *&iptr, uint32_t *iend) {
    uint32_t channel = (*iptr >> 20) & 0x7F;
    uint32_t window_width = (*iptr >> 0) & 0x0FFF;

    auto wrd = new Df125WindowRawData(rocid, slot, channel, itrigger);
    event->Insert(wrd);

    for (uint32_t isample = 0; isample < window_width; isample += 2) {

        // Advance to next word
        iptr++;

        // Truncated data: block ends before all advertised samples (halld_recon fix ff374582)
        if (iptr >= iend) {
            static std::atomic<int> warn_count{0};
            if (ShouldWarn(warn_count))
                jerr << "fa125 window raw data are incomplete - the collection of samples has been truncated!" << endl;
            iptr--;
            break;
        }

        // Make sure this is a data continuation word, if not, stop here
        if (((*iptr >> 31) & 0x1) != 0x0)break;

        bool invalid_1 = (*iptr >> 29) & 0x1;
        bool invalid_2 = (*iptr >> 13) & 0x1;
        uint16_t sample_1 = 0;
        uint16_t sample_2 = 0;
        if (!invalid_1)sample_1 = (*iptr >> 16) & 0x1FFF;
        if (!invalid_2)sample_2 = (*iptr >> 0) & 0x1FFF;

        // Sample 1
        wrd->samples.push_back(sample_1);
        wrd->invalid_samples |= invalid_1;
        wrd->overflow |= (sample_1 >> 12) & 0x1;

        if ((isample + 2) == window_width && invalid_2)break; // skip last sample if flagged as invalid

        // Sample 2
        wrd->samples.push_back(sample_2);
        wrd->invalid_samples |= invalid_2;
        wrd->overflow |= (sample_2 >> 12) & 0x1;
    }
}

//-------------------------
// ParseF1TDCBank
//-------------------------
void EVIOBlockedEventParser::ParseF1TDCBank(uint32_t rocid, uint32_t *&iptr, uint32_t *iend) {
    _DBG_ << " Skipping ParseF1TDCBank" << std::endl;
    iptr = iend;
}

//-------------------------
// ParseTIBank
//-------------------------
void EVIOBlockedEventParser::ParseTIBank(uint32_t rocid, uint32_t *&iptr, uint32_t *iend) {
    _DBG_ << " Skipping ParseTIBank" << std::endl;
    iptr = iend;
}


//----------------
// DumpBinary
//----------------
void EVIOBlockedEventParser::DumpBinary(const uint32_t *iptr, const uint32_t *iend, uint32_t MaxWords,
                                        const uint32_t *imark) {
    /// This is used for debugging. It will print to the screen the words
    /// starting at the address given by iptr and ending just before iend
    /// or for MaxWords words, whichever comes first. If iend is NULL,
    /// then MaxWords will be printed. If MaxWords is zero then it is ignored
    /// and only iend is checked. If both iend==NULL and MaxWords==0, then
    /// only the word at iptr is printed.

    std::cout << "Dumping binary: istart=" << std::hex << iptr << " iend=" << iend << " MaxWords=" << std::dec
              << MaxWords << std::endl;

    if (iend == NULL && MaxWords == 0) MaxWords = 1;
    if (MaxWords == 0) MaxWords = (uint32_t) 0xffffffff;

    uint32_t Nwords = 0;
    while (iptr != iend && Nwords < MaxWords) {

        // line1 is hex and line2 is decimal
        std::stringstream line1, line2;

        // print words in columns 8 words wide. First part is
        // reserved for word number
        uint32_t Ncols = 8;
        line1 << std::setw(5) << Nwords;
        line2 << std::string(5, ' ');

        // Loop over columns
        for (uint32_t i = 0; i < Ncols; i++, iptr++, Nwords++) {

            if (iptr == iend) break;
            if (Nwords >= MaxWords) break;

            std::stringstream iptr_hex;
            iptr_hex << std::hex << "0x" << *iptr;

            string mark = (iptr == imark ? "*" : " ");

            line1 << std::setw(12) << iptr_hex.str() << mark;
            line2 << std::setw(12) << *iptr << mark;
        }

        std::cout << line1.str() << std::endl;
        std::cout << line2.str() << std::endl;
        std::cout << std::endl;
    }
}

