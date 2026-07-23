//
// Created by Dmitry Romanov on 3/8/2024.
// Subject to the terms in the LICENSE file found in the top-level directory.
//

#pragma once
/**
 * Omnifactories are a lightweight layer connecting JANA to generic algorithms
 * It is assumed multiple input data (controlled by input tags)
 * which might be changed by user parameters.
 */


#include <JANA/CLI/JVersion.h>
#include <JANA/JMultifactory.h>
#include <JANA/JEvent.h>
#include <spdlog/spdlog.h>
#include "extensions/spdlog/SpdlogExtensions.h"
#include "services/log/Log_service.h"

#include <string>
#include <vector>


struct EmptyConfig {};

template <typename ConfigT=EmptyConfig>
class CozyFactory : public JMultifactory {
public:

    /// ========================
    /// Handle input collections
    /// ========================

    struct InputBase {
        std::string type_name;
        std::vector<std::string> collection_names;
        bool is_variadic = false;

        virtual void GetCollection(const JEvent& event) = 0;
    };

    template <typename T>
    class Input : public InputBase {

        std::vector<const T*> m_data;

    public:
        Input(CozyFactory* owner, std::string default_tag="") {
            owner->RegisterInput(this);
            this->collection_names.push_back(default_tag);
            this->type_name = JTypeInfo::demangle<T>();
        }

        const std::vector<const T*>& operator()() { return m_data; }

    private:
        friend class CozyFactory;

        void GetCollection(const JEvent& event) {
            m_data = event.Get<T>(this->collection_names[0]);
        }
    };



    void RegisterInput(InputBase* input) {
        m_inputs.push_back(input);
    }


    /// =========================
    /// Handle output collections
    /// =========================

    struct OutputBase {
        std::string type_name;
        std::vector<std::string> collection_names;
        bool is_variadic = false;

        virtual void CreateHelperFactory(CozyFactory& fac) = 0;
        virtual void SetCollection(CozyFactory& fac) = 0;
        virtual void Reset() = 0;
    };

    template <typename T>
    class Output : public OutputBase {
        std::vector<T*> m_data;

    public:
        Output(CozyFactory* owner, std::string default_tag_name="") {
            owner->RegisterOutput(this);
            this->collection_names.push_back(default_tag_name);
            this->type_name = JTypeInfo::demangle<T>();
        }

        std::vector<T*>& operator()() { return m_data; }

    private:
        friend class CozyFactory;

        void CreateHelperFactory(CozyFactory& fac) override {
            fac.DeclareOutput<T>(this->collection_names[0]);
        }

        void SetCollection(CozyFactory& fac) override {
            fac.SetData<T>(this->collection_names[0], this->m_data);
        }

        void Reset() override {
            m_data.clear();
        }
    };



    void RegisterOutput(OutputBase* output) {
        m_outputs.push_back(output);

    }


    // =================
    // Handle parameters
    // =================

    struct ParameterBase {
        std::string m_name;
        std::string m_description;
        virtual void Configure(JParameterManager& parman, const std::string& prefix) = 0;
        virtual void Configure(std::map<std::string, std::string> fields) = 0;
    };

    template <typename T>
    class ParameterRef : public ParameterBase {

        T* m_data;

    public:
        ParameterRef(CozyFactory* owner, std::string name, T& slot, std::string description="") {
            owner->RegisterParameter(this);
            this->m_name = name;
            this->m_description = description;
            m_data = &slot;
        }

        const T& operator()() { return *m_data; }

    private:
        friend class CozyFactory;

        void Configure(JParameterManager& parman, const std::string& prefix) override {
            parman.SetDefaultParameter(prefix + ":" + this->m_name, *m_data, this->m_description);
        }
        void Configure(std::map<std::string, std::string> fields) override {
            auto it = fields.find(this->m_name);
            if (it != fields.end()) {
                const auto& value_str = it->second;
                if constexpr (10000 * JVersion::major
                              + 100 * JVersion::minor
                                + 1 * JVersion::patch < 20102) {
                    *m_data = JParameterManager::Parse<T>(value_str);
                } else {
                    JParameterManager::Parse(value_str, *m_data);
                }
            }
        }
    };

    template <typename T>
    class Parameter : public ParameterBase {

        T m_data;

    public:
        Parameter(CozyFactory* owner, std::string name, T default_value, std::string description) {
            owner->RegisterParameter(this);
            this->m_name = name;
            this->m_description = description;
            m_data = default_value;
        }

        const T& operator()() { return m_data; }

    private:
        friend class CozyFactory;

        void Configure(JParameterManager& parman, const std::string& prefix) override {
            parman.SetDefaultParameter(prefix + ":" + this->m_name, m_data, this->m_description);
        }
        void Configure(std::map<std::string, std::string> fields) override {
            auto it = fields.find(this->m_name);
            if (it != fields.end()) {
                const auto& value_str = it->second;
                if constexpr (10000 * JVersion::major
                              + 100 * JVersion::minor
                                + 1 * JVersion::patch < 20102) {
                    m_data = JParameterManager::Parse<T>(value_str);
                } else {
                    JParameterManager::Parse(value_str, m_data);
                }
            }
        }
    };



