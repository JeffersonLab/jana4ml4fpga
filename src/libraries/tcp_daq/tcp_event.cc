#include "tcp_event.h"
#include "tcp_thread.h"

static char HOST[128];
static int PORT = 20249;
static int TCP_FLAG;
static int current_sd = 0;
static int HOLD_TIME = 0; //--- assume only one sender (DC) !!!

//-------------------------------------------------------------------------------------------------------
int tcp_event_host(char *host, int port, bool report) {
    strncpy(HOST, host, 128);
    PORT = port;
    if (report) printf("==>> jana_tcp:: set HOST=%s PORT=%d \n", HOST, PORT);
    return 0;
}

//-------------------------------------------------------------------------------------------------------
int tcp_event_get_sd() {
    printf("==>> jana_tcp:: set HOST=%s PORT=%d SD=%d \n", HOST, PORT, current_sd);
    return current_sd;
}

//-------------------------------------------------------------------------------------------------------
int tcp_event_hold(int htime) {
    printf("==>> jana_tcp:: HOLD TIME = %d new=%d\n", HOLD_TIME, htime);
    if (HOLD_TIME == 0 || htime == 0) HOLD_TIME = htime;
    return HOLD_TIME;
}

//-------------------------------------------------------------------------------------------------------
int tcp_get3(int sock, int *DATA, int lenDATA) {
    int nleft, nread = 0;
    char *rcv;
    nleft = lenDATA;
    rcv = (char *) DATA;
    while (nleft > 0) {
//      if(!sig_hup) printf("try to receive = %d of %d\n",nleft,lenDATA);
        nread = recv(sock, rcv, nleft, 0);
        if (nread <= 0) {
            perror("tcp_get3():recv");
            return -1;
        }
        nleft -= nread;
//        if(!sig_hup)  printf("nread= %d ==== nleft=%d\n",nread,nleft);
        rcv += nread;
    }
    return nleft;
}

//---------------------------------------------------------------------------------------------------------------
//                      S E N D  
//---------------------------------------------------------------------------------------------------------------
int tcp_event_snd(unsigned int *DATA, int lenDATA, int n, int k, unsigned int evtHDR, unsigned int TriggerID) {
    static char hostname[100];
    static int sd;
    static struct sockaddr_in pin;
    struct hostent *hp;
    static int TCP_FLAG, HEADER[10];
    int nleft, nsent;
    static unsigned int time0, time1;
    char *snd;

// 0=initial -1 -no connection -2 error send 
    if (PORT == 0) {  //-- set HOST and PORT to default: localhost:20249
        strncpy(HOST, "localhost", 128);
        PORT = 20249;
        printf("==> jana_tcp:: set DEFAULT!! HOST=%s PORT=%d \n", HOST, PORT);
    }
    if (TCP_FLAG == 0) {

        TCP_FLAG = -2;
        time((time_t *) &time0);
        strcpy(hostname, HOST);
        printf("jana_tcp:: go find out about the desired host machine \n");

        sd = tcp_open_th(-PORT, HOST); // connect to remote port

        if (sd >= 0)
            printf("jana_tcp:: CONNECTED to %s  port=%d  local_sock=%d port=%d\n", hostname, PORT, sd,
                   ntohs(pin.sin_port));

        TCP_FLAG = 1;  //---  server OK, send DATA
    } else if (TCP_FLAG == -1) { //--- no server (error connect)

        time((time_t *) &time1);  //---- timer for retry

        printf("jana_tcp:: t1=%d  t0=%d t1-t0=%d \n", time1, time0, time1 - time0);
        if ((time1 - time0) > T_WAIT) {
            time0 = time1;
            printf("jana_tcp:: %d RE-try to connect to %s\n", time1 - time0, hostname);
            TCP_FLAG = 0;
        };
        return -1;
    } else if (TCP_FLAG == -2) { //--- server disappeared  (error send)
        printf("jana_tcp:: close socket... %s\n", hostname);
        close(sd);
        time((time_t *) &time0);
        TCP_FLAG = -1;
        return -1;
    };

    //--------------------------------------------------
    //------------   ENDIF TCP_FLAG  -------------------
    //--------------------------------------------------

    HEADER[0] = 0x5;  //---  buffered for evb
    HEADER[1] = 0xAABBCCDD;
    HEADER[2] = lenDATA;
    HEADER[3] = evtHDR;
    HEADER[4] = TriggerID;
    HEADER[5] = n;
    HEADER[6] = k;
    HEADER[7] = k;

#ifdef JF_DEBUG
    printf("send HEADER size=%d\n",sizeof(HEADER));
#endif

    if (send(sd, (char *) HEADER, sizeof(HEADER), 0) == -1) {
        perror("send");
        TCP_FLAG = -2;
        return -1;
    }
#ifdef JF_DEBUG
    printf("send DATA lenDATA=%d bytes (%d words)  nmod=%d mod=%d trigger=%d\n",lenDATA*4,lenDATA,n,k, TriggerID);
#endif

    nleft = lenDATA * 4;
    snd = (char *) DATA;
    while (nleft > 0) {
        if (DEBUG > 3) printf("try to send = %d of %d\n", PACKSIZE, nleft);
        if (nleft < PACKSIZE) nsent = send(sd, snd, nleft, 0);
        else nsent = send(sd, snd, PACKSIZE, 0);
        //printf("sent DATA\n");
        if (nsent <= 0) {
            perror("send");
            TCP_FLAG = -2;
            return -1;
        }
        nleft -= nsent;
        snd += nsent;
    }
    return nleft;
}


