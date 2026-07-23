//
// Created by Dmitry Romanov on 3/8/2024.
// Subject to the terms in the LICENSE file found in the top-level directory.
//

#include "FpgaExchangeFactory.h"

#include <rawdataparser/Df125WindowRawData.h>
#include <JANA/JEvent.h>

#include <TSocket.h>
#include <TMarker.h>
#include <TCanvas.h>
#include <TH2.h>
#include <TF1.h>
#include <rawdataparser/Df125WindowRawData.h>

#include "F125Cluster.h"

#define USE_TRK
#define MAX_PRINT 1

#define USE_TCP 1
#define USE_FIT 1
#define MAX_NODES 100
#define BUFSIZE 128000

#define USE_125_RAW


namespace ml4fpga::fpgacon {
    void FpgaExchangeFactory::CozyInit() {
        std::string plugin_name = GetPluginName();
        //
        // // Get JANA application
        auto app = GetApplication();
        // InitLogger(plugin_name + ":fpga");
        //
        //
        bool use_fpga = false;
        app->SetDefaultParameter(plugin_name + ":use_fpga", use_fpga, "Send messages to FPGA via TCP");
        int port = m_cfg_port();
        const std::string&host = m_cfg_host();
        m_socket = std::make_unique<TSocket>(host.c_str(), port);
        m_socket->SetOption(kNoDelay,1);
        logger()->info("  kNoDelay \n");

    }


