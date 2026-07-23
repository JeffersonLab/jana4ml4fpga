
#ifndef _CDaqTCPEventSender_h_
#define _CDaqTCPEventSender_h_

#include <JANA/JEventProcessor.h>
#include <services/log/Log_service.h>
#include <spdlog/logger.h>
#include <extensions/spdlog/SpdlogMixin.h>

class CDaqTCPEventSender : public JEventProcessor {

    // Shared state (e.g. histograms, TTrees, TFiles) live
    std::mutex m_mutex;

    int m_port;
    std::string m_remote_host;

    int m_socket_fd;                            /// Socket file descriptor that is used in all socket C funcs
    std::shared_ptr<spdlog::logger> m_log;      /// logger
    int TCP_FLAG;                               /// 0 failed state, 1 working state
    
public:

    CDaqTCPEventSender();
    virtual ~CDaqTCPEventSender() = default;

    void Init() override;
    void Process(const std::shared_ptr<const JEvent>& event) override;
    void Finish() override;

};


#endif // _CDaqTCPEventSender_h_

