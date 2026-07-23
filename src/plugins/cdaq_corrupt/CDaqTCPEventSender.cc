
#include "CDaqTCPEventSender.h"
#include <JANA/JLogger.h>

#include <rawdataparser/EVIOBlockedEvent.h>
#include <rawdataparser/CDaqTCPevent.h>

#include <tcp_daq/tcp_event.h>

CDaqTCPEventSender::CDaqTCPEventSender() {
    SetTypeName(NAME_OF_THIS); // Provide JANA with this class's name
}

void CDaqTCPEventSender::Init() {

    m_port = 0;
    m_remote_host = "localhost";
    GetApplication()->SetDefaultParameter("cdaq:remote_host", m_remote_host, "TCP remote host to connect to");
    GetApplication()->SetDefaultParameter("cdaq:remote_port", m_port, "TCP port on remote host to connect to");


    m_log = GetApplication()->GetService<Log_service>()->logger(GetPluginName());
    auto param_prefix = GetPluginName();
    std::string default_level = "info";
    JApplication *app = GetApplication();

    // Logger. Get plugin level sub-Log
    m_log = app->GetService<Log_service>()->logger(param_prefix);

    // Get Log level from user parameter or default (WARNING: This is duplicated in CDaqEventSource.cc !!)
    std::string log_level_str = default_level.empty() ?         // did user provide default level?
                                spdlog::extensions::LogLevelToString(m_log->level()) :   //
                                default_level;
    app->SetDefaultParameter(param_prefix + ":LogLevel", log_level_str,
                             "LogLevel: trace, debug, info, warn, err, critical, off");
    // printf("CDAQ LOG LEVEL %s\n", log_level_str.c_str());
    m_log->set_level(spdlog::extensions::ParseLogLevel(log_level_str));


    //================================================================

    if( m_port != 0){
        // Tell the tcp_event library which host/port to connect to when 
        // we (eventually) try and send the first event.
        tcp_event_host((char*)m_remote_host.c_str(), m_port);
    }else{
        m_log->info("Sending of events over TCP disabled. To enable, set the cdaq:remote_port");
        m_log->info("to the port number to send to. cdaq:remote_host can also be used to specify");
        m_log->info("a host other than the localhost. E.g.:");
        m_log->info("   jana4ml4fpga -Pcdaq:remote_host=gluon200.jlab.org -Pcdaq:remote_port=20249 ...");
    }
    

}

void CDaqTCPEventSender::Process(const std::shared_ptr<const JEvent> &event) {

    // If the port number was not changed from 0 then user has not enabled
    // us to send TCP events.
    if( m_port ==0 ) return;

    // Get the full event buffer. We support if the event was read from
    // a file or if it was read from a TCP socket. 
    auto evioblockedevents = event->Get<EVIOBlockedEvent>();
    auto cdaqtcpevents = event->Get<CDaqTCPevent>();

    // std::cout << "CDaqTCPevent: " << cdaqtcpevents.size() << "  EVIOBlockedEvent:" << evioblockedevents.size() << std::endl;

    // Get pointer to data and copy up all metadata
    unsigned int *DATA = nullptr;
    int lenDATA;
    int Nr_Modules;
    int ModuleID;
    unsigned int evtHDR = 123; // what is this set to?
    unsigned int TriggerID;
    if( ! cdaqtcpevents.empty() ){
        DATA = const_cast<unsigned int*>(cdaqtcpevents[0]->DATA.data());
        lenDATA = cdaqtcpevents[0]->DATA.size();
        Nr_Modules = cdaqtcpevents[0]->Nr_Modules;
        ModuleID   = cdaqtcpevents[0]->ModuleID;
        TriggerID  = cdaqtcpevents[0]->TriggerID;
    }else if( ! evioblockedevents.empty() ){
        DATA = const_cast<unsigned int*>(evioblockedevents[0]->data.data());
        lenDATA = evioblockedevents[0]->data.size();
        Nr_Modules = 0;
        ModuleID   = 0;
        TriggerID  = 0;
    }

    if( !DATA ) return; // no event to send

    /// Lock mutex
    std::lock_guard<std::mutex>lock(m_mutex);

    // Send event, establishing the connection if not already established.
    tcp_event_host((char*)m_remote_host.c_str(), m_port, false);
    auto res = tcp_event_snd(DATA, lenDATA ,Nr_Modules ,ModuleID ,evtHDR ,TriggerID);
    if( res != 0 ) m_log->info("Error sending CDAQ TCP event!");

}

void CDaqTCPEventSender::Finish() {
    // Close any resources
    LOG << "CDaqTCPEventSender::Finish" << LOG_END;
}