//---------------------------------------------------------------------------------------------------------------
//                      G E T 
//---------------------------------------------------------------------------------------------------------------
int
tcp_event_get(char *HOST2, unsigned int *DATA, int *lenDATA, int *Nr_Modules, int *ModuleID, unsigned int *TriggerID) {
    static char hostname[100];
    static int sd;
    static struct sockaddr_in pin;
    struct hostent *hp;
    static int TCP_FLAG, HEADER[10];
    int nleft, nread;
    static unsigned int time0, time1;
    char *rcv;
    static unsigned int MARKER, REQUEST, REQUESTED;
    int evtTrigID, evtModID, evtSize;
#define LINFO 128
    char host_name[LINFO];
    int sd_current = 0;
    static int sd_bind;

    // 0=initial -1 -no connection -2 error send
    if (TCP_FLAG == 0) {

        TCP_FLAG = -2;
        time((time_t *) &time0);

        printf("1: bind: port %d  \n", PORT);
        sd_bind = tcp_open_th(PORT, NULL); // bind local port

        printf("1: listen3: port %d  \n", sd_bind);
        int rem_port = tcp_listen3(sd_bind, host_name, LINFO, &sd);
        printf(" return from listen3 , blin, ADD NEW Client sem wait ... remport =%d \n", rem_port);

        printf("jana_tcp:: CONNECTED to %s  port=%d  local_sock=%d\n", hostname, PORT, sd);
        TCP_FLAG = 1;  //---  server OK, send DATA
    } else if (TCP_FLAG == -1) { //--- no server (error connect)
        time((time_t *) &time1);  //---- timer for retry

        printf("2: listen3: port %d  \n", 3);
        int rem_port = tcp_listen3(sd_bind, host_name, LINFO, &sd);
        printf(" return from listen3 , blin, ADD NEW Client sem wait ... remport =%d \n", rem_port);
        TCP_FLAG = 1;
        /*
          printf("jana_tcp:: t1=%d  t0=%d t1-t0=%d wait %d sec...\n",time1,time0,time1-time0,T_WAIT-(time1-time0));
          if ((time1-time0) > T_WAIT ) { time0=time1;
          printf("jana_tcp:: %d RE-try to connect to %s\n",time1-time0,hostname);
          TCP_FLAG=0;
          };
          return -1;
        */
    } else if (TCP_FLAG == -2) { //--- server disappeared  (error send)
        printf("jana_tcp:: close socket... %s bind sd = %d \n", hostname, sd_bind);
        close(sd);
        printf("jana_tcp:: closed socket... %s bind sd = %d \n", hostname, sd_bind);
        time((time_t *) &time0);
        REQUESTED = 0;
        TCP_FLAG = -1;
        return -1;
    };

    //--------------------------------------------------
    //------------   ENDIF TCP_FLAG  -------------------
    //--------------------------------------------------

    REQUEST = *Nr_Modules;
    //printf("REQUEST=%#X(%d)  REQUESTED=%d\n",REQUEST,REQUEST,REQUESTED);

    if (REQUESTED == 0 && REQUEST > 0x1000) {
        if (REQUEST == 0x1002) {
            HEADER[0] = 0x2;  //-- request for 1 event --
            REQUESTED = 0;
        } else if (REQUEST == 0x1003) {
            HEADER[0] = 0x3;  //-- request continuous events --
            REQUESTED = 1;
        } else if (REQUEST == 0x1013) {
            HEADER[0] = 0x13;  //-- request continuous exclusive events --
            REQUESTED = 1;
        } else {
            HEADER[0] = 0x2;  //-- request for 1 event default --
            REQUESTED = 0;
        }

        HEADER[1] = 0xAABBCCDD;
        HEADER[2] = *lenDATA;
        HEADER[3] = 0;
        HEADER[4] = 0;
        HEADER[5] = *Nr_Modules;
        HEADER[6] = *ModuleID;

        //printf(" tcp_event_get(): ask DATA max lenDATA=%d\n",*lenDATA);

        if (send(sd, (char *) HEADER, sizeof(HEADER), 0) == -1) {
            perror("send");
            TCP_FLAG = -2;
            return -1;
        }
    }
    //-----------------------------------------------------

    //-- recv HEADER ---
    //printf(" tcp_event_get(): wait for HEADER \n");

    nread = 0;
    nleft = sizeof(HEADER);
    rcv = (char *) HEADER;
    while (nleft > 0) {
        nread = recv(sd, rcv, nleft, 0);
        if (nread <= 0) {
            perror("recv_header");
            TCP_FLAG = -2;
            return -1;
        }
        nleft -= nread;
        rcv += nread;
    }
    MARKER = HEADER[1];
    *lenDATA = HEADER[2];
    *TriggerID = HEADER[4];
    *Nr_Modules = HEADER[5];
    *ModuleID = HEADER[6];

    //printf("HEADER OK=%#X, Nr_MOD=%d, ModID=%d, lenDATA=%d\n",MARKER,*Nr_Modules,*ModuleID,*lenDATA);

    //-- recv DATA ---
    //printf(" tcp_event_get(): wait for DATA \n");

    nleft = *lenDATA * 4;
    rcv = (char *) DATA;
    while (nleft > 0) {
        nread = recv(sd, rcv, nleft, 0);
        if (nread <= 0) {
            perror("recv_data");
            TCP_FLAG = -2;
            return -1;
        }
        nleft -= nread;
        rcv += nread;
        //printf("DATA recv=%d  nleft=%d\n",nread,nleft);
    }

    evtTrigID = DATA[1];
    evtModID = (DATA[0] >> 24) & 0xf;
    evtSize = DATA[0] & 0xfffff;

    printf("OK, TCP recv=%d  nleft=%d Trg=%d(%d) Mod=%d siz=%d\n",nread,nleft,evtTrigID,*TriggerID,evtModID,evtSize);

    return 0;
}
//============================================================================


