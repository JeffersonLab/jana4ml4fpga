// Diagnostic plots from the gem_sample_data_* branches of the flat events tree.
//
//   root -l -b -q 'plot_gem_samples.C("run8169.root", "ai_rework/plots/gem_samples")'
//
// These are the per-sample GEM diagnostics: one row per (APV channel x time bin)
// carrying the raw ADC value, the rolling baseline (mean/sigma over the last
// gemrecon:stat_len events), the baseline-subtracted value and an n-sigma noise
// flag (is_noise = |raw - rolling_avg| < n*rolling_std, i.e. TRUE means "this
// sample is consistent with baseline").
//
// IMPORTANT: everything is filled in ONE MANUAL LOOP rather than with
// TTree::Draw. Draw's element-wise cuts on the std::vector<bool> is_noise branch
// silently mis-pair the flag with the other per-sample vectors, which produces
// correct COUNTS but wrong value distributions. Do not "simplify" this back to
// Draw with an is_noise cut.
//
// Summary numbers are printed as well as drawn - check those first.
#include <cstdio>
#include <vector>

void plot_gem_samples(const char* filename,
                      const char* outdir = "ai_rework/plots/gem_samples") {
    gStyle->SetOptStat(1110);
    gStyle->SetPalette(kBird);
    gSystem->mkdir(outdir, kTRUE);

    TFile file(filename);
    if (file.IsZombie()) { printf("Cannot open %s\n", filename); return; }
    TTree* tree = (TTree*)file.Get("events");
    if (!tree) { printf("No 'events' tree in %s\n", filename); return; }

    std::vector<bool>*     is_noise = nullptr;
    std::vector<double>*   value    = nullptr;
    std::vector<double>*   raw      = nullptr;
    std::vector<double>*   avg      = nullptr;
    std::vector<double>*   sigma    = nullptr;
    std::vector<uint32_t>* time_bin = nullptr;
    std::vector<uint32_t>* channel  = nullptr;

    tree->SetBranchAddress("gem_sample_data_is_noise",        &is_noise);
    tree->SetBranchAddress("gem_sample_data_value",           &value);
    tree->SetBranchAddress("gem_sample_data_raw_value",       &raw);
    tree->SetBranchAddress("gem_sample_data_rolling_average", &avg);
    tree->SetBranchAddress("gem_sample_data_rolling_std",     &sigma);
    tree->SetBranchAddress("gem_sample_data_time_bin",        &time_bin);
    tree->SetBranchAddress("gem_sample_data_channel",         &channel);

    TH1D h_raw ("h_raw",  "Raw ADC sample value;raw_value [ADC];samples", 200, 1800, 2400);
    TH1D h_val ("h_val",  "Baseline-subtracted value;value = -(raw - rolling_avg) [ADC];samples", 200, -500, 500);
    TH1D h_avg ("h_avg",  "Rolling baseline;rolling_average [ADC];samples", 200, 1800, 2400);
    TH1D h_std ("h_std",  "Channel noise;rolling_std [ADC];samples", 200, 0, 50);
    TH1D h_tb  ("h_tb",   "Signal samples per time bin;time_bin;samples over n-sigma", 16, -0.5, 15.5);
    TH2D h_occ ("h_occ",  "Signal occupancy;APV channel;value [ADC]", 128, 0, 128, 100, -200, 1400);

    Long64_t n_events = tree->GetEntries();
    Long64_t n_tot = 0, n_sig = 0;
    double sum_sig = 0, sum_noi = 0;

    for (Long64_t e = 0; e < n_events; ++e) {
        tree->GetEntry(e);
        if (!value) continue;
        for (size_t i = 0; i < value->size(); ++i) {
            ++n_tot;
            h_raw.Fill((*raw)[i]);
            h_val.Fill((*value)[i]);
            h_avg.Fill((*avg)[i]);
            h_std.Fill((*sigma)[i]);

            const bool noise = (*is_noise)[i];
            if (!noise) {                       // signal: above n-sigma
                ++n_sig;
                sum_sig += std::abs((*value)[i]);
                h_tb.Fill((*time_bin)[i]);
                h_occ.Fill((*channel)[i], (*value)[i]);
            } else {
                sum_noi += std::abs((*value)[i]);
            }
        }
    }

    if (n_tot == 0) {
        printf("\nNo gem_sample_data in %s.\n"
               "Was gemrecon2 loaded, and is -Pflat_tree:write_gem_samples on?\n", filename);
        return;
    }

    printf("\n=== gem_sample_data summary: %s ===\n", filename);
    printf("  events              : %lld\n", n_events);
    printf("  samples total       : %lld  (%.0f per event)\n", n_tot, double(n_tot) / n_events);
    printf("  signal (is_noise=0) : %lld  (%.3f%%)\n", n_sig, 100.0 * n_sig / n_tot);
    printf("  mean |value| signal : %.2f ADC\n", n_sig ? sum_sig / n_sig : 0.0);
    printf("  mean |value| noise  : %.2f ADC\n", (n_tot - n_sig) ? sum_noi / (n_tot - n_sig) : 0.0);
    printf("  raw_value           : mean %.1f  rms %.1f\n", h_raw.GetMean(), h_raw.GetRMS());
    printf("  rolling_average     : mean %.1f  rms %.1f\n", h_avg.GetMean(), h_avg.GetRMS());
    printf("  rolling_std         : mean %.2f  rms %.2f\n", h_std.GetMean(), h_std.GetRMS());
    for (int i = 1; i <= h_tb.GetNbinsX(); ++i)
        if (h_tb.GetBinContent(i) > 0)
            printf("  time_bin %-2d         : %.0f signal samples\n", i - 1, h_tb.GetBinContent(i));

    TCanvas c("c", "GEM sample diagnostics", 1800, 1000);
    c.Divide(3, 2);
    c.cd(1); gPad->SetLogy(); h_raw.DrawCopy();
    c.cd(2); gPad->SetLogy(); h_val.DrawCopy();
    c.cd(3);                  h_avg.DrawCopy();
    c.cd(4); gPad->SetLogy(); h_std.DrawCopy();
    c.cd(5);                  h_tb.DrawCopy();
    c.cd(6); gPad->SetLogz(); h_occ.DrawCopy("COLZ");

    TString png = TString::Format("%s/gem_samples.png", outdir);
    c.SaveAs(png);
    printf("\n  canvas -> %s\n\n", png.Data());
}
