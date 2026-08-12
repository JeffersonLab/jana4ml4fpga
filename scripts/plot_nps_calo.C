// NPS 3x3 PbWO4 calorimeter beam-test plots from the flat events tree
// (FADC250 triggered mode; blocks on roc 65, slot 19, channels 0-8).
//
//   root -l -b -q 'plot_nps_calo.C("full_1029.root", "ai_rework/plots/nps_calo")'
//
// Produces:
//   calo_overview.png - per-channel pulse-height spectra, occupancy, energy sum,
//                       multiplicity, channel-channel correlation matrix
//   calo_events.png   - event displays: 3x3 energy maps + the 9 waveforms for
//                       the highest-energy events
//
// Channel -> position: ch = 3*row + col (row 0 at the bottom of the display).
// This is an ASSUMPTION consistent with the data - the correlation matrix is
// printed so it can be cross-checked: physical neighbours share shower energy,
// so adjacent channels must co-fire more than distant ones.
#include <vector>
#include <cstdio>

static const int kRoc = 65, kSlot = 19, kNCh = 9;

void plot_nps_calo(const char* filename,
                   const char* outdir = "ai_rework/plots/nps_calo",
                   int n_displays = 4) {
    gStyle->SetOptStat(0);
    gStyle->SetPalette(kBird);
    gSystem->mkdir(outdir, kTRUE);

    TFile file(filename);
    if (file.IsZombie()) { printf("Cannot open %s\n", filename); return; }
    TTree* tree = (TTree*)file.Get("events");
    if (!tree) { printf("No 'events' tree in %s\n", filename); return; }

    // NOTE: every f250_pulse_* quantity is stored as vector<uint32_t> in the
    // tree (see F250FDCPulseRecord.h) - reading them into vector<double> would
    // silently mis-read. Cast to double only in the arithmetic.
    std::vector<uint32_t> *p_roc = 0, *p_slot = 0, *p_ch = 0;
    std::vector<uint32_t> *p_peak = 0, *p_integral = 0, *p_ped = 0;
    std::vector<uint32_t> *p_nsi = 0, *p_nsp = 0;
    std::vector<uint32_t> *w_roc = 0, *w_slot = 0, *w_ch = 0, *w_sidx = 0, *w_scnt = 0;
    std::vector<uint16_t> *w_samples = 0;
    ULong64_t evnum = 0;
    tree->SetBranchAddress("f250_pulse_roc", &p_roc);
    tree->SetBranchAddress("f250_pulse_slot", &p_slot);
    tree->SetBranchAddress("f250_pulse_channel", &p_ch);
    tree->SetBranchAddress("f250_pulse_pulse_peak", &p_peak);
    tree->SetBranchAddress("f250_pulse_integral", &p_integral);
    tree->SetBranchAddress("f250_pulse_pedestal", &p_ped);
    tree->SetBranchAddress("f250_pulse_nsamples_integral", &p_nsi);
    tree->SetBranchAddress("f250_pulse_nsamples_pedestal", &p_nsp);
    tree->SetBranchAddress("f250_wraw_roc", &w_roc);
    tree->SetBranchAddress("f250_wraw_slot", &w_slot);
    tree->SetBranchAddress("f250_wraw_channel", &w_ch);
    tree->SetBranchAddress("f250_wraw_samples_index", &w_sidx);
    tree->SetBranchAddress("f250_wraw_samples_count", &w_scnt);
    tree->SetBranchAddress("f250_wraw_samples", &w_samples);
    tree->SetBranchAddress("event_number", &evnum);

    // ---- histograms ---------------------------------------------------------
    TH1D* h_spec[kNCh];
    for (int c = 0; c < kNCh; ++c)
        h_spec[c] = new TH1D(Form("h_spec_%d", c),
                             Form("ch %d (row %d, col %d);integral - pedestal [ADC];pulses", c, c / 3, c % 3),
                             100, 0, 4000);
    TH2D h_occ("h_occ", "Occupancy;col;row", 3, -0.5, 2.5, 3, -0.5, 2.5);
    TH2D h_esh("h_esh", "Energy share;col;row", 3, -0.5, 2.5, 3, -0.5, 2.5);
    TH1D h_sum("h_sum", "Event energy sum (all 9 blocks);#Sigma (integral - pedestal) [ADC];events", 100, 0, 10000);
    TH1D h_mult("h_mult", "Blocks over threshold per event;blocks;events", 10, -0.5, 9.5);
    TH2D h_corr("h_corr", "Channel co-firing (pulse in i AND j);channel;channel",
                kNCh, -0.5, 8.5, kNCh, -0.5, 8.5);
    TH2D h_cog("h_cog", "Shower centre of gravity;col COG;row COG", 60, -0.5, 2.5, 60, -0.5, 2.5);

    // event displays: keep the n_displays highest-energy events
    struct EvDisp { double sum; Long64_t entry; ULong64_t evnum; double e[kNCh]; };
    std::vector<EvDisp> best;

    const Long64_t n_entries = tree->GetEntries();
    long ev_fadc = 0;
    for (Long64_t e = 0; e < n_entries; ++e) {
        tree->GetEntry(e);
        if (!p_roc || p_roc->empty()) continue;
        double ech[kNCh] = {0};
        int fired[kNCh] = {0};
        for (size_t i = 0; i < p_roc->size(); ++i) {
            if ((*p_roc)[i] != kRoc || (*p_slot)[i] != kSlot) continue;
            int c = (*p_ch)[i];
            if (c < 0 || c >= kNCh) continue;
            // Pedestal-subtracted energy: the raw integral is dominated by the
            // ~100 ADC baseline x NSA+NSB samples; without subtraction every
            // fired block shows ~1000 regardless of signal.
            double integral = (double)(*p_integral)[i];
            if (p_ped && p_nsi && p_nsp && i < p_ped->size() && (*p_nsp)[i] > 0)
                integral -= (double)(*p_ped)[i] * (*p_nsi)[i] / (double)(*p_nsp)[i];
            ech[c] += integral;
            fired[c] = 1;
            h_spec[c]->Fill(integral);
        }
        double sum = 0; int mult = 0; double cx = 0, cy = 0;
        for (int c = 0; c < kNCh; ++c) {
            sum += ech[c]; mult += fired[c];
            h_occ.Fill(c % 3, c / 3, fired[c]);
            h_esh.Fill(c % 3, c / 3, ech[c]);
            for (int d = 0; d < kNCh; ++d)
                if (fired[c] && fired[d]) h_corr.Fill(c, d);
            cx += ech[c] * (c % 3); cy += ech[c] * (c / 3);
        }
        if (mult == 0) continue;
        ev_fadc++;
        h_sum.Fill(sum); h_mult.Fill(mult);
        if (sum > 0) h_cog.Fill(cx / sum, cy / sum);

        if ((int)best.size() < n_displays || sum > best.back().sum) {
            EvDisp d; d.sum = sum; d.entry = e; d.evnum = evnum;
            for (int c = 0; c < kNCh; ++c) d.e[c] = ech[c];
            best.push_back(d);
            std::sort(best.begin(), best.end(),
                      [](const EvDisp& a, const EvDisp& b) { return a.sum > b.sum; });
            if ((int)best.size() > n_displays) best.pop_back();
        }
    }

    printf("\n=== %s: %lld entries, %ld with FADC pulses ===\n", filename, n_entries, ev_fadc);
    printf("occupancy (row-major, row2 top):\n");
    for (int r = 2; r >= 0; --r)
        printf("   %6.0f %6.0f %6.0f\n", h_occ.GetBinContent(1, r + 1),
               h_occ.GetBinContent(2, r + 1), h_occ.GetBinContent(3, r + 1));
    printf("co-firing with centre ch4:  ");
    for (int c = 0; c < kNCh; ++c) if (c != 4)
        printf("ch%d:%.0f  ", c, h_corr.GetBinContent(c + 1, 5));
    printf("\n");

    // ---- overview canvas ----------------------------------------------------
    TCanvas c1("c1", "NPS calo overview", 1900, 1150);
    c1.Divide(3, 2);
    // 3x3 spectra inside pad 1
    c1.cd(1);
    auto* sub = new TPad("sub", "", 0, 0, 1, 1); sub->Draw(); sub->cd();
    sub->Divide(3, 3, 0.002, 0.002);
    for (int c = 0; c < kNCh; ++c) {
        // draw so pad layout matches physical layout: row 2 on top
        int row = c / 3, col = c % 3;
        sub->cd((2 - row) * 3 + col + 1);
        gPad->SetLogy();
        h_spec[c]->SetLineColor(kAzure + 2); h_spec[c]->Draw("HIST");
    }
    c1.cd(2); h_esh.Scale(1.0 / std::max(1.0, h_esh.Integral())); h_esh.Draw("COLZ TEXT");
    c1.cd(3); gPad->SetLogy(); h_sum.SetLineColor(kAzure + 2); h_sum.Draw("HIST");
    c1.cd(4); gPad->SetLogy(); h_mult.SetLineColor(kAzure + 2); h_mult.Draw("HIST");
    c1.cd(5); h_corr.Draw("COLZ TEXT");
    c1.cd(6); h_cog.Draw("COLZ");
    TString png1 = TString::Format("%s/calo_overview.png", outdir);
    c1.SaveAs(png1);

    // ---- event displays -----------------------------------------------------
    TCanvas c2("c2", "NPS calo events", 1900, 1150);
    c2.Divide(2, std::max(1, n_displays / 2) * 2 / 2);  // 2 columns
    c2.Clear(); c2.Divide(2, (n_displays + 1) / 2);
    int pad = 0;
    for (auto& d : best) {
        tree->GetEntry(d.entry);
        c2.cd(++pad);
        auto* p = new TPad(Form("pev%d", pad), "", 0, 0, 1, 1); p->Draw(); p->cd();
        p->Divide(2, 1, 0.001, 0.001);
        // left: 3x3 energy map
        p->cd(1);
        auto* hmap = new TH2D(Form("h_ev%d", pad),
                              Form("event %llu   #Sigma=%.0f ADC;col;row", d.evnum, d.sum),
                              3, -0.5, 2.5, 3, -0.5, 2.5);
        for (int c = 0; c < kNCh; ++c) hmap->Fill(c % 3, c / 3, d.e[c]);
        hmap->Draw("COLZ TEXT");
        // right: 9 waveforms
        p->cd(2); gPad->SetGrid();
        double wmax = 0;
        std::vector<TGraph*> graphs;
        for (size_t i = 0; w_roc && i < w_roc->size(); ++i) {
            if ((*w_roc)[i] != kRoc || (*w_slot)[i] != kSlot) continue;
            int ch = (*w_ch)[i];
            auto* g = new TGraph();
            for (uint32_t s = 0; s < (*w_scnt)[i]; ++s) {
                double v = (*w_samples)[(*w_sidx)[i] + s];
                g->SetPoint(s, s * 4.0, v);  // 4 ns / sample
                wmax = std::max(wmax, v);
            }
            g->SetLineColor(gStyle->GetColorPalette(ch * 255 / kNCh));
            g->SetLineWidth(2); g->SetTitle(Form("ch%d", ch));
            graphs.push_back(g);
        }
        for (size_t i = 0; i < graphs.size(); ++i) {
            if (i == 0) {
                graphs[i]->SetMaximum(wmax * 1.1);
                graphs[i]->SetTitle("waveforms;t [ns];ADC");
                graphs[i]->Draw("AL");
            } else graphs[i]->Draw("L SAME");
        }
    }
    TString png2 = TString::Format("%s/calo_events.png", outdir);
    c2.SaveAs(png2);
    printf("  -> %s\n  -> %s\n\n", png1.Data(), png2.Data());
}