    void FpgaExchangeFactory::CozyProcess(uint64_t run_number, uint64_t event_number) {
        try {
            int verbose_event_len = 100;

            if (event_number < 3) return;
            TStopwatch event_sw;

            auto time_info = new FpgaExchangeTimeInfo();
#if (USE_TCP==1)
            //-----------------  send DATA  ----------------

            int DC_NROC = 4;
            static unsigned int BUFFER[BUFSIZE];
            float* FBUFFER = (float *)BUFFER;
            int mod_len = 1, hdr, itrg = 0;
            unsigned int EVT_Type_BOR = (0x0 & 0x3) << 22; //--  BOR=0x0
            unsigned int EVT_Type_EOR = (0x1 & 0x3) << 22; //--  EOR=0x1
            unsigned int EVT_Type_DATA = (0x2 & 0x3) << 22; //--  DATA=0x2
            static int HEADER[10];
            int modID = 4;

            int k = 4; //-- start data 4 words header;
            logger()->trace("-- BUFFER:: \n");

            auto&clusters = m_input_clusters();
            for (auto&cluster: clusters) {
            }

            for (int n = 0; n < std::min(clusters.size(), m_cfg_fpga_max_hits); n++) {
                auto cluster = clusters[n];

                // XPos to buffer
                FBUFFER[k++] = cluster->pos_x;
                logger()->trace("{} {:x} {}", (k - 1), BUFFER[k - 1], FBUFFER[k - 1]);

                // YPos to buffer
                FBUFFER[k++] = cluster->pos_z;
                logger()->trace("{} {:x} {}", (k - 1), BUFFER[k - 1], FBUFFER[k - 1]);

                // Trace cluster data
                logger()->trace("==> Cluster: id:{:>10} x:{:>10.2f} z:{:>10.2f} dedx:{:>10.2f}", cluster->id, cluster->pos_x, cluster->pos_z, cluster->dedx);
            }

            logger()->trace(" Filled bufer size={} , data = {} hits = {} \n", k, k - 4, (k - 4) / 2);

            int PAD = k % 2;

            itrg++;
            for (int mod_index = 0; mod_index < mod_len; mod_index++) {
                int LENEVENT = k + PAD;

                BUFFER[0] = DC_NROC; // DC_NROC if send to EVB, or
                BUFFER[0] |= EVT_Type_DATA;
                BUFFER[1] = itrg;
                BUFFER[2] = LENEVENT; //--
                BUFFER[3] = PAD; //--

                BUFFER[0] &= ~(0xff << 24); // --  clear ModID
                unsigned int MODIDx = ((modID + mod_index) & 0xff) << 24; //-- ModID=modID+i
                BUFFER[0] |= MODIDx;


                int evtModID = (BUFFER[0] >> 24) & 0xff;
                unsigned int evtTrigID = BUFFER[1];
                int evtSize = BUFFER[2];
                int evtPad = BUFFER[3];

                if (event_number < verbose_event_len) {
                    printf("==> SEND:: Trg=%d(%d,%d) Mod=%d(%d) siz=%d(%d), evtPad=%d\n"
                           , evtTrigID, itrg, BUFFER[1], evtModID, mod_index, evtSize, LENEVENT, evtPad);
                }


                unsigned int* DATA = BUFFER;
                int lenDATA = LENEVENT;
                int n = mod_len;
                int k = mod_index;
                unsigned int evtHDR = (mod_index & 0xF) << 24;
                unsigned int TriggerID = itrg;

                HEADER[0] = 0x5; //---  buffered for evb
                HEADER[1] = 0xAABBCCDD;
                HEADER[2] = lenDATA;
                HEADER[3] = evtHDR;
                HEADER[4] = TriggerID;
                HEADER[5] = n;
                HEADER[6] = k;
                HEADER[7] = k;

                TStopwatch send_sw;

                m_socket->SendRaw((char *)HEADER, sizeof(HEADER), kDefault);
                m_socket->SendRaw((char *)DATA, lenDATA * 4, kDefault);

                send_sw.Stop();
                time_info->send_cpu_time = send_sw.CpuTime();
                time_info->send_real_time = send_sw.RealTime();


                if (event_number < verbose_event_len)
                {
                    printf("read GNN out, wait for FPGA data ... \n"); //=======================================================
                }


                unsigned int NDATA[MAX_NODES + 10];
                int RHEADER[10];
                int COLMAP[] = {1, 2, 3, 4, 6, 5};

                TStopwatch receive1_sw;

                m_socket->RecvRaw((char *)RHEADER, sizeof(RHEADER), kDefault);
                int lenNODES = RHEADER[2];

                if (event_number < verbose_event_len)
                {
                    printf("RHEADER::");
                    for (int ih = 0; ih < 5; ih++) printf(" %d (0x%x) \n", RHEADER[ih], RHEADER[ih]);
                    printf(" LenDATA=%d \n", RHEADER[2]);
                }

                m_socket->RecvRaw((char *)NDATA, lenNODES * 4, kDefault);

                receive1_sw.Stop();
                time_info->receive1_cpu_time = receive1_sw.CpuTime();
                time_info->receive1_real_time = receive1_sw.RealTime();

                PAD = NDATA[3];
                size_t nnodes = lenNODES - 4 - PAD; //-- 4 is size of header
                if (event_number < verbose_event_len)
                {
                    printf("nodes return: %d (nclust=%d), TRKS: \n", nnodes, clusters.size());
                }


                unsigned int TDATA[2048];
                float* FTDATA = (float *)TDATA;
#if (USE_FIT==1)
                if (event_number < verbose_event_len)
                {
                    printf("read FIT out, wait for FPGA data ... \n");
                }

                TStopwatch receive2_sw;

                int receive_result = m_socket->RecvRaw((char *)RHEADER, sizeof(RHEADER), kDefault);
                logger()->debug("receive_result={}", receive_result);

                int lenFITS = RHEADER[2];

                if (event_number < verbose_event_len)
                {
                    printf("RHEADER::");
                    for (int ih = 0; ih < 5; ih++) printf(" %d \n", RHEADER[ih]);
                    printf(" LenDATA=%d \n", RHEADER[2]);
                }
                m_socket->RecvRaw((char *)TDATA, lenFITS * 4, kDefault);

                receive2_sw.Stop();
                time_info->receive2_cpu_time = receive2_sw.CpuTime();
                time_info->receive2_real_time = receive2_sw.RealTime();

                PAD = TDATA[3];
                int nfits = lenFITS - 4 - PAD;

                if (event_number < verbose_event_len)
                {
                    for (int i = 0; i < lenFITS; i++) {
                        printf("tracks fit return: i=%d  data=0x%x (%f)  \n", i, TDATA[i], FTDATA[i]);
                    }

                    printf("tracks fit return: %d  PAD=%d , TDATA[3]=%d \n", nfits, PAD, TDATA[3]);
                }

#else
     		int nfits=nnodes;
     		printf("tracks fit return: %d  \n", nfits);
#endif  // --- end  if USE_FIT  ---


#endif  // --- end  if USE_TCP  ---

                if (nfits > 0) {
                    //=============== Draw FPGA Clust =============
                    for (int clust_iter = 0; clust_iter < std::min(nnodes, clusters.size()); clust_iter++) {
                        auto hits_track_assoc = new FpgaHitsToTrack(); // Hits id to track id association
                        hits_track_assoc->hit_index = clust_iter;
                        hits_track_assoc->track_index = NDATA[clust_iter + 4];
                        m_output_hits_to_track().push_back(hits_track_assoc);
                    }

                    // for (int nd = 0; nd < std::min(nnodes, nhits); nd++) {
                    // 	int trknum = NDATA[nd + 4];
                    // 	printf(" %u, ", trknum);
                    // 	c2->cd(3);
                    // 	gPad->Modified();
                    // 	gPad->Update();
                    // 	if (trknum > 0) {
                    // 		//DrawPolyMarker (1, &hits_Xpos[nd], &hits_Zpos[n], Option_t *option="")
                    // 		//TMarker* m = new TMarker(hits_Xpos[nd],hits_Zpos[n],24);  // memory leak !!!!!
                    // 		TMarker m = TMarker(hits_Zpos[nd], hits_Xpos[nd], 24);
                    // 		int tcol = std::min(trknum, 6);
                    // 		int mcol = COLMAP[tcol - 1];
                    // 		m.SetMarkerColor(mcol);
                    // 		m.SetMarkerStyle(20);
                    // 		m.SetMarkerSize(1.5);
                    // 		m.DrawClone();
                    // 		gPad->Modified();
                    // 		gPad->Update();
                    // 		printf("=====>  Draw:%d X,Y,Z,E=%f %f %f %f \n", nd, hits_Xpos[nd], hits_Ypos[nd], hits_Zpos[nd], hits_dEdx[nd]);
                    // 	}
                    // }
                    // c2->Modified();
                    // c2->Update();
                    // printf("\n");


#if (USE_FIT==1)
                    //=============== Draw Tracks lines =============

                    int ntracks = nfits / 3;
                    int cs = nfits % 3;
                    if (cs != 0) {
                        if (event_number < verbose_event_len)
                        {
                            printf("==========>>>>   Error FIT results : %d %d %d \n", nfits, ntracks, cs);
                        }
                        break;
                    };

                    int cnt = 4; // word counter in data buffer
                    // c2->cd(3);
                    // gPad->Modified();
                    // gPad->Update();
                    for (int i = 0; i < ntracks; i++) {
                        auto track_fit = new FpgaTrackFit();
                        track_fit->track_id = TDATA[cnt++];
                        track_fit->slope = FTDATA[cnt++];
                        track_fit->intersect = FTDATA[cnt++];
                        m_output_trak_fit().push_back(track_fit);
                    }
                    // c2->Modified();
                    // c2->Update();
#endif  // --- end  if USE_FIT  ---
                }
                else {
                    if (event_number < verbose_event_len)
                    {
                        printf(" No tracks to draw \n");
                    }
                }

                // //=============== Draw All Clust ================
                // //----------------------  To FPGA ------------------
                // c2->cd(2);
                // gPad->Modified();
                // gPad->Update();
                // printf(" Draw clusters  \n");
                // for (int k = 0; k < nhits; k++) {
                // 	TMarker mh = TMarker(hits_Zpos[k], hits_Xpos[k], 24);
                // 	int mhcol = 1;
                // 	mh.SetMarkerColor(mhcol);
                // 	mh.SetMarkerSize(1.5);
                // 	mh.DrawClone();
                // 	gPad->Modified();
                // 	gPad->Update();
                // }
                // c2->Modified();
                // c2->Update();
                // //----------------------  from FPGA ------------------
                // c2->cd(3);
                // gPad->Modified();
                // gPad->Update();
                // printf(" Draw clusters  \n");
                // for (int k = 0; k < nhits; k++) {
                // 	TMarker mh = TMarker(hits_Zpos[k], hits_Xpos[k], 24);
                // 	int mhcol = 1;
                // 	mh.SetMarkerColor(mhcol);
                // 	mh.SetMarkerSize(1.5);
                // 	//c2->cd(2); mh.DrawClone();  c2->Modified(); c2->Update();
                // 	mh.DrawClone();
                // 	gPad->Modified();
                // 	gPad->Update();
                // }
                // c2->Modified();
                // c2->Update();
            } // -- end mod  ---
            //printf(" all done, click low right pad ...  \n");
            //c2->cd(2); gPad->WaitPrimitive();
            //if (jentry<35) sleep(5); else sleep(1);


            //-------------------------------------------------------
            event_sw.Stop();
            time_info->event_cpu_time = event_sw.CpuTime();
            time_info->event_real_time = event_sw.RealTime();

            if(event_number < 10)
            {
                logger()->info("Exchange time info:");
                logger()->info("   event_cpu_time     [us]:  {}", time_info->event_cpu_time*1000000);
                logger()->info("   event_real_time    [us]:  {}", time_info->event_real_time*1000000);
                logger()->info("   send_cpu_time      [us]:  {}", time_info->send_cpu_time*1000000);
                logger()->info("   send_real_time     [us]:  {}", time_info->send_real_time*1000000);
                logger()->info("   receive1_cpu_time  [us]:  {}", time_info->receive1_cpu_time*1000000);
                logger()->info("   receive1_real_time [us]:  {}", time_info->receive1_real_time*1000000);
                logger()->info("   receive2_cpu_time  [us]:  {}", time_info->receive2_cpu_time*1000000);
                logger()->info("   receive2_real_time [us]:  {}", time_info->receive2_real_time*1000000);
            }
            m_output_timing().push_back(time_info);
        }
        catch (std::exception&e) {
            logger()->warn("FpgaExchangeFactory Exception during process {}", e.what());
            //m_logger->warn("Exception during process {}", e.what());
            //throw JException();
        }
    }


    //=======================  End Fa125 RAW  process Loop  =====================================================
}
