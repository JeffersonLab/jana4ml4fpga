

#pragma once


#include <vector>
#include <memory>

#include <JANA/JEvent.h>
// MIGRATION note (JANA2 2026.x): the JEventPool-based overloads were removed -
// the old pool API no longer exists and events are now provided by the caller.
#include <rawdataparser/EVIOBlockedEvent.h>

// #include <rawdataparser/DStatusBits.h>
#include <rawdataparser/daq_param_type.h>
#include <rawdataparser/DModuleType.h>


#include <rawdataparser/Df250Config.h>
#include <rawdataparser/Df250PulseIntegral.h>
// #include <rawdataparser/Df250StreamingRawData.h>
#include <rawdataparser/Df250WindowSum.h>
// #include <rawdataparser/Df250PulseRawData.h>
#include <rawdataparser/Df250TriggerTime.h>
#include <rawdataparser/Df250PulseTime.h>
#include <rawdataparser/Df250PulsePedestal.h>
#include <rawdataparser/Df250PulseData.h>
#include <rawdataparser/Df250WindowRawData.h>
#include <rawdataparser/Df125Config.h>
#include <rawdataparser/Df125TriggerTime.h>
#include <rawdataparser/Df125PulseIntegral.h>
#include <rawdataparser/Df125PulseTime.h>
#include <rawdataparser/Df125PulsePedestal.h>
// #include <rawdataparser/Df125PulseRawData.h>
#include <rawdataparser/Df125WindowRawData.h>
#include <rawdataparser/Df125CDCPulse.h>
#include <rawdataparser/Df125FDCPulse.h>
#include <rawdataparser/DF1TDCConfig.h>
// #include <rawdataparser/DF1TDCHit.h>
// #include <rawdataparser/DF1TDCTriggerTime.h>
#include <rawdataparser/DCAEN1290TDCConfig.h>
// #include <rawdataparser/DCAEN1290TDCHit.h>
#include <rawdataparser/DCODAEventInfo.h>
#include <rawdataparser/DCODAControlEvent.h>
#include <rawdataparser/DCODAROCInfo.h>
// #include <rawdataparser/Df250Scaler.h>
// #include <rawdataparser/DL1Info.h>
#include <rawdataparser/DEPICSvalue.h>
// #include <rawdataparser/DEventTag.h>
// #include <rawdataparser/Df250BORConfig.h>
// #include <rawdataparser/Df125BORConfig.h>
// #include <rawdataparser/DF1TDCBORConfig.h>
// #include <rawdataparser/DCAEN1290TDCBORConfig.h>
// #include <rawdataparser/DTSGBORConfig.h>
// #include <rawdataparser/DDIRCTriggerTime.h>
// #include <rawdataparser/DDIRCTDCHit.h>
// #include <rawdataparser/DDIRCADCHit.h>
#include <rawdataparser/DGEMSRSWindowRawData.h>
// #include <rawdataparser/DBORptrs.h>
#include "EVIOBlockedEventParserConfig.h"

class EVIOBlockedEventParser{

    std::vector <std::shared_ptr<JEvent>> events;
    size_t ievent_idx; // keeps track of the next event in the "events" vector to use while parsing

    EVIOBlockedEventParserConfig m_config;

public:

    EVIOBlockedEventParser();
    ~EVIOBlockedEventParser();

    EVIOBlockedEventParserConfig GetConfigCopy() { return m_config; }
    void Configure(EVIOBlockedEventParserConfig config) { m_config = config; }

    std::vector <std::shared_ptr<JEvent>> ParseEVIOBlockedEvent(EVIOBlockedEvent &block, std::shared_ptr<JEvent> &preallocated_event);
    std::vector <std::shared_ptr<JEvent>> ParseEVIOBlockedEvent(EVIOBlockedEvent &block, std::shared_ptr<JEvent> *preallocated_event);

    void ParseBank(uint32_t *istart, uint32_t *iend);
    void ParseEPICSbank(uint32_t* &iptr, uint32_t *iend);
    void ParseBORbank(uint32_t* &iptr, uint32_t *iend);
    void ParseControlEvent(uint32_t* &iptr, uint32_t *iend);
    void ParsePhysicsBank(uint32_t* &iptr, uint32_t *iend);
    void ParseCDAQBank(uint32_t* &iptr, uint32_t *iend);
    void ParseBuiltTriggerBank(uint32_t* &iptr, uint32_t *iend);
    void ParseDataBank(uint32_t* &iptr, uint32_t *iend);

    void ParseCAEN1190(uint32_t rocid, uint32_t *&iptr, uint32_t *iend);
    void ParseModuleConfiguration(uint32_t rocid, uint32_t *&iptr, uint32_t *iend);
    void ParseEventTagBank(uint32_t *&iptr, uint32_t *iend);
    void ParseJLabModuleData(uint32_t rocid, uint32_t *&iptr, uint32_t *iend);
    void ParseSSPBank(uint32_t rocid, uint32_t *&iptr, uint32_t *iend);
    void ParseTSscalerBank(uint32_t *&iptr, uint32_t *iend);
    void Parsef250scalerBank(uint32_t rocid, uint32_t *&iptr, uint32_t *iend);
    void ParseRawTriggerBank(uint32_t rocid, uint32_t *&iptr, uint32_t *iend);
    void ParseDGEMSRSBank(uint32_t rocid, uint32_t *&iptr, uint32_t *iend);

    void MakeDGEMSRSWindowRawData(JEvent *event, uint32_t rocid, uint32_t slot, uint32_t itrigger, uint32_t apv_id, const std::vector<int> &rawData16bits);
    // MIGRATION (JANA2 2026.x): JEvent::Insert(vector) now REPLACES the databundle
    // instead of appending, so per-APV inserts would drop all but the last APV.
    // SRS objects are accumulated here and inserted once per event.
    void FlushDGEMSRSWindowRawData(JEvent *event);
    std::vector<DGEMSRSWindowRawData*> m_srs_accumulator;
    void Parsef250Bank(uint32_t rocid, uint32_t *&iptr, uint32_t *iend);
    void MakeDf250WindowRawData(JEvent *event, uint32_t rocid, uint32_t slot, uint32_t itrigger, uint32_t* &iptr, uint32_t *iend);
    void Parsef125Bank(uint32_t rocid, uint32_t *&iptr, uint32_t *iend);
    void MakeDf125WindowRawData(JEvent *event, uint32_t rocid, uint32_t slot, uint32_t itrigger, uint32_t* &iptr, uint32_t *iend);
    void ParseF1TDCBank(uint32_t rocid, uint32_t *&iptr, uint32_t *iend);
    void ParseTIBank(uint32_t rocid, uint32_t *&iptr, uint32_t *iend);

    void DumpBinary(const uint32_t *iptr, const uint32_t *iend, uint32_t MaxWords, const uint32_t *imark);
};

