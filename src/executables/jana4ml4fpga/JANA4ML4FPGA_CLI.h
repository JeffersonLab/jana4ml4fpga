//
// Created by Xinxin (Cissie) Mei on 2/27/23.
/// The code is taken from EICrecon but is simplified.
//

#ifndef JANA4ML4FPGA_JANA4ML4FPGA_CLI_H
#define JANA4ML4FPGA_JANA4ML4FPGA_CLI_H

#include <JANA/JApplication.h>

#include <rawdataparser/EVIOBlockedEvent.h>

namespace jana {

    enum Flag {
        Unknown,
        ShowUsage,
        ShowThisVersion,
        ShowJANAVersion,
        ShowDefaultPlugins,
        ShowAvailablePlugins,
        ShowConfigs,
        LoadConfigs,
        DumpConfigs,
        ListFactories
    };

    struct UserOptions {
        /// Code representation of all user options.
        /// This lets us cleanly separate args parsing from execution.

        std::map<Flag, bool> flags;
        std::map<std::string, std::string> params;
        std::vector<std::string> evio_filenames;
        std::string load_config_file;
        std::string dump_config_file;
    };

    /// Read the user options from the command line and initialize @param options.
    /// If there are certain flags, mark them as true.
    /// Push the evio file source strings to @param options.EVIOFilenames.
    /// Push the parameter strings to @param options.params as key-value pairs.
    /// If the user option is to load or dump a config file, initialize @param options.load/dump_config_file
    UserOptions GetCliOptions(int nargs, char *argv[], bool expect_extra=true);

    /// If the user option contains print only flags, print the info ann return true; otherwise return false.
    /// The print only flags include: "-v", "-h", "-L", "--list_default_plugins", "--list_available_plugins".
    /// When the info is shown, the application will exit immediately.
    bool HasPrintOnlyCliOptions(UserOptions& options, std::vector<std::string> const& default_plugins);

    void PrintUsage();
    void PrintVersion();

    /// List the @param default_plugins in a table.
    /// @param default_plugins is given at the top of the eicrecon.cc.
    void PrintDefaultPlugins(std::vector<std::string> const& default_plugins);

    /// Add the default plugins and the user input plugins to @param options.params.
    /// It comes before creating the @class JApplication, so that printer will print the plugins correctly.
    void AddPluginsToOptionParams(UserOptions& options, std::vector<std::string> const& default_plugins);

    void AddDefaultPluginsToJApplication(JApplication* app, std::vector<std::string> const& default_plugins);

    void PrintFactories(JApplication* app);

    /// Copy the @param options params (from the cli or the config file) to a JParameterManager @var para_mgr.
    /// Create an empty JApplication @var app.
    /// Add the event sources got from the cli input to @var app, and then return.
    /// @note The cli -Pkey=value pairs are not processed when the function returns. They are processed,
    /// or, added to @var app at calling JApplication::Initialize().
    JApplication* CreateJApplication(UserOptions& options);

    void AddEventSource(JApplication* app, UserOptions& options);

    int Execute(JApplication* app, UserOptions& options);
}

#endif //JANA4ML4FPGA_JANA4ML4FPGA_CLI_H
