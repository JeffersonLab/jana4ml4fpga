#pragma once


#include <iostream>
#include <vector>
#include <string>
#include <mutex>

#include <JANA/JApplication.h>
#include <JANA/Services/JGlobalRootLock.h>
#include <JANA/Services/JServiceLocator.h>
#include <services/log/Log_service.h>
#include <services/root_output/RootFile_service.h>

#include <TFile.h>

/**
 * This Service centralizes creation of Data quality monitor
 */
class DataQualityMonitorService : public JService
{
public:
    explicit DataQualityMonitorService(JApplication *app ):m_app(app){}
    ~DataQualityMonitorService() override = default;

    void acquire_services(JServiceLocator *locater) override {
        auto log_service = m_app->GetService<Log_service>();
        auto root_file_service = m_app->GetService<RootFile_service>();
        m_log = log_service->logger("dqm");
        m_root_file = root_file_service->GetHistFile();
        m_top_dir = m_root_file->mkdir("dqm", "Data Quality Monitoring ", /*returnExistingDirectory*/ true);
        m_events_dir = m_top_dir->mkdir("events", "Plots per event", /*returnExistingDirectory*/ true);
        m_integral_dir = m_top_dir->mkdir("integral", "Plots over events range", /*returnExistingDirectory*/ true);

        // Get TDirectory for histograms root file
        m_glb_lock = m_app->GetService<JGlobalRootLock>();
        m_app->SetDefaultParameter("dqm:min_event", m_min_event_index, "Min event number, when DQM working.");
        m_app->SetDefaultParameter("dqm:max_event", m_max_event_index, "Max event number, when DQM working. 0 - no cap");
        m_app->SetDefaultParameter("dqm:every", m_min_event_index, "DQM work every x events: 1 - every event, 2 - once in 2 events, etc.");
    }

    /// This will return a pointer to the top-level directory for current file
    ///
    /// \return main DQM directory
    TDirectory* GetPerEventDir(uint64_t entry_num){
        m_glb_lock->acquire_write_lock();
        std::string dir_name = "evt_" + std::to_string(entry_num);
        auto result  = m_events_dir->mkdir(dir_name.c_str(), /*title=*/"", /*returnExistingDirectory=*/true);
        m_glb_lock->release_lock();
        return result;
    }

    /// This will return a pointer to the top-level directory for current file
    ///
    /// \return main DQM directory
    TDirectory* GetPerEventSubDir(uint64_t entry_num, const std::string& subdir_name){
        m_glb_lock->acquire_write_lock();
        std::string dir_name = "evt_" + std::to_string(entry_num);
        auto evt_dir  = m_events_dir->mkdir(dir_name.c_str(), /*title=*/"", /*returnExistingDirectory=*/true);
        auto result  = evt_dir->mkdir(subdir_name.c_str(), /*title=*/"", /*returnExistingDirectory=*/true);
        m_glb_lock->release_lock();
        return result;
    }

    /// This will return a pointer to the top-level directory for current file
    ///
    /// \return main DQM directory
    TDirectory* GetIntegralSubDir(const std::string& subdir_name){
        m_glb_lock->acquire_write_lock();
        auto result  = m_integral_dir->mkdir(subdir_name.c_str(), /*title=*/"", /*returnExistingDirectory=*/true);
        m_glb_lock->release_lock();
        return result;
    }

    /// This will return a pointer to the top-level directory for current file
    ///
    /// \return main DQM directory
    TDirectory* GetIntegralDir(){
        return m_integral_dir;
    }

    bool ShouldProcessEvent(size_t event_index) {
        bool is_step_ok = event_index > 1 && (event_index % m_step) == 0;
        if(is_step_ok
           && event_index >= m_min_event_index
           && (m_max_event_index == 0 || event_index <= m_max_event_index)) {
            return true;
        }
        return false;
    }



private:

    DataQualityMonitorService()=default;

    JApplication *m_app=nullptr;
    std::shared_ptr<spdlog::logger> m_log;
    TDirectory *m_root_file;                        /// pointer to a TDirectory object which represents the ROOT file.
    TDirectory *m_top_dir;                          /// top DQM directory
    TDirectory *m_events_dir;                       /// TDirectory where each event subdir is stored
    std::shared_ptr<JGlobalRootLock> m_glb_lock;    /// Global ROOT lock
    TDirectory *m_integral_dir;
    size_t m_min_event_index = 100;
    size_t m_max_event_index = 5000;
    size_t m_step = 1;
};

