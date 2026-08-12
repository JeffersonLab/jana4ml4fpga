// Print the on-disk (compressed) size of every branch group in an events tree,
// biggest first. Use it to see what is actually filling up a large output file
// before deciding which -Pflat_tree:write_* groups to switch off.
//
//   root -l -b -q 'tree_branch_sizes.C("Run_008229_events.root")'
//
// Branches are grouped by their name prefix (srs_raw_*, gem_sample_data_*, ...)
// because that is the granularity the write switches work at.
#include <map>
#include <string>
#include <vector>
#include <algorithm>

void tree_branch_sizes(const char* filename, const char* treename = "events") {
    TFile file(filename);
    if (file.IsZombie()) { printf("Cannot open %s\n", filename); return; }

    TTree* tree = (TTree*)file.Get(treename);
    if (!tree) { printf("No tree '%s' in %s\n", treename, filename); return; }

    // Group prefixes match the -Pflat_tree:write_* switches
    const std::vector<std::string> groups = {
        "srs_raw", "gem_sample_data", "gem_scluster", "gem_peak", "srs_prerecon",
        "f125_wraw", "f250_wraw", "f125_pulse", "f250_pulse", "fpga_", "event_number"};

    std::map<std::string, Long64_t> sizes;
    Long64_t total = 0;

    for (auto obj : *tree->GetListOfBranches()) {
        TBranch* branch = (TBranch*)obj;
        Long64_t zipped = branch->GetZipBytes();
        total += zipped;

        std::string name = branch->GetName();
        std::string group = "other";
        for (const auto& g : groups) {
            if (name.rfind(g, 0) == 0) { group = g; break; }
        }
        sizes[group] += zipped;
    }

    std::vector<std::pair<std::string, Long64_t>> sorted(sizes.begin(), sizes.end());
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    Long64_t entries = tree->GetEntries();
    printf("\n%s : %lld entries, %.2f GB total in branches\n\n",
           filename, entries, total / 1e9);
    printf("  %-18s %12s %8s %12s\n", "group", "size(MB)", "share", "bytes/event");
    printf("  %s\n", std::string(54, '-').c_str());
    for (const auto& [group, size] : sorted) {
        if (size == 0) continue;
        printf("  %-18s %12.1f %7.1f%% %12.0f\n", group.c_str(), size / 1e6,
               100.0 * size / (total ? total : 1),
               entries ? double(size) / entries : 0.0);
    }
    printf("\n");
}
