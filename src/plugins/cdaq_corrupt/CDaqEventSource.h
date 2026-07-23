#pragma once

#include <JANA/JEventSource.h>
#include <JANA/JEventSourceGeneratorT.h>
#include <rawdataparser/EVIOBlockedEventParser.h>
#include <spdlog/logger.h>
#include <extensions/spdlog/SpdlogMixin.h>
#include <sys/socket.h>
#include <future>
#include "rawdataparser/CDaqTCPevent.h"
#include "evio/HDEVIO.h"
//#include "rawdataparser/EVIOFileWriter.h"

class CDaqEventSource : public JEventSource, public spdlog::extensions::SpdlogMixin<CDaqEventSource>
{

    /// Add member variables here

public:
    CDaqEventSource(std::string resource_name, JApplication* app);

    virtual ~CDaqEventSource() = default;

    void Open() override;

    void GetEvent(std::shared_ptr<JEvent>) override;
    
    static std::string GetDescription();

private:

    void ThrowOnErrno (const std::string& comment);

    int32_t TcpReadData(int read_fd, uint32_t *data, int data_len) {
        int bytes_left = data_len;
        auto data_bytes = (char *) data;
        while (bytes_left > 0) {
            // Read data
            int read_len = recv(read_fd, data_bytes, bytes_left, 0);
            if (read_len <= 0) {
                perror("    ERROR at tcp_get recv()");
                return -1;
            }

            // Prepare for the next read
            bytes_left -= read_len;
            data_bytes += read_len;
        }
        return bytes_left;
    }

    void WaitForClient();

    int m_port;
    std::string m_remote_host;

    int m_socket_fd;                            /// Socket file descriptor that is used in all socket C funcs
    std::shared_ptr<spdlog::logger> m_log;      /// logger
    int TCP_FLAG;                               /// 0 failed state, 1 working state

    // We wait for a client in a separate thread. This thread returns file descriptor that can be used
    // for 'recv' function, to get data from socket. This field is used for:
    // 1. Check someone is connected as this promise is done when this happnes
    // 2. Get receive file descriptor that is needed for further reads
    std::atomic<int> m_receive_fd=-1;
    std::thread m_listen_thread;               /// Thread is used to wait for a client to connect

    // EVIO block parser
    EVIOBlockedEventParserConfig m_parser_config;
    // EVIOBlockedEventParser parser; // TODO: make this persistent
    size_t m_end_of_runs_count=0;
    //EVIOFileWriter *m_evio_output_file;

    void PrintEVIOBlockHeader(uint32_t* buff);

    void NetworkOpen();

    CDaqTCPevent *ReadNetworkEvent(int count);

    CDaqTCPevent *ReadEventFromDisk(const string &base_name, int event_num);
};

template <>
double JEventSourceGeneratorT<CDaqEventSource>::CheckOpenable(std::string);



