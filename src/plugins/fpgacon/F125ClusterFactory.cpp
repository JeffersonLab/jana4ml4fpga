//
// Created by Dmitry Romanov on 3/8/2024.
// Subject to the terms in the LICENSE file found in the top-level directory.
//

#include "F125ClusterFactory.h"

#include <TCanvas.h>
#include <TH2.h>
#define MAX_CLUST 500

namespace ml4fpga::fpgacon {

	// -- GEMTRD mapping --
	int GetGEMChan(int ch, int slot) {
		int cardNumber = ch / 24;
		int cardChannel = ch - cardNumber * 24;
		int invCardChannel = 23 - cardChannel;
		if (slot < 6 || (slot == 6 && ch < 24)) {
			return invCardChannel + cardNumber * 24 + (slot - 3) * 72.;
		}
		return -1;
	}
	// -- large GEMTRD mapping --
//
#define first_slot 3
#define gem_x_slot 0
#define gem_x_ch0 0

#define gem_y_slot 1
#define gem_y_ch0 48

#define last_slot 8
#define last_ch 71

	int GetGEMXChan(int fch, int slot)
{
  int sl=slot-first_slot;
  if((sl>=gem_x_slot&&sl<gem_y_slot)||(sl==gem_y_slot&&fch<gem_y_ch0)){
    int ch=sl*72+fch-gem_x_ch0;
    return ch;
  } else {
    return -1;
  }

}
int GetGEMYChan(int fch, int slot)
{
  int sl=slot-first_slot;
  if(sl>7)sl=sl-2;
  if((sl>gem_y_slot&&sl<last_slot)||(sl==gem_y_slot&&fch>=gem_y_ch0)||(sl==last_slot&&fch<=last_ch)){
    int ch=(sl-gem_y_slot)*72+fch-gem_y_ch0;
    return ch;
  } else {
    return -1;
  }

}

    void F125ClusterFactory::CozyInit() {

    		int nx0 = 100;
    		int ny0 = 528;

    		hevt = new TH2F("hevt", " Event display; z pos,mm; y pos,mm ", nx0, 0., +30., ny0, -50., +50.);
    		hevt->SetStats(false);
    		hevt->SetMaximum(10.);
    		// HistList->Add(hevt);		// TODO (commented)

    		hevtc = new TH2F("hevtc", " Clustering ; FADC bins; GEM strips", nx0, -0.5, nx0 - 0.5, ny0, -0.5, ny0 - 0.5);
    		hevtc->SetStats(false);
    		hevtc->SetMinimum(0.07);
    		hevtc->SetMaximum(40.);
    		// HistList->Add(hevtc);

    		hevti = new TH2F("hevti", " ML-FPGA response; z pos,mm; y pos,mm ", nx0, 0., +30., ny0, -50., +50.);
    		hevti->SetStats(false);
    		hevti->SetMaximum(10.);
    		//HistList->Add(hevti);		// TODO (commented)

    		hevtf = new TH2F("hevtf", " Clusters for FPGA ; z pos,mm; y pos,mm ", nx0, 0., +30., ny0, -50., +50.);
    		hevtf->SetStats(false);
    		hevtf->SetMaximum(10.);
    		// HistList->Add(hevtf);	// TODO (commented)


    }

    void F125ClusterFactory::CozyBeginRun(uint64_t run_number) {

    }

