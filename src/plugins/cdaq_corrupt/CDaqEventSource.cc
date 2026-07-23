#include "CDaqEventSource.h"

#include <future>
#include <thread>
#include <chrono>
#include <JANA/JEvent.h>
#include <rawdataparser/EVIOBlockedEvent.h>
#include <rawdataparser/CDaqTCPevent.h>
#include <tcp_daq/tcp_event.h>
#include <tcp_daq/tcp_thread.h>
#include <services/log/Log_service.h>
#include <rawdataparser/swap_bank.h>


#define MAXDATA 65536L   //-- in words;  400kB max (RAW CDC) => 100000L

//------------------------
// HexStr
//------------------------
inline std::string HexStr(uint32_t v) {
    char str[256];
    sprintf(str, "0x%08x", v);

    return std::string(str);
}


CDaqEventSource::CDaqEventSource(std::string resource_name, JApplication *app) : JEventSource(resource_name, app) {
    SetTypeName(NAME_OF_THIS); // Provide JANA with class name

}

void CDaqEventSource::Open() {

    NetworkOpen();
}



void CDaqEventSource::GetEvent(std::shared_ptr<JEvent> event) {
    static int count=0;
    count++;
    
    try {
        CDaqTCPevent* tcpevent = ReadNetworkEvent(count);

        if(tcpevent == nullptr) {
            m_log->warn("tcpevent==0 event# {}", count);
        }

        if(tcpevent->DATA.size() == 0 || tcpevent->lenDATA == 0)
        {
            m_log->warn("tcpevent->DATA.size() {} == 0 || tcpevent->lenDATA {} == 0", tcpevent->DATA.size() == 0, tcpevent->lenDATA == 0);
        }

        tcpevent->DATA.resize(tcpevent->lenDATA);


        //--------------------  Parse the event ---------------------------

        // Here we use the same EVIOBlockedEventParser class that is used
        // to parse events read from a file. This requires we present the
        // data in the form of a EVIOBlockedEvent. However, we would also
        // like to insert the CDaqTCPevent object into a JANA factory. We
        // want to do this effieciently, without copying the entire data block.
        // Thus, we "loan" the data block to our local (temporary)  EVIOBlockedEvent
        // for purposes of parsing and then return it to the tcpevent.
        EVIOBlockedEvent block;
        block.data.swap(tcpevent->DATA); // loan event data to block

        PrintEVIOBlockHeader(block.data.data());

        // Get events from current block
        EVIOBlockedEventParser parser;
        parser.Configure(m_parser_config);
        auto events = parser.ParseEVIOBlockedEvent(block, event);
        m_log->debug("Parsed block {} had {} events, swap_needed={}", block.block_number, events.size(), block.swap_needed);
        for (size_t i = 0; i < events.size(); i++) {
            auto &parsed_event = events[i];
            m_log->trace("  Event #{} got event-number={}:", i, parsed_event->GetEventNumber());

            for (auto factory: parsed_event->GetFactorySet()->GetAllFactories()) {
                m_log->trace("    Factory = {:<30}  NumObjects = {}", factory->GetObjectName(), factory->GetNumObjects());
            }
        }

        // Insert the CDaqTCPevent into JANA event.
        block.data.swap(tcpevent->DATA); // return loaned event data back to tcpevent
        event->Insert(tcpevent);  // JANA now owns tcpevent

    } catch (std::exception exp) {
        printf("EXCEPTION IN CDAQEVENTSOURCE %s\n", exp.what());
        throw RETURN_STATUS::kTRY_AGAIN;
    }

}

std::string CDaqEventSource::GetDescription() {

    /// GetDescription() helps JANA explain to the user what is going on
    return "Reads EVIO events from CDaq from TCP";
}


