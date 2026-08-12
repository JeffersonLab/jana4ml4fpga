// GEM reconstruction chain, stage by stage, from the flat events tree.
//
//   root -l -b -q 'plot_gem_chain.C("run8169.root", "ai_rework/plots/gem_chain")'
//
// Produces two figures:
//
//   chain_event.png       - one event walked through every stage of the chain
//   chain_population.png  - the same quantities over all events in the file
//
// The chain (gemreco library) is:
//
//   raw SRS ADC samples            gem_sample_data_raw_value
//     -> rolling baseline          gem_sample_data_rolling_average / _rolling_std
//     -> baseline subtraction      gem_sample_data_value
//     -> merge APVs into planes    srs_prerecon_x / _y   (amplitude[time][strip])
//     -> find peaks on each plane  gem_peak_*            (index, height, width, area)
//     -> match X/Y peaks           gem_scluster_*        (x, y, energy, adc)
//
// NOTE: all per-sample vectors are read element-wise in manual loops. Do not use
// TTree::Draw with a cut on the std::vector<bool> is_noise branch - it mis-pairs
// the flag with the other vectors (counts right, distributions wrong).
#include <vector>
#include <string>

static const int kNStripBins = 256;   // strips per plane (prerecon count / n_time_bins)

// warm_up: entries to skip when auto-picking the display event. With
// gemrecon2:freeze_after=0 the pedestals are learned on the fly, so the first
// ~50-500 events produce a quantised, unphysical plane profile and a ~6x
// inflated peak rate. Anything plotted from there is calibration warm-up, not
// detector response.
void plot_gem_chain(const char* filename,
                    const char* outdir = "ai_rework/plots/gem_chain",
                    Long64_t force_entry = -1,
                    Long64_t warm_up = 500) {
    gStyle->SetOptStat(0);
    gStyle->SetPalette(kBird);
    gStyle->SetTitleSize(0.055, "t");
    gSystem->mkdir(outdir, kTRUE);

    TFile file(filename);
    if (file.IsZombie()) { printf("Cannot open %s\n", filename); return; }
    TTree* tree = (TTree*)file.Get("events");
    if (!tree) { printf("No 'events' tree in %s\n", filename); return; }

    // ---- branches -----------------------------------------------------------
    std::vector<double>   *pre_x = nullptr, *pre_y = nullptr;
    std::vector<double>   *pk_height = nullptr, *pk_area = nullptr, *pk_pos = nullptr;
    std::vector<uint32_t> *pk_index = nullptr, *pk_time = nullptr, *pk_width = nullptr;
    std::vector<std::string>* pk_plane = nullptr;
    std::vector<double>   *cl_x = nullptr, *cl_y = nullptr, *cl_e = nullptr, *cl_adc = nullptr;
    std::vector<double>   *sd_raw = nullptr, *sd_avg = nullptr, *sd_val = nullptr, *sd_std = nullptr;
    std::vector<uint32_t> *sd_chan = nullptr, *sd_apv = nullptr, *sd_tb = nullptr;
    std::vector<bool>     *sd_noise = nullptr;
    ULong64_t n_peaks = 0, n_clusters = 0, n_pre = 0, evnum = 0;

    tree->SetBranchAddress("srs_prerecon_x", &pre_x);
    tree->SetBranchAddress("srs_prerecon_y", &pre_y);
    tree->SetBranchAddress("srs_prerecon_count", &n_pre);
    tree->SetBranchAddress("gem_peak_count", &n_peaks);
    tree->SetBranchAddress("gem_peak_index", &pk_index);
    tree->SetBranchAddress("gem_peak_time_id", &pk_time);
    tree->SetBranchAddress("gem_peak_height", &pk_height);
    tree->SetBranchAddress("gem_peak_width", &pk_width);
    tree->SetBranchAddress("gem_peak_area", &pk_area);
    tree->SetBranchAddress("gem_peak_real_pos", &pk_pos);
    tree->SetBranchAddress("gem_peak_plane_name", &pk_plane);
    tree->SetBranchAddress("gem_scluster_count", &n_clusters);
    tree->SetBranchAddress("gem_scluster_x", &cl_x);
    tree->SetBranchAddress("gem_scluster_y", &cl_y);
    tree->SetBranchAddress("gem_scluster_energy", &cl_e);
    tree->SetBranchAddress("gem_scluster_adc", &cl_adc);
    tree->SetBranchAddress("event_number", &evnum);
    const bool has_samples = tree->GetBranch("gem_sample_data_value") != nullptr;
    if (has_samples) {
        tree->SetBranchAddress("gem_sample_data_raw_value", &sd_raw);
        tree->SetBranchAddress("gem_sample_data_rolling_average", &sd_avg);
        tree->SetBranchAddress("gem_sample_data_rolling_std", &sd_std);
        tree->SetBranchAddress("gem_sample_data_value", &sd_val);
        tree->SetBranchAddress("gem_sample_data_channel", &sd_chan);
        tree->SetBranchAddress("gem_sample_data_apv", &sd_apv);
        tree->SetBranchAddress("gem_sample_data_time_bin", &sd_tb);
        tree->SetBranchAddress("gem_sample_data_is_noise", &sd_noise);
    }

    const Long64_t n_entries = tree->GetEntries();

    // ---- pick the event to display -----------------------------------------
    // Past the calibration warm-up, and with a peak on one of the two planes the
    // srs_prerecon profile actually stores (peaks are found on every detector
    // plane, but only -Pgemrecon:plane_name_x/_y are written as profiles - a peak
    // on any other plane cannot be drawn on top of them).
    Long64_t best = force_entry;
    if (best < 0) {
        ULong64_t best_score = 0;
        for (Long64_t e = warm_up; e < n_entries; ++e) {
            tree->GetEntry(e);
            if (!pre_x || pre_x->size() < size_t(kNStripBins)) continue;
            // Prefer a NARROW peak: ~95% of "peaks" are >=32 strips wide and sit
            // on the flat unmapped part of the plane (see the report) - those are
            // finder artifacts, not detector signal, and make a useless display.
            ULong64_t score = 0;
            for (size_t p = 0; p < n_peaks && pk_plane && p < pk_plane->size(); ++p) {
                const std::string& pl = (*pk_plane)[p];
                if (pl.find("1X") == std::string::npos && pl.find("1Y") == std::string::npos)
                    continue;
                if (p < pk_width->size() && (*pk_width)[p] < 32 && (*pk_index)[p] < 128)
                    score += 100;          // narrow peak in the mapped region
                else
                    score += 1;
            }
            score += 1000 * n_clusters;
            if (score > best_score) { best_score = score; best = e; }
        }
        if (best < 0)
            printf("WARNING: no post-warm-up event with a peak on a profiled plane\n");
    }
    if (best < 0) { printf("No suitable event found\n"); return; }

    // =========================================================================
    //  FIGURE 1 - one event through the chain
    // =========================================================================
    tree->GetEntry(best);
    const int n_tb = n_pre > 0 ? int(n_pre / kNStripBins) : 3;
    printf("\n=== event display: entry %lld (event_number %llu) ===\n", best, evnum);
    printf("  peaks=%llu clusters=%llu prerecon=%llu (%d time bins x %d strips)\n",
           n_peaks, n_clusters, n_pre, n_tb, kNStripBins);

    TCanvas c1("c1", "GEM reconstruction chain - one event", 1900, 1150);
    c1.Divide(3, 2);
    const int col[4] = {kAzure + 2, kOrange + 7, kGreen + 2, kMagenta + 1};

    // --- (1) INPUT: raw ADC per channel -------------------------------------
    c1.cd(1); gPad->SetGrid();
    TH1D* h_in = new TH1D("h_in", "STEP 0  INPUT: raw SRS ADC;APV*128 + channel;raw ADC",
                          1600, 0, 1600);
    if (has_samples)
        for (size_t i = 0; i < sd_raw->size(); ++i)
            if ((*sd_tb)[i] == 0)
                h_in->SetBinContent(int((*sd_apv)[i] * 128 + (*sd_chan)[i]) + 1, (*sd_raw)[i]);
    h_in->SetLineColor(col[0]); h_in->GetYaxis()->SetRangeUser(1900, 2200);
    h_in->Draw("HIST");

    // --- (2) rolling baseline ------------------------------------------------
    c1.cd(2); gPad->SetGrid();
    TH1D* h_ped = new TH1D("h_ped", "STEP 1  rolling baseline (pedestal);APV*128 + channel;ADC",
                           1600, 0, 1600);
    if (has_samples)
        for (size_t i = 0; i < sd_avg->size(); ++i)
            if ((*sd_tb)[i] == 0)
                h_ped->SetBinContent(int((*sd_apv)[i] * 128 + (*sd_chan)[i]) + 1, (*sd_avg)[i]);
    h_ped->SetLineColor(col[1]); h_ped->GetYaxis()->SetRangeUser(1900, 2200);
    h_ped->Draw("HIST");

    // --- (3) baseline-subtracted --------------------------------------------
    c1.cd(3); gPad->SetGrid();
    TH1D* h_sub = new TH1D("h_sub",
        "STEP 2  baseline-subtracted  value = -(raw - baseline);APV*128 + channel;ADC", 1600, 0, 1600);
    if (has_samples)
        for (size_t i = 0; i < sd_val->size(); ++i)
            if ((*sd_tb)[i] == 0)
                h_sub->SetBinContent(int((*sd_apv)[i] * 128 + (*sd_chan)[i]) + 1, (*sd_val)[i]);
    h_sub->SetLineColor(col[2]); h_sub->Draw("HIST");

    // --- (4)(5) plane profiles with peaks -----------------------------------
    // srs_prerecon_{x,y} is amplitude[time][strip], flattened time-major.
    auto draw_plane = [&](int pad, const std::vector<double>* prof, const char* pname,
                          const char* title) {
        c1.cd(pad); gPad->SetGrid();
        std::vector<TH1D*> hs;
        std::vector<double> all;               // for robust (percentile) y-range
        for (int t = 0; t < n_tb; ++t) {
            auto* h = new TH1D(Form("h_%s_%d", pname, t), title, kNStripBins, 0, kNStripBins);
            for (int s = 0; s < kNStripBins; ++s) {
                size_t idx = size_t(t) * kNStripBins + s;
                if (idx < prof->size()) {
                    h->SetBinContent(s + 1, (*prof)[idx]);
                    all.push_back((*prof)[idx]);
                }
            }
            h->SetLineColor(col[t % 4]); h->SetLineWidth(2);
            hs.push_back(h);
        }
        // Dead/masked strips sit ~1500 ADC below everything else. A percentile
        // cut is not enough (they are ~15% of the strips), so use median +- MAD,
        // which ignores them entirely, and then make sure any marked peak fits.
        std::sort(all.begin(), all.end());
        double med = all.empty() ? 0 : all[all.size() / 2];
        std::vector<double> dev;
        dev.reserve(all.size());
        for (double v : all) dev.push_back(std::abs(v - med));
        std::sort(dev.begin(), dev.end());
        double mad = dev.empty() ? 1 : std::max(1.0, dev[dev.size() / 2]);
        double lo = med - 6 * mad;
        double hi = med + 10 * mad;
        for (size_t p = 0; p < n_peaks && p < pk_index->size(); ++p)
            if (!pk_plane || p >= pk_plane->size() ||
                (*pk_plane)[p].find(pname) != std::string::npos)
                hi = std::max(hi, (*pk_height)[p]);
        double pad_span = (hi - lo) * 0.15 + 1;
        for (size_t t = 0; t < hs.size(); ++t) {
            hs[t]->GetYaxis()->SetRangeUser(lo - pad_span, hi + pad_span * 2);
            hs[t]->Draw(t == 0 ? "HIST" : "HIST SAME");
        }
        // mark the peaks the algorithm found on this plane
        auto* leg = new TLegend(0.62, 0.70, 0.98, 0.92);
        leg->SetBorderSize(0); leg->SetFillStyle(0);
        for (int t = 0; t < n_tb && t < (int)hs.size(); ++t)
            leg->AddEntry(hs[t], Form("time bin %d", t), "l");
        int n_marked = 0;
        for (size_t p = 0; p < n_peaks && p < pk_index->size(); ++p) {
            if (pk_plane && p < pk_plane->size() &&
                (*pk_plane)[p].find(pname) == std::string::npos) continue;
            double xpos = (*pk_index)[p] + 0.5;
            auto* m = new TMarker(xpos, (*pk_height)[p], 23);
            m->SetMarkerColor(kRed); m->SetMarkerSize(2.0); m->Draw();
            auto* l = new TLine(xpos, lo - pad_span, xpos, hi + pad_span);
            l->SetLineColor(kRed); l->SetLineStyle(2); l->Draw();
            if (!n_marked++) leg->AddEntry(m, "peak found", "p");
        }
        leg->Draw();
        printf("  plane %-8s : %d peaks marked\n", pname, n_marked);
    };
    draw_plane(4, pre_x, "X", "STEP 3+4  plane X profile -> peak finding;strip;amplitude [ADC]");
    draw_plane(5, pre_y, "Y", "STEP 3+4  plane Y profile -> peak finding;strip;amplitude [ADC]");

    // --- (6) peak properties for this event ---------------------------------
    c1.cd(6);
    auto* txt = new TPaveText(0.02, 0.02, 0.98, 0.98);
    txt->SetFillColor(0); txt->SetBorderSize(1); txt->SetTextAlign(12); txt->SetTextFont(42);
    txt->AddText(Form("#bf{STEP 5  peaks -> clusters}   entry %lld / event %llu", best, evnum));
    txt->AddText("");
    for (size_t p = 0; p < n_peaks && p < pk_index->size() && p < 8; ++p)
        txt->AddText(Form("peak %zu  plane %-8s strip %-4u t=%u  height %.0f  width %u  area %.0f",
                          p, pk_plane && p < pk_plane->size() ? (*pk_plane)[p].c_str() : "?",
                          (*pk_index)[p], (*pk_time)[p], (*pk_height)[p], (*pk_width)[p], (*pk_area)[p]));
    txt->AddText("");
    if (n_clusters == 0)
        txt->AddText("no X/Y cluster in this event (needs a peak in BOTH planes)");
    for (size_t k = 0; k < n_clusters && k < 4; ++k)
        txt->AddText(Form("cluster %zu  x=%.2f  y=%.2f  energy=%.1f  adc=%.1f",
                          k, (*cl_x)[k], (*cl_y)[k], (*cl_e)[k], (*cl_adc)[k]));
    txt->Draw();

    TString png1 = TString::Format("%s/chain_event.png", outdir);
    c1.SaveAs(png1);

    // =========================================================================
    //  FIGURE 2 - populations over all events
    // =========================================================================
    TH1D h_pk_h ("h_pk_h",  "Peak height;height [ADC];peaks", 100, 0, 800);
    TH1D h_pk_w ("h_pk_w",  "Peak width (note bimodality: real ~6, artifacts ~128);width [strips];peaks", 70, 0, 280);
    TH1D h_pk_a ("h_pk_a",  "Peak area;area [ADC];peaks", 100, 0, 60000);
    TH1D h_pk_p ("h_pk_p",  "Peak position;real_pos [mm];peaks", 100, -60, 60);
    TH1D h_pk_n ("h_pk_n",  "Peaks per event;peaks;events", 12, -0.5, 11.5);
    TH1D h_pk_t ("h_pk_t",  "Peak time bin;time_id;peaks", 8, -0.5, 7.5);
    TH1D h_amp  ("h_amp",   "Plane amplitude (all strips);amplitude [ADC];strip-samples", 120, -200, 1000);
    TH2D h_cl   ("h_cl",    "Clusters X vs Y;x [mm];y [mm]", 60, -60, 60, 60, -60, 60);
    TH1D h_cl_e ("h_cl_e",  "Cluster energy;energy;clusters", 60, 0, 6000);
    TH1D h_pk_i ("h_pk_i",  "Peak strip index (>=128 = unmapped flat region);strip;peaks", 128, 0, 256);

    long tot_pk = 0, tot_cl = 0;
    for (Long64_t e = 0; e < n_entries; ++e) {
        tree->GetEntry(e);
        h_pk_n.Fill(n_peaks); tot_pk += n_peaks; tot_cl += n_clusters;
        for (size_t p = 0; p < n_peaks && p < pk_index->size(); ++p) {
            h_pk_h.Fill((*pk_height)[p]); h_pk_w.Fill((*pk_width)[p]);
            h_pk_a.Fill((*pk_area)[p]);   h_pk_p.Fill((*pk_pos)[p]);
            h_pk_t.Fill((*pk_time)[p]);
            h_pk_i.Fill((*pk_index)[p]);
        }
        for (size_t k = 0; k < n_clusters && k < cl_x->size(); ++k) {
            h_cl.Fill((*cl_x)[k], (*cl_y)[k]); h_cl_e.Fill((*cl_e)[k]);
        }
        if (pre_x) for (double v : *pre_x) h_amp.Fill(v);
    }

    TCanvas c2("c2", "GEM chain - populations", 1900, 1150);
    c2.Divide(3, 3);
    auto drawh = [&](int pad, TH1* h, bool logy) {
        c2.cd(pad); gPad->SetGrid(); if (logy) gPad->SetLogy();
        h->SetLineColor(kAzure + 2); h->SetLineWidth(2); h->SetFillColorAlpha(kAzure + 2, 0.25);
        h->DrawCopy("HIST");
    };
    drawh(1, &h_amp,  true);
    drawh(2, &h_pk_h, false);
    drawh(3, &h_pk_a, false);
    drawh(4, &h_pk_w, false);
    drawh(5, &h_pk_p, false);
    drawh(6, &h_pk_t, false);
    drawh(7, &h_pk_n, true);
    c2.cd(8); gPad->SetGrid(); h_cl.DrawCopy("COLZ");
    drawh(9, &h_pk_i, false);

    TString png2 = TString::Format("%s/chain_population.png", outdir);
    c2.SaveAs(png2);

    printf("\n=== populations over %lld events ===\n", n_entries);
    printf("  peaks    : %ld total (%.3f/event), height mean %.0f, width mean %.1f, area mean %.0f\n",
           tot_pk, double(tot_pk) / n_entries, h_pk_h.GetMean(), h_pk_w.GetMean(), h_pk_a.GetMean());
    printf("  clusters : %ld total (%.4f/event)\n", tot_cl, double(tot_cl) / n_entries);
    printf("  plane amplitude: mean %.2f rms %.2f\n", h_amp.GetMean(), h_amp.GetRMS());
    printf("\n  -> %s\n  -> %s\n\n", png1.Data(), png2.Data());
}
