//
// Created by xmei@jlab.org on 2/9/23.
//

#ifndef JANA4ML4FPGA_CDAQEVIOFILESOURCE_H
#define JANA4ML4FPGA_CDAQEVIOFILESOURCE_H

#include <string>
#include <atomic>
#include <chrono>
#include <cinttypes>

#include <JANA/JEventSource.h>
#include <JANA/JEventSourceGeneratorT.h>
#include <JANA/JApplication.h>
#include <JANA/JEvent.h>
#include <JANA/JFactory.h>

#include <evio/HDEVIO.h>
#include <evio/DModuleType.h>
#include <rawdataparser/EVIOBlockedEvent.h>
#include <rawdataparser/EVIOBlockedEventParserConfig.h>
#include <memory>
#include <spdlog/logger.h>

#define DEFAULT_READ_BUFF_LEN 4000000

class CDAQEVIOFileSource :
        public JEventSource{

public:

    CDAQEVIOFileSource(std::string resource_name, JApplication *app);

    virtual ~CDAQEVIOFileSource();

    void Open() override;

    void GetEvent(std::shared_ptr <JEvent>);

    static std::string GetDescription();

private:
    std::shared_ptr<spdlog::logger> m_log;   // aspect logger: "evio"
    std::string m_evio_filename = "";
    std::unique_ptr <HDEVIO> m_hdevio;
    EVIOBlockedEventParserConfig m_parser_config;

    uint32_t *m_buff = nullptr;
    uint32_t m_buff_len = DEFAULT_READ_BUFF_LEN;
    int m_block_number = 1;

    void OpenEVIOFile(std::string filename);
};

template<>
double JEventSourceGeneratorT<CDAQEVIOFileSource>::CheckOpenable(std::string);

#endif //JANA4ML4FPGA_CDAQEVIOFILESOURCE_H