template<>
double JEventSourceGeneratorT<CDaqEventSource>::CheckOpenable(std::string resource_name) {

    /// CheckOpenable() decides how confident we are that this EventSource can handle this resource.
    ///    0.0        -> 'Cannot handle'
    ///    (0.0, 1.0] -> 'Can handle, with this confidence level'

    /// To determine confidence level, feel free to open up the file and check for magic bytes or metadata.
    /// Returning a confidence <- {0.0, 1.0} is perfectly OK!

    // For convenience, we convert resource_name to lower case
    std::transform(resource_name.begin(), resource_name.end(), resource_name.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // And also check all conceivable variants
    return ((resource_name == "tcpcdaqevio") || (resource_name == "tcp-cdaq-evio") ||
            (resource_name == "tcp-cdaq-evio")) ? 1.0 : 0.0;
}

void CDaqEventSource::ThrowOnErrno(const std::string &comment) {
    auto error_message = fmt::format("{}: {}", comment, strerror(errno));
    m_log->error(error_message);
    throw JException(error_message);

}

void CDaqEventSource::PrintEVIOBlockHeader(uint32_t* buff) {
    using namespace std;
        cout << endl;
        cout << "EVIO Block Header:" << endl;
        cout << "------------------------" << endl;
        cout << " Block Length: " << HexStr(buff[0]) << " (" << buff[0] << " words = " << (buff[0]>>(10-2)) << " kB)" << endl;
        cout << " Block Number: " << HexStr(buff[1]) << endl;
        cout << "Header Length: " << HexStr(buff[2]) << " (should be 8)" << endl;
        cout << "  Event Count: " << HexStr(buff[3]) << endl;
        cout << "   Reserved 1: " << HexStr(buff[4]) << endl;
        cout << "     Bit Info: " << HexStr(buff[5]>>8) << endl;
        cout << "      Version: " << HexStr(buff[5]&0xFF) << endl;
        cout << "   Reserved 3: " << HexStr(buff[6]) << endl;
        cout << "   Magic word: " << HexStr(buff[7]) << endl;
}

void CDaqEventSource::NetworkOpen() {
    m_port = 20249;
    m_remote_host = "localhost";
    GetApplication()->SetDefaultParameter("cdaq:port", m_port, "TCP port on remote host to connect to");
    GetApplication()->SetDefaultParameter("cdaq:host", m_remote_host, "TCP port on remote host to connect to");
    GetApplication()->SetDefaultParameter("daq:srs_window_raw:ntsamples", m_parser_config.NSAMPLES_GEMSRS, "Number of GEM SRS time samples per APV");


    m_log = GetApplication()->GetService<Log_service>()->logger(GetPluginName());
    auto param_prefix = GetPluginName();
    string default_level = "info";
    JApplication *app = GetApplication();

    // Logger. Get plugin level sub-Log
    m_log = app->GetService<Log_service>()->logger(param_prefix);

    // Get Log level from user parameter or default
    std::string log_level_str = default_level.empty() ?         // did user provide default level?
                                spdlog::extensions::LogLevelToString(m_log->level()) :   //
                                default_level;
    app->SetDefaultParameter(param_prefix + ":LogLevel", log_level_str,
                             "LogLevel: trace, debug, info, warn, err, critical, off");
    printf("CDAQ LOG LEVEL %s\n", log_level_str.c_str());
    m_log->set_level(spdlog::extensions::ParseLogLevel(log_level_str));

    m_log->info("GetResourceName() = {}", GetResourceName());


    //================================================================

    // The tcp_event interface uses the tcp_thread routines underneath
    // for doing the actual TCP operations. The way it works is the
    // following:
    //
    // 1. Set the host and port(see below) using tcp_event_host(host, port)
    // 2. Call either tcp_event_snd() or tcp_event_get(). The first time
    //    either of these is called, the socket will be opened. If
    //    tcp_event_snd() is called, then the connection is initiated
    //    to the specified host/port and the data sent. If tcp_event_get()
    //    is called, then the socket is opened on the port and we listen for
    //    a connection. In other words:
    //
    //       tcp_event_snd = we are the client
    //       tcp_event_get = we are the server
    //
    // 4. Only the first call to either of these, sets the socket up
    //    so subsequent calls will just use it.
    //
    // 3. This assumes a model where the client is sending data to the
    //    server.
    tcp_event_host((char*)m_remote_host.c_str(), m_port);

}

CDaqTCPevent *CDaqEventSource::ReadEventFromDisk(const std::string& base_name, int event_num) {
    // Vector to store the read data
    std::vector<uint32_t> data;

    // Open the binary file in read mode
    std::ifstream inputFile(fmt::format("{}_{}",base_name, event_num), std::ios::binary);

    // Check if the file was opened successfully
    if (!inputFile) {
        std::cerr << "Failed to open the file for reading.\n";
        return nullptr; // Return an error code
    }

    // Determine the file size
    inputFile.seekg(0, std::ios::end); // Move to the end of the file
    std::streamsize size = inputFile.tellg(); // Get the position, which represents the size
    inputFile.seekg(0, std::ios::beg); // Move back to the beginning of the file

    // Resize the vector to fit all the data
    data.resize(size / sizeof(uint32_t));

    // Read the data from the file into the vector
    if (size > 0) {
        inputFile.read(reinterpret_cast<char*>(data.data()), size);
    }

    // Close the file
    inputFile.close();

    // Optional: Print the read data
    std::cout << "Data read from the file:\n";
    for (uint32_t num : data) {
        std::cout << num << '\n';
    }

    auto tcpevent = new CDaqTCPevent(0);
    tcpevent->DATA = data;
    return tcpevent;
}

CDaqTCPevent *CDaqEventSource::ReadNetworkEvent(int count) {
    // Disable the timeout at this point since the call to tcp_event_get
    // may block indefinitely while waiting for an event.
    static std::once_flag co_to_flag;
    std::call_once( co_to_flag, [this](){
        m_log->info("Disabling JANA timeout due to networked event source.");
        japp->SetTimeoutEnabled(false);});

    // Diable ticker temporarily
    auto ticker = japp->IsTickerEnabled();
    japp->SetTicker(false);

    // Read event from TCP.
    // n.b. the first time this is called it will open a port and listen
    // for events. Subsequent calls will just listen. In all cases, this
    // may block indefinitely.
    auto tcpevent = new CDaqTCPevent( MAXDATA );
    auto res = tcp_event_get( tcpevent->HOST2, tcpevent->DATA.data(), &tcpevent->lenDATA, &tcpevent->Nr_Modules, &tcpevent->ModuleID, &tcpevent->TriggerID );
    // Restore ticker
    japp->SetTicker(ticker);

    // Check results
    if( res ){
        // Error reading event
        m_log->warn("Error reading event from TCP, result code = {}", res);
        delete tcpevent;
        return nullptr;
    } else {
        m_log->trace("HOST2={} lenDATA={} Nr_Modules={} ModuleID={} TriggerID={}",
                     tcpevent->HOST2, tcpevent->lenDATA, tcpevent->Nr_Modules, tcpevent->ModuleID, tcpevent->TriggerID);
    }

    std::ofstream outputFile(fmt::format("event2_{}", count), std::ios::binary);
    // Check if the file was opened successfully
    if (!outputFile) {
        std::cerr << "Failed to open the file for writing.\n";
        return tcpevent;
    }

    // Write the vector to the file
    if (!tcpevent->DATA.empty()) {
        // Writing the entire vector to the file at once
        outputFile.write(reinterpret_cast<const char*>(tcpevent->DATA.data()), tcpevent->DATA.size() * sizeof(uint32_t));
    }

    // Close the file
    outputFile.close();

    return tcpevent;
}