    void F125ClusterFactory::CozyProcess(uint64_t run_number, uint64_t event_number) {

		logger()->trace("Event #{}", event_number);
		if(event_number < 3) return;

    	std::vector<const Df125WindowRawData *> f125_values = m_f125_input();


    	// //-------------------------------------------------------------------------
    	// //-----------------  canvas 1 FPGA Display ----------
    	// char c2Title[256];
    	// sprintf(c2Title, "Event_Display_Run=%lu", event_number);
    	// auto* c2 = new TCanvas("FPGA", c2Title, 100, 100, 1000, 1300);
    	// c2->Divide(1, 3);
    	// c2->cd(1);



    	hevt->Reset();
    	hevtc->Reset();
    	hevti->Reset();
    	hevtf->Reset();


		// Detect electron or not
		bool electron = false;
		for (size_t i = 0; i < f125_values.size(); i++) {
			if (f125_values[i]->channel != 15) continue;		// We need this 15 channel
			// search max sample
			int adc_max = 0;
			for (int si = 0; si < f125_values[i]->samples.size(); si++) {
				const int adc = f125_values[i]->samples[si]; // printf(" sample=%d adc=%d \n",si,adc);
				if (adc > 4090) continue;		// It is a warning, but we'll warn about it later
				if (adc > adc_max) {
					adc_max = adc;
				}
			}
			if (adc_max > 300) electron = true;
		}



		// Is electron loop
    	for (size_t i = 0; i < f125_values.size(); i++) {
    		// --- fadc125 channels loop

    		auto f125_data = f125_values[i];
    		auto f125_slot = f125_data->slot;
    		auto f125_chan = f125_data->channel;
    		int f125_gem_chan = GetGEMYChan(f125_chan, f125_slot);


    		int amax = 0;
    		int tmax = 0;
    		if (f125_gem_chan < 0) continue;


    		for (int si = 0; si < f125_data->samples.size(); si++) {

    			int adc = f125_data->samples[si]; // printf(" sample=%d adc=%d \n",si,adc);
    			if (adc > 4090) {
    				logger()->warn("FADC125 overflow i={} slot={} chan={} value={}", i, f125_slot, f125_chan, adc);
    			}
    			if (adc > amax) {
    				amax = adc;
    				tmax = si;
    			}

    			if (adc > m_cfg_dedx_threshold()) {
    				auto time = si - m_cfg_time_window_start();
    				if ( 0 > time || time > 100 ) continue; // --- drop early and late hits ---
    				hevtc->SetBinContent(100-time,f125_gem_chan,adc/100.);
    				hevt->SetBinContent(100-time,f125_gem_chan,adc/100.);
    			}

    		} // --  end of samples loop

    	} // -- end of fadc125 channels loop

		// -------------------------------   hist dist clustering         ------------------------

    	float clust_Xpos[MAX_CLUST];
    	float clust_Ypos[MAX_CLUST];
    	float clust_Zpos[MAX_CLUST];
    	float clust_dEdx[MAX_CLUST];
    	float clust_Size[MAX_CLUST];
    	float clust_Width[MAX_CLUST][3]; // y1, y2, dy ; strips
    	float clust_Length[MAX_CLUST][3]; // x1, x2, dx ; time

    	for (int k = 0; k < MAX_CLUST; k++) {
    		clust_Xpos[k] = 0;
    		clust_Ypos[k] = 0;
    		clust_Zpos[k] = 0;
    		clust_dEdx[k] = 0;
    		clust_Size[k] = 0;
    		clust_Width[k][0] = 999999;
    		clust_Width[k][1] = -999999;
    		clust_Width[k][2] = 0;
    		clust_Length[k][0] = 999999;
    		clust_Length[k][1] = -999999;
    		clust_Length[k][2] = 0;
    	}
    	float CL_DIST = 2.7; // mm
    	int nclust = 0;

    	//hevti->Reset();
    	//for (int i=0; i<EVENT_SIZE; i++) {

    	TH2F* hp = hevt; // -- hevt and hevtc should be same bin size
    	TH2F* hpc = hevtc;

    	int nx = hp->GetNbinsX();
    	int ny = hp->GetNbinsY();
    	double xmi = hp->GetXaxis()->GetBinLowEdge(1);
    	double xma = hp->GetXaxis()->GetBinUpEdge(nx);
    	double ymi = hp->GetYaxis()->GetBinLowEdge(1);
    	double yma = hp->GetYaxis()->GetBinUpEdge(ny);
    	double binx = (xma - xmi) / nx;
    	double biny = (yma - ymi) / ny;
		logger()->trace("nx={}, ny={}, xmi={}, xma={}, ymi={}, yma={}", nx, ny, xmi, xma, ymi, yma);

    	double THR2 = 1.2;
    	for (int ix = 0; ix < nx; ix++) {
    		//-------------------- clustering loop ------------------------------------
    		for (int iy = 0; iy < ny; iy++) {
    			double c1 = hpc->GetBinContent(ix, iy); // hpc->SetBinContent(ix,iy,5.);         // energy
    			double x1 = double(ix) / double(nx) * (xma - xmi) + xmi - binx / 2.; // drift time
    			double y1 = double(iy) / double(ny) * (yma - ymi) + ymi - biny / 2.; // X strip

    			if (c1 < THR2) continue;
    			if (nclust == 0) {
    				clust_Xpos[nclust] = y1;
    				clust_Ypos[nclust] = 0;
    				clust_Zpos[nclust] = x1;
    				clust_dEdx[nclust] = c1;
    				clust_Size[nclust] = 1;
    				clust_Width[nclust][0] = y1;
    				clust_Width[nclust][1] = y1;
    				clust_Width[nclust][2] = 0;
    				clust_Length[nclust][0] = x1;
    				clust_Length[nclust][1] = x1;
    				clust_Length[nclust][2] = 0;
    				nclust++;
    				continue;
    			}

    			int added = 0;
    			for (int k = 0; k < nclust; k++) {
    				double dist = sqrt(pow((y1 - clust_Xpos[k]), 2.) + pow((x1 - clust_Zpos[k]), 2.));
    				//--- dist hit to clusters
    				if (dist < CL_DIST) {
    					clust_Xpos[k] = (y1 * c1 + clust_Xpos[k] * clust_dEdx[k]) / (c1 + clust_dEdx[k]); //--  new X pos
    					clust_Zpos[k] = (x1 * c1 + clust_Zpos[k] * clust_dEdx[k]) / (c1 + clust_dEdx[k]); //--  new Z pos
    					clust_dEdx[k] = c1 + clust_dEdx[k]; // new dEdx
    					clust_Size[k] = 1 + clust_Size[k]; // clust size in pixels
    					//if (k==9) printf("L:1: k=%d y1=%f min=%f max=%f \n",k,y1, clust_Width[k][0], clust_Width[k][1] );
    					if (y1 < clust_Width[k][0]) clust_Width[k][0] = y1;
    					if (y1 > clust_Width[k][1]) clust_Width[k][1] = y1;
    					clust_Width[k][2] = clust_Width[k][1] - clust_Width[k][0];
    					if (x1 < clust_Length[k][0]) clust_Length[k][0] = x1;
    					if (x1 > clust_Length[k][1]) clust_Length[k][1] = x1;
    					clust_Length[k][2] = clust_Length[k][1] - clust_Length[k][0];
    					//if (k==9) printf("L:2: k=%d y1=%f min=%f max=%f \n",k,y1, clust_Width[k][0], clust_Width[k][1] );
    					// Var(X_n) = Var(X_{n-1}) + \frac{(X_n - \bar{X_{n-1}})^2}{n}
    					hpc->SetBinContent(ix, iy, k + 1.);
    					added = 1;
    					break;
    				}
    			}
    			if (added == 0) {
    				if (nclust + 1 >= MAX_CLUST) continue;
    				clust_Xpos[nclust] = y1;
    				clust_Ypos[nclust] = 0;
    				clust_Zpos[nclust] = x1;
    				clust_dEdx[nclust] = c1;
    				clust_Size[nclust] = 1;
    				clust_Width[nclust][0] = y1;
    				clust_Width[nclust][1] = y1;
    				clust_Width[nclust][2] = 0;
    				clust_Length[nclust][0] = x1;
    				clust_Length[nclust][1] = x1;
    				clust_Length[nclust][2] = 0;
    				nclust++;
    			}
    		}
    	} //----------------------------------- end  clustering loop -----------------------------------------------

		for(int i=0; i < nclust; i++) {

			auto cluster = new F125Cluster();
			cluster->id = i;
			cluster->pos_x =     clust_Xpos[i];
			cluster->pos_y =     clust_Ypos[i];
			cluster->pos_z =     clust_Zpos[i];
			cluster->dedx =      clust_dEdx[i];
			cluster->size =      clust_Size[i];
			cluster->width[0] =  clust_Width[i][0];
			cluster->width[1] =  clust_Width[i][1];
			cluster->width[2] =  clust_Width[i][2];
			cluster->length[0] = clust_Length[i][0];
			cluster->length[1] = clust_Length[i][1];
			cluster->length[2] = clust_Length[i][2];
			m_cluster_output().push_back(cluster);
		}


		auto clust_context = new F125ClusterContext();
		clust_context->is_electron = electron;
		clust_context->hevt = hevt;
		clust_context->hevtc = hevtc;
		m_cluster_context_output().push_back(clust_context);


    }

    void F125ClusterFactory::CozyFinish() {

    }
}