    void RegisterParameter(ParameterBase* parameter) {
        m_parameters.push_back(parameter);
    }

    void ConfigureAllParameters(std::map<std::string, std::string> fields) {
        for (auto* parameter : this->m_parameters) {
            parameter->Configure(fields);
        }
    }

    // ===============
    // Handle services
    // ===============

    struct ServiceBase {
        virtual void Init(JApplication* app) = 0;
    };

    template <typename ServiceT>
    class Service : public ServiceBase {

        std::shared_ptr<ServiceT> m_data;

    public:

        Service(CozyFactory* owner) {
            owner->RegisterService(this);
        }

        ServiceT& operator()() {
            return *m_data;
        }

    private:

        friend class CozyFactory;

        void Init(JApplication* app) {
            m_data = app->GetService<ServiceT>();
        }

    };

    void RegisterService(ServiceBase* service) {
        m_services.push_back(service);
    }


    // ================
    // Handle resources
    // ================

    struct ResourceBase {
        virtual void ChangeRun(const JEvent& event) = 0;
    };

    template <typename ServiceT, typename ResourceT, typename LambdaT>
    class Resource : public ResourceBase {
        ResourceT m_data;
        LambdaT m_lambda;

    public:

        Resource(CozyFactory* owner, LambdaT lambda) : m_lambda(lambda) {
            owner->RegisterResource(this);
        };

        const ResourceT& operator()() { return m_data; }

    private:
        friend class CozyFactory;

        void ChangeRun(const JEvent& event) {
            auto run_nr = event.GetRunNumber();
            std::shared_ptr<ServiceT> service = event.GetJApplication()->template GetService<ServiceT>();
            m_data = m_lambda(service, run_nr);
        }
    };

    void RegisterResource(ResourceBase* resource) {
        m_resources.push_back(resource);
    }


public:
    std::vector<InputBase*> m_inputs;
    std::vector<OutputBase*> m_outputs;
    std::vector<ParameterBase*> m_parameters;
    std::vector<ServiceBase*> m_services;
    std::vector<ResourceBase*> m_resources;

private:

    // App belongs on JMultifactory, it is just missing temporarily
    JApplication* m_app;

    // Plugin name belongs on JMultifactory, it is just missing temporarily
    std::string m_plugin_name;

    // Prefix for parameters and loggers, derived from plugin name and tag in PreInit().
    std::string m_prefix;

    /// Current logger
    std::shared_ptr<spdlog::logger> m_logger;


public:


    inline void PreInit(std::string tag) {

        m_prefix = (this->GetPluginName().empty()) ? tag : this->GetPluginName() + ":" + tag;

        // Set output collection names and create corresponding helper factories
        for (auto* output : m_outputs) {
            output->CreateHelperFactory(*this);
        }

        // Obtain logger
        m_logger = m_app->GetService<Log_service>()->logger(m_prefix);

        // Configure logger. Priority = [JParameterManager, system log level]
        std::string default_log_level = spdlog::extensions::LogLevelToString(m_logger->level());
        m_app->SetDefaultParameter(m_prefix + ":LogLevel", default_log_level, "LogLevel: trace, debug, info, warn, err, critical, off");
        m_logger->set_level(spdlog::extensions::ParseLogLevel(default_log_level));
    }


    virtual void CozyInit() {};
    virtual void CozyProcess(uint64_t run_number, uint64_t event_number) {};
    virtual void CozyBeginRun(uint64_t run_number) {};
    virtual void CozyFinish() {};


    void Init() override {
        auto app = GetApplication();
        for (auto* parameter : m_parameters) {
            parameter->Configure(*(app->GetJParameterManager()), m_prefix);
        }
        for (auto* service : m_services) {
            service->Init(app);
        }
        CozyInit();
    }

    void BeginRun(const std::shared_ptr<const JEvent>& event) override {
        for (auto* resource : m_resources) {
            resource->ChangeRun(*event);
        }
        CozyBeginRun(event->GetRunNumber());
    }

    void Process(const std::shared_ptr<const JEvent> &event) override {
        try {
            for (auto* input : m_inputs) {
                input->GetCollection(*event);
            }
            for (auto* output : m_outputs) {
                output->Reset();
            }
            CozyProcess(event->GetRunNumber(), event->GetEventNumber());
            for (auto* output : m_outputs) {
                output->SetCollection(*this);
            }
        }
        catch(std::exception &e) {
            m_logger->warn("Exception during process {}", e.what());
            //m_logger->warn("Exception during process {}", e.what());
            //throw JException();
        }
    }

    void Finish() override {
        CozyFinish();
    };



    void SetApplication(JApplication* app) { m_app = app; }

    JApplication* GetApplication() { return m_app; }

    void SetPluginName(std::string plugin_name) { m_plugin_name = plugin_name; }

    std::string GetPluginName() { return m_plugin_name; }

    inline std::string GetPrefix() { return m_prefix; }

    /// Retrieve reference to already-configured logger
    std::shared_ptr<spdlog::logger> &logger() { return m_logger; }



};

