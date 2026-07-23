#include <filesystem>
#include <JANA/JException.h>
#include "GemMapping.h"

GemMapping *GemMapping::instance = 0;
using namespace std;

//======================================================================================================================================
void GemMapping::SetCartesianStripsReadoutMap(std::string readoutBoard, std::string detectorType, std::string detector, int detID,
                                              std::string planeX, float sizeX, int connectorsX, int orientX,
                                              std::string planeY, float sizeY, int connectorsY, int orientY) {

    printf("   GemMapping::SetCartesianStripsReadoutMap() => readout=%s, detType=%s, det=%s, detID=%d, planeX=%s, SizeX=%f, connectorsX=%d, planeY=%s, SizeY=%f, connectorsY=%d \n",
           readoutBoard.c_str(), detectorType.c_str(), detector.c_str(), detID, planeX.c_str(), sizeX, connectorsX,
           planeY.c_str(), sizeY, connectorsY);

    fDetectorFromIDMap[detID] = detector;
    fReadoutBoardFromIDMap[detID] = readoutBoard;

    fDetectorIDFromDetectorMap[detector] = detID;
    fReadoutBoardFromDetectorMap[detector] = readoutBoard;
    fDetectorTypeFromDetectorMap[detector] = detectorType;

    fDetectorListFromDetectorTypeMap[detectorType].push_back(detector);
    fDetectorListFromReadoutBoardMap[readoutBoard].push_back(detector);

    fPlaneIDFromPlaneMap[planeX] = 0;
    fPlaneIDFromPlaneMap[planeY] = 1;
    fPlaneFromPlaneIDMap[0] = planeX;
    fPlaneFromPlaneIDMap[1] = planeY;

    fDetectorFromPlaneMap[planeX] = detector;
    fDetectorFromPlaneMap[planeY] = detector;

    fPlaneListFromDetectorMap[detector].push_back(planeX);
    fPlaneListFromDetectorMap[detector].push_back(planeY);

    fCartesianPlaneMap[planeX].push_back(0);
    fCartesianPlaneMap[planeX].push_back(sizeX);
    fCartesianPlaneMap[planeX].push_back(connectorsX);
    fCartesianPlaneMap[planeX].push_back(orientX);

    fCartesianPlaneMap[planeY].push_back(1);
    fCartesianPlaneMap[planeY].push_back(sizeY);
    fCartesianPlaneMap[planeY].push_back(connectorsY);
    fCartesianPlaneMap[planeY].push_back(orientY);
}

//======================================================================================================================================
void GemMapping::SetUVStripsReadoutMap(std::string readoutBoard, std::string detectorType, std::string detector, int detID,
                                       float length, float innerR, float outerR, std::string planeTop,
                                       int connectTop, int orientTop, std::string planeBot, int connectBot,
                                       int orientBot) {

    printf("   GemMapping::SetUVStripsReadoutMap() => readout=%s, detType=%s, det=%s,  planeTop=%s, nbAPV_Top=%d, planeBot=%s, nbAPV_Bot=%d, Length=%f, innerR=%f, outerR=%f \n",
           readoutBoard.c_str(), detectorType.c_str(), detector.c_str(), planeTop.c_str(), connectTop, planeBot.c_str(),
           connectBot, length, innerR, outerR);

    fDetectorFromIDMap[detID] = detector;
    fReadoutBoardFromIDMap[detID] = readoutBoard;

    fDetectorIDFromDetectorMap[detector] = detID;
    fReadoutBoardFromDetectorMap[detector] = readoutBoard;
    fDetectorTypeFromDetectorMap[detector] = detectorType;

    fDetectorListFromDetectorTypeMap[detectorType].push_back(detector);
    fDetectorListFromReadoutBoardMap[readoutBoard].push_back(detector);

    fPlaneIDFromPlaneMap[planeTop] = 0;
    fPlaneIDFromPlaneMap[planeBot] = 1;
    fPlaneFromPlaneIDMap[0] = planeTop;
    fPlaneFromPlaneIDMap[1] = planeBot;

    fDetectorFromPlaneMap[planeTop] = detector;
    fDetectorFromPlaneMap[planeBot] = detector;

    fPlaneListFromDetectorMap[detector].push_back(planeTop);
    fPlaneListFromDetectorMap[detector].push_back(planeBot);

    fCartesianPlaneMap[planeTop].push_back(0);
    fCartesianPlaneMap[planeBot].push_back(1);

    fUVangleReadoutMap[detector].push_back(length);
    fUVangleReadoutMap[detector].push_back(innerR);
    fUVangleReadoutMap[detector].push_back(outerR);

    fUVangleReadoutMap[planeTop].push_back(0);
    fUVangleReadoutMap[planeTop].push_back(connectTop);
    fUVangleReadoutMap[planeTop].push_back(orientTop);
    fUVangleReadoutMap[planeTop].push_back(length);

    fUVangleReadoutMap[planeBot].push_back(1);
    fUVangleReadoutMap[planeBot].push_back(connectBot);
    fUVangleReadoutMap[planeBot].push_back(orientBot);
    fUVangleReadoutMap[planeBot].push_back(outerR);
}

//======================================================================================================================================
void GemMapping::Set1DStripsReadoutMap(std::string readoutBoard, std::string detectorType, std::string detector, int detID,
                                       std::string plane, float size, int connectors, int orient) {

    printf("   GemMapping::SetDetectorMap() => readout=%s, detType=%s, det=%s, detID=%d, plane=%s, Size=%f, connectors=%d, orientation=%d \n",
           readoutBoard.c_str(), detectorType.c_str(), detector.c_str(), detID, plane.c_str(), size, connectors, orient);

    fDetectorFromIDMap[detID] = detector;
    fReadoutBoardFromIDMap[detID] = readoutBoard;

    fDetectorIDFromDetectorMap[detector] = detID;
    fReadoutBoardFromDetectorMap[detector] = readoutBoard;
    fDetectorTypeFromDetectorMap[detector] = detectorType;

    fDetectorListFromDetectorTypeMap[detectorType].push_back(detector);
    fDetectorListFromReadoutBoardMap[readoutBoard].push_back(detector);

    fPlaneIDFromPlaneMap[plane] = 0;
    fPlaneFromPlaneIDMap[0] = plane;

    fDetectorFromPlaneMap[plane] = detector;
    fPlaneListFromDetectorMap[detector].push_back(plane);

    f1DStripsPlaneMap[plane].push_back(0);
    f1DStripsPlaneMap[plane].push_back(size);
    f1DStripsPlaneMap[plane].push_back(connectors);
    f1DStripsPlaneMap[plane].push_back(orient);
}

//======================================================================================================================================
void GemMapping::SetCMSGEMReadoutMap(std::string readoutBoard, std::string detectorType, std::string detector, int detID,
                                     std::string EtaSector, float etaSectorPos, float etaSectorSize,
                                     float nbOfSectorConnectors, int apvOrientOnEtaSector) {
    printf("   GemMapping::SetDetectorMap() =>readout=%s, detType=%s, det=%s, detID=%d, EtaSector=%s, etaSectorSize=%f, nbOSectorfConnectors=%f, apvOrientOnEtaSector=%d \n",
           readoutBoard.c_str(), detectorType.c_str(), detector.c_str(), detID, EtaSector.c_str(), etaSectorSize,
           nbOfSectorConnectors, apvOrientOnEtaSector);

    fDetectorFromIDMap[detID] = detector;
    fReadoutBoardFromIDMap[detID] = readoutBoard;

    fDetectorIDFromDetectorMap[detector] = detID;
    fReadoutBoardFromDetectorMap[detector] = readoutBoard;
    fDetectorTypeFromDetectorMap[detector] = detectorType;
    fDetectorListFromDetectorTypeMap[detectorType].push_back(detector);
    fDetectorListFromReadoutBoardMap[readoutBoard].push_back(detector);

    fDetectorFromPlaneMap[EtaSector] = detector;
    fPlaneListFromDetectorMap[detector].push_back(EtaSector);

    fCMSGEMDetectorMap[EtaSector].push_back(etaSectorPos);
    fCMSGEMDetectorMap[EtaSector].push_back(etaSectorSize);
    fCMSGEMDetectorMap[EtaSector].push_back(nbOfSectorConnectors);
    fCMSGEMDetectorMap[EtaSector].push_back(apvOrientOnEtaSector);
}

//======================================================================================================================================
std::string GemMapping::GetAPV(std::string detPlane, int fecId, int adcCh, int apvNo, int apvIndex) {
    stringstream out;
    out << fecId;
    std::string fecIDStr = out.str();
    out.str("");
    out << adcCh;
    std::string adcChStr = out.str();
    out.str("");
    out << apvNo;
    std::string apvNoStr = out.str();
    out.str("");
    out << apvIndex;
    std::string apvIndexStr = out.str();
    out.str("");
    std::string apvName = "apv" + apvNoStr + "_adcCh" + adcChStr + "_FecId" + fecIDStr + "_" + detPlane;
    return apvName;
}

//======================================================================================================================================
void GemMapping::SetPadsReadoutMap(std::string readoutBoard, std::string detectorType, std::string detector, int detID,
                                   std::string padPlane, float padSizeX, float padSizeY, float nbOfPadX,
                                   float nbOfPadY, float nbOfConnectors) {
    printf("   GemMapping::SetDetectorMap() =>readout=%s, detType=%s, det=%s, detID=%d, padPlane=%s, nbOfPadX=%f, padSizeX=%f, nbOfPadY=%f, padSizeY=%f, nbOfConnectors=%f \n",
           readoutBoard.c_str(), detectorType.c_str(), detector.c_str(), detID, padPlane.c_str(), nbOfPadX, padSizeX,
           nbOfPadY, padSizeY, nbOfConnectors);

    fDetectorFromIDMap[detID] = detector;
    fReadoutBoardFromIDMap[detID] = readoutBoard;

    fDetectorIDFromDetectorMap[detector] = detID;
    fReadoutBoardFromDetectorMap[detector] = readoutBoard;
    fDetectorTypeFromDetectorMap[detector] = detectorType;
    fDetectorListFromDetectorTypeMap[detectorType].push_back(detector);
    fDetectorListFromReadoutBoardMap[readoutBoard].push_back(detector);

    fDetectorFromPlaneMap[padPlane] = detector;
    fPlaneListFromDetectorMap[detector].push_back(padPlane);
    fPlaneIDFromPlaneMap[padPlane] = 0;
    fPlaneFromPlaneIDMap[0] = padPlane;

    fPadDetectorMap[detector].push_back(padSizeX);
    fPadDetectorMap[detector].push_back(padSizeY);
    fPadDetectorMap[detector].push_back(nbOfPadX);
    fPadDetectorMap[detector].push_back(nbOfPadY);
    fPadDetectorMap[detector].push_back(nbOfConnectors);
}

//======================================================================================================================================
void GemMapping::SetAPVMap(std::string detPlane, int fecId, int adcCh, int apvNo, int apvOrient, int apvIndex,
                           int apvHdr, std::string defaultAPV) {
    int apvID = (fecId << 4) | adcCh;
    printf("   GemMapping::SetAPVMap()=> apv[%d]: apvId=%d, fecID=%d, adcCh=%d, plane=%s, orient=%d, index=%d, hdr=%d,  status=%s \n",
           apvNo, apvID, fecId, adcCh, detPlane.c_str(), apvOrient, apvIndex, apvHdr, defaultAPV.c_str());
    std::string apvName = GetAPV(detPlane, fecId, adcCh, apvNo, apvIndex);
    fAPVNoFromIDMap[apvID] = apvNo;
    fAPVIDFromAPVNoMap[apvNo] = apvID;
    fAPVFromIDMap[apvID] = apvName;
    fAPVstatusMap[apvID] = defaultAPV;
    fAPVHeaderLevelFromIDMap[apvID] = apvHdr;
    fAPVOrientationFromIDMap[apvID] = apvOrient;
    fAPVIndexOnPlaneFromIDMap[apvID] = apvIndex;
    fAPVIDFromNameMap[apvName] = apvID;
    fPlaneFromAPVIDMap[apvID] = detPlane;
    fAPVIDListFromFECIDMap[fecId].push_back(apvID);
    fFECIDListFromPlaneMap[detPlane].push_back(fecId);
    fAPVIDListFromPlaneMap[detPlane].push_back(apvID);
    fADCchannelsFromFECMap[fecId].push_back(adcCh);
    std::string detector = GetDetectorFromPlane(detPlane);
    fAPVIDListFromDetectorMap[detector].push_back(apvID);
}

//======================================================================================================================================
void GemMapping::SetAPVtoPadMapping(int fecId, int adcCh, int padId, int apvCh) {
    int apvID = (fecId << 4) | adcCh;
    int apvChPadCh = (padId << 8) | apvCh;
    fAPVToPadChannelMap[apvID].push_back(apvChPadCh);
}

//============================================================================================
void GemMapping::LoadAPVtoPadMapping(const char *mappingCfgFilename) {
    //  Clear() ;
    printf("  GemMapping::LoadAPVtoPadMapping() ==> Loading Mapping from %s \n", mappingCfgFilename);
    ifstream filestream(mappingCfgFilename, ifstream::in);
    TString line;
    while (line.ReadLine(filestream)) {

        line.Remove(TString::kLeading, ' ');   // strip leading spaces
        if (line.BeginsWith("#")) continue;   // and skip comments
        //    printf("   GemMapping::LoadAPVtoPadMapping ==> Scanning the mapping cfg file %s\n",line.c_str()) ;

        //=== Create an array of the tokens separated by "," in the line;
        TObjArray *tokens = line.Tokenize(",");

        //=== iterator on the tokens array
        TIter myiter(tokens);
        while (TObjString *st = (TObjString *) myiter.Next()) {

            //== Remove leading and trailer spaces
            // TODO PLACE!!!
//      std::string s = st->GetString().Remove(TString::kLeading, ' ' );
//      s.Remove(std::string::kTrailing, ' ' );
//      if(s == "PAD") {
//        int apvCh = (((TObjString*) myiter.Next())->GetString().Remove(TString::kLeading, ' ' )).Atoi();
//        int padId = (((TObjString*) myiter.Next())->GetString().Remove(TString::kLeading, ' ' )) .Atoi();;
//        int fecId = (((TObjString*) myiter.Next())->GetString().Remove(TString::kLeading, ' ' )).Atoi();
//        int adcCh = (((TObjString*) myiter.Next())->GetString().Remove(TString::kLeading, ' ' )).Atoi();
//        SetAPVtoPadMapping(fecId, adcCh, padId, apvCh) ;
//      }
        }
        tokens->Delete();
    }
    printf("\n  ======================================================================================================================\n");
}

//======================================================================================================================================
void GemMapping::PrintMapping() {
    using namespace std;
    map<std::string, list<std::string> >::const_iterator det_itr;
    for (det_itr = fPlaneListFromDetectorMap.begin(); det_itr != fPlaneListFromDetectorMap.end(); ++det_itr) {
        string detector(det_itr->first);
        list<std::string> detPlaneList = det_itr->second;

        printf("  ======================================================================================================================\n");
        printf("   GemMapping::PrintMapping() ==> Detector = %s \n", detector.c_str());

        list<std::string>::const_iterator plane_itr;
        for (plane_itr = detPlaneList.begin(); plane_itr != detPlaneList.end(); ++plane_itr) {
            std::string detPlane = *plane_itr;
            list<int> fecList = GetFECIDListFromPlane(detPlane);
            list<int>::const_iterator fec_itr;
            for (fec_itr = fecList.begin(); fec_itr != fecList.end(); ++fec_itr) {
                int fecId = *fec_itr;
                printf("   GemMapping::PrintMapping() ==> Plane=%s, FEC=%d \n", detPlane.c_str(), fecId);
                list<int> apvList = GetAPVIDListFromPlane(detPlane);
                list<int>::const_iterator apv_itr;
                for (apv_itr = apvList.begin(); apv_itr != apvList.end(); ++apv_itr) {
                    int apvID = *apv_itr;
                    int apvNo = GetAPVNoFromID(apvID);
                    int apvIndex = GetAPVIndexOnPlane(apvID);
                    int apvOrient = GetAPVOrientation(apvID);
                    int fecID = GetFECIDFromAPVID(apvID);
                    int adcCh = GetADCChannelFromAPVID(apvID);
                    int apvHdrLevel = GetAPVHeaderLevelFromID(apvID);
                    std::string apvName = GetAPVFromID(apvID);
                    std::string defautAPV = GetAPVstatus(apvID);
                    if (fecID == fecId)
                        printf("   GemMapping::PrintMapping() ==> adcCh=%d, apv=%s, apvID=%d, apvNo=%d, index=%d, orient=%d, hdr=%d, status=%s\n",
                               adcCh, apvName.c_str(), apvID, apvNo, apvIndex, apvOrient, apvHdrLevel, defautAPV.c_str());
                }
            }
            printf("\n");
        }
    }

    printf("   GemMapping::PrintMapping() ==> Mapping of %d detectors, %d planes, %d FECs, %d APVs\n",
           GetNbOfDetectors(), GetNbOfPlane(), GetNbOfFECs(), GetNbOfAPVs());
    printf("======================================================================================================================\n");
}

//======================================================================================================================================
void GemMapping::SaveMapping(const char *file) {
    printf("   GemMapping::SaveMapping() ==> Saving PRD Mapping to file [%s],\n", file);
    FILE *f = fopen(file, "w");
    fprintf(f, "#################################################################################################\n");
    fprintf(f, "         readoutType  Detector    Plane  DetNo   Plane   size (mm)  connectors  orientation\n");
    fprintf(f, "#################################################################################################\n");
    map<std::string, list<std::string> >::const_iterator det_itr;
    for (det_itr = fPlaneListFromDetectorMap.begin(); det_itr != fPlaneListFromDetectorMap.end(); ++det_itr) {
        std::string detector = det_itr->first;
        std::string readoutBoard = GetReadoutBoardFromDetector(detector);
        std::string detectorType = GetDetectorTypeFromDetector(detector);
        if ((readoutBoard == "CARTESIAN") || (readoutBoard == "UV_ANGLE_OLD")) {
            list<std::string> detPlaneList = det_itr->second;
            std::string planeX = detPlaneList.front();
            vector<float> cartesianPlaneX = GetCartesianReadoutMap(planeX);
            float sizeX = cartesianPlaneX[1];
            int connectorsX = (int) (cartesianPlaneX[2]);
            int orientX = (int) (cartesianPlaneX[3]);

            std::string planeY = detPlaneList.back();
            vector<float> cartesianPlaneY = GetCartesianReadoutMap(planeY);
            float sizeY = cartesianPlaneY[1];
            int connectorsY = (int) (cartesianPlaneY[2]);
            int orientY = (int) (cartesianPlaneY[3]);
            fprintf(f, "DET,  %s,   %s,   %s,   %s,  %f,   %d,   %d,   %s,   %f,   %d,   %d \n", readoutBoard.c_str(),
                    detectorType.c_str(), detector.c_str(), planeX.c_str(), sizeX, connectorsX, orientX, planeY.c_str(),
                    sizeY, connectorsY, orientY);
        } else {
            printf("   GemMapping::SaveMapping() ==> detector readout board type %s is not yet implemented ==> PLEASE MOVE ON \n",
                   readoutBoard.c_str());
            continue;
        }
    }

    fprintf(f, "###############################################################\n");
    fprintf(f, "#     fecId   adcCh   detPlane  apvOrient  apvIndex    apvHdr #\n");
    fprintf(f, "###############################################################\n");
    map<int, std::string>::const_iterator apv_itr;
    for (apv_itr = fAPVFromIDMap.begin(); apv_itr != fAPVFromIDMap.end(); ++apv_itr) {
        int apvID = apv_itr->first;
        int fecId = GetFECIDFromAPVID(apvID);
        int adcCh = GetADCChannelFromAPVID(apvID);
        std::string detPlane = GetPlaneFromAPVID(apvID);
        int apvOrient = GetAPVOrientation(apvID);
        int apvIndex = GetAPVIndexOnPlane(apvID);
        int apvHdr = GetAPVHeaderLevelFromID(apvID);
        std::string defautAPV = GetAPVstatus(apvID);
        fprintf(f, "APV,   %d,     %d,     %s,     %d,    %d,   %d,  %s, \n", fecId, adcCh, detPlane.c_str(), apvOrient,
                apvIndex, apvHdr, defautAPV.c_str());
    }
    fclose(f);
}

//============================================================================================
void GemMapping::LoadMapping(const char *mappingCfgFilename) {

    Clear();
    printf("   GemMapping::LoadMapping() ==> Loading Mapping from %s \n", mappingCfgFilename);
    int apvNo = 0;
    int detID = 0;
    // Check if the file exists and can be opened for reading
    if (!std::filesystem::exists(mappingCfgFilename) ||
        !std::filesystem::is_regular_file(mappingCfgFilename)) {
        throw JException("Error: File '" + std::string(mappingCfgFilename) + "' does not exist or cannot be opened.");
    }

    ifstream filestream(mappingCfgFilename, ifstream::in);
    TString line;
    while (line.ReadLine(filestream)) {

        line.Remove(TString::kLeading, ' ');   // strip leading spaces
        if (line.BeginsWith("#")) continue;   // and skip comments
        //printf("   GemMapping::LoadDefaultMapping() ==> Scanning the mapping cfg file %s\n",line.c_str()) ;

        //=== Create an array of the tokens separated by "," in the line;
        TObjArray *tokens = line.Tokenize(",");

        //=== iterator on the tokens array
        TIter myiter(tokens);
        while (TObjString *st = (TObjString *) myiter.Next()) {

            //== Remove leading and trailer spaces
            TString s = ((TString) st->GetString()).Remove(TString::kLeading, ' ');
            s.Remove(TString::kTrailing, ' ');

            //      printf("    GemMapping::LoadDefaultMapping() ==> Data ==> %s\n",s.c_str()) ;
            if (s == "DET") {
                std::string readoutBoard(((TString) ((TObjString *) myiter.Next())->GetString()).Remove(TString::kLeading, ' '));
                std::string detectorType(((TString) ((TObjString *) myiter.Next())->GetString()).Remove(TString::kLeading,' '));
                std::string detector(((TString) ((TObjString *) myiter.Next())->GetString()).Remove(TString::kLeading, ' '));
                //	printf("    GemMapping::LoadDefaultMapping() ==> Data ==> %s\n",s.c_str()) ;

                if (readoutBoard == "CARTESIAN") {
                    std::string planeX(((TString) ((TObjString *) myiter.Next())->GetString()).Remove(TString::kLeading, ' '));
                    float sizeX = (((TString) ((TObjString *) myiter.Next())->GetString()).Remove(TString::kLeading, ' ')).Atof();
                    int nbOfConnectorsX = (((TString) ((TObjString *) myiter.Next())->GetString()).Remove(TString::kLeading, ' ')).Atoi();
                    int orientationX = (((TString) ((TObjString *) myiter.Next())->GetString()).Remove(TString::kLeading, ' ')).Atoi();

                    std::string planeY(((TString) ((TObjString *) myiter.Next())->GetString()).Remove(TString::kLeading, ' '));
                    float sizeY = (((TString) ((TObjString *) myiter.Next())->GetString()).Remove(TString::kLeading, ' ')).Atof();
                    int nbOfConnectorsY = (((TString) ((TObjString *) myiter.Next())->GetString()).Remove(TString::kLeading, ' ')).Atoi();
                    int orientationY = (((TString) ((TObjString *) myiter.Next())->GetString()).Remove(TString::kLeading, ' ')).Atoi();
                    SetCartesianStripsReadoutMap(readoutBoard, detectorType, detector, detID, planeX, sizeX,
                                                 nbOfConnectorsX, orientationX, planeY, sizeY, nbOfConnectorsY,
                                                 orientationY);

                } else if (readoutBoard == "1DSTRIPS") {
                    std::string plane(((TString) ((TObjString *) myiter.Next())->GetString()).Remove(TString::kLeading,' '));
                    float size = (float)(((TString) ((TObjString *) myiter.Next())->GetString()).Remove(TString::kLeading, ' ')).Atof();
                    int nbOfConnectors = (((TString) ((TObjString *) myiter.Next())->GetString()).Remove(TString::kLeading, ' ')).Atoi();
                    int orientation = (((TString) ((TObjString *) myiter.Next())->GetString()).Remove(TString::kLeading, ' ')).Atoi();
                    Set1DStripsReadoutMap(readoutBoard, detectorType, detector, detID, plane, size, nbOfConnectors,orientation);
                } else if (readoutBoard == "UV_ANGLE") {
                    float length = (float)(((TString) ((TObjString *) myiter.Next())->GetString()).Remove(TString::kLeading, ' ')).Atof();
                    float outerRadius = (float)(((TString) ((TObjString *) myiter.Next())->GetString()).Remove(TString::kLeading, ' ')).Atof();
                    float innerRadius = (float)(((TString) ((TObjString *) myiter.Next())->GetString()).Remove(TString::kLeading, ' ')).Atof();

                    std::string planeTop(((TString) ((TObjString *) myiter.Next())->GetString()).Remove(TString::kLeading,' '));
                    int nbOfConnectorsTop = (((TString) ((TObjString *) myiter.Next())->GetString()).Remove(TString::kLeading, ' ')).Atoi();
                    int orientationTop = (((TString) ((TObjString *) myiter.Next())->GetString()).Remove(TString::kLeading, ' ')).Atoi();

                    std::string planeBot(((TString) ((TObjString *) myiter.Next())->GetString()).Remove(TString::kLeading, ' '));
                    int nbOfConnectorsBot = (((TString) ((TObjString *) myiter.Next())->GetString()).Remove(TString::kLeading, ' ')).Atoi();
                    int orientationBot = (((TString) ((TObjString *) myiter.Next())->GetString()).Remove(TString::kLeading, ' ')).Atoi();

                    SetUVStripsReadoutMap(readoutBoard, detectorType, detector, detID, length, innerRadius, outerRadius,
                                          planeTop, nbOfConnectorsTop, orientationTop, planeBot, nbOfConnectorsBot,
                                          orientationBot);
                } else if (readoutBoard == "PADPLANE") {
                    std::string padPlane(((TString) ((TObjString *) myiter.Next())->GetString()).Remove(TString::kLeading,' '));
                    float padSizeX = (float)(((TString) ((TObjString *) myiter.Next())->GetString()).Remove(TString::kLeading, ' ')).Atof();
                    float nbPadX = (float)(((TString) ((TObjString *) myiter.Next())->GetString()).Remove(TString::kLeading, ' ')).Atof();
                    float padSizeY = (float)(((TString) ((TObjString *) myiter.Next())->GetString()).Remove(TString::kLeading, ' ')).Atof();
                    float nbPadY = (float)(((TString) ((TObjString *) myiter.Next())->GetString()).Remove(TString::kLeading,' ')).Atof();
                    float nbConnectors = (float)(((TString) ((TObjString *) myiter.Next())->GetString()).Remove(TString::kLeading, ' ')).Atof();
                    SetPadsReadoutMap(readoutBoard, detectorType, detector, detID, padPlane, padSizeX, padSizeY, nbPadX, nbPadY, nbConnectors);
                } else if (readoutBoard == "CMSGEM") {
                    std::string etaSector(((TString) ((TObjString *) myiter.Next())->GetString()).Remove(TString::kLeading, ' '));
                    float etaSectorPos = (float)(((TString) ((TObjString *) myiter.Next())->GetString()).Remove(TString::kLeading, ' ')).Atof();
                    float etaSectorSize = (float)(((TString) ((TObjString *) myiter.Next())->GetString()).Remove(TString::kLeading, ' ')).Atof();
                    float nbConnectors = (float)(((TString) ((TObjString *) myiter.Next())->GetString()).Remove(TString::kLeading, ' ')).Atof();
                    int orientation = (((TString) ((TObjString *) myiter.Next())->GetString()).Remove(TString::kLeading, ' ')).Atoi();
                    SetCMSGEMReadoutMap(readoutBoard, detectorType, detector, detID, etaSector, etaSectorPos,
                                        etaSectorSize, nbConnectors, orientation);
                } else {
                    printf("XXXXXXX GemMapping::LoadDefaultMapping()==> detector with this readout board type %s is not yet implemented ==> PLEASE MOVE ON XXXXXXXXXXX \n",
                           readoutBoard.c_str());
                    continue;
                }
                detID++;
            }

            if (s == "APV") {
                int fecId = (((TString) ((TObjString *) myiter.Next())->GetString()).Remove(TString::kLeading,' ')).Atoi();
                int adcCh = (((TString) ((TObjString *) myiter.Next())->GetString()).Remove(TString::kLeading,' ')).Atoi();
                std::string detPlane(((TString) ((TObjString *) myiter.Next())->GetString()).Remove(TString::kLeading,' '));
                int apvOrient = (((TString) ((TObjString *) myiter.Next())->GetString()).Remove(TString::kLeading,' ')).Atoi();
                int apvIndex = (((TString) ((TObjString *) myiter.Next())->GetString()).Remove(TString::kLeading,' ')).Atoi();
                int apvheader = (((TString) ((TObjString *) myiter.Next())->GetString()).Remove(TString::kLeading,' ')).Atoi();
                std::string defautAPV(((TString) ((TObjString *) myiter.Next())->GetString()).Remove(TString::kLeading, ' '));

                if (detPlane == "NULL") continue;
                SetAPVMap(detPlane, fecId, adcCh, apvNo, apvOrient, apvIndex, apvheader, defautAPV);
                apvNo++;
            }
            if (s == "FEC") {
                int fecId = (((TString) ((TObjString *) myiter.Next())->GetString()).Remove(TString::kLeading, ' ')).Atoi();
                std::string ip(((TString) ((TObjString *) myiter.Next())->GetString()).Remove(TString::kLeading, ' '));
                fFECIPFromFECID[fecId] = ip;
                fFECIDFromFECIP[ip] = fecId;
            }
        }
        tokens->Delete();
    }
    ComputeActiveADCchannelsMap();
}

//============================================================================================
void GemMapping::Clear() {
    printf("   GemMapping::Clear() ==> Clearing Previous Mapping \n");

    fAPVIDFromAPVNoMap.clear();
    fAPVIDFromNameMap.clear();
    fAPVIDListFromDetectorMap.clear();
    fAPVIDListFromPlaneMap.clear();
    fAPVNoFromIDMap.clear();
    fAPVFromIDMap.clear();
    fAPVstatusMap.clear();
    fAPVHeaderLevelFromIDMap.clear();

    fPlaneIDFromPlaneMap.clear();
    fDetectorIDFromDetectorMap.clear();
    fDetectorFromIDMap.clear();
    fDetectorFromAPVIDMap.clear();
    fDetectorFromPlaneMap.clear();

    fPlaneFromAPVIDMap.clear();
    fReadoutBoardFromIDMap.clear();
    fReadoutBoardFromDetectorMap.clear();
    fNbOfAPVsFromDetectorMap.clear();
    fAPVOrientationFromIDMap.clear();
    fAPVIndexOnPlaneFromIDMap.clear();
    //  fADCchannelsFromFEC.clear() ;
    printf("   GemMapping::Clear() ==> Previous Mapping cleared \n");
}

//============================================================================================
template<typename M>
void ClearMapOfList(M &amap) {
    for (typename M::iterator it = amap.begin(); it != amap.end(); ++it) {
        ((*it).second).clear();
    }
    amap.clear();
}

//============================================================================================
void GemMapping::ComputeActiveADCchannelsMap() {
    printf("   GemMapping::ComputeActiveADCchannelsMap() ==> ComputeActiveADCchannelsMap  \n");
    fActiveADCchannelsMap.clear();
    map<int, int>::const_iterator adcChannel_itr;
    for (adcChannel_itr = fAPVNoFromIDMap.begin(); adcChannel_itr != fAPVNoFromIDMap.end(); ++adcChannel_itr) {
        int apvid = (*adcChannel_itr).first;
        int activeChannel = apvid & 0xF;
        int fecId = (apvid >> 4) & 0xF;
        fActiveADCchannelsMap[fecId].push_back(activeChannel);
    }
}

//=====================================================
int GemMapping::CMSStripMapping(int chNo) {
    if ((chNo % 2) == 1) {
        chNo = 127 - ((chNo - 1) / 2);
    } else {
        chNo = (chNo / 2);
    }
    return chNo;
}

//=====================================================
int GemMapping::StandardMapping(int chNo) {
    return chNo;
}

//=====================================================
int GemMapping::EICStripMapping(int chNo) {
    if (chNo % 2 == 0) chNo = chNo / 2;
    else chNo = 64 + (chNo - 1) / 2;
    return chNo;
}

//=====================================================
int GemMapping::HMSStripMapping(int chNo) {
    if (chNo % 4 == 0) chNo = chNo + 2;
    else if (chNo % 4 == 1) chNo = chNo - 1;
    else if (chNo % 4 == 2) chNo = chNo + 1;
    else if (chNo % 4 == 3) chNo = chNo - 2;
    else chNo = chNo;
    return chNo;
}

//=====================================================
int GemMapping::PRadStripMapping(int apvID, int chNo) {
    printf("   GemMapping::GetPRadStripMapping(() ==> APVId=%d \n", apvID);
    //------------ APV25 Internal Channel Mapping
    chNo = (32 * (chNo % 4)) + (8 * (int) (chNo / 4)) - (31 * (int) (chNo / 16));

    //------------ APV25 Channel to readout strip Mapping
    if (( GetPlaneFromAPVID(apvID).find("X") != std::string::npos) && (GetAPVIndexOnPlane(apvID) == 11)) {
        if (chNo % 2 == 0)
            chNo = (chNo / 2) + 48;
        else if (chNo < 96)
            chNo = (95 - chNo) / 2;
        else
            chNo = 127 + (97 - chNo) / 2;
    } else { // NON (fDetectorType == "PRADGEM") && (fPlane.Contains("Y")) && (fAPVIndex == 11)
        if (chNo % 2 == 0)
            chNo = (chNo / 2) + 32;
        else if (chNo < 64)
            chNo = (63 - chNo) / 2;
        else
            chNo = 127 + (65 - chNo) / 2;
    }
    //   printf("PRDPedestal::PRadStripsFMapping ==>  APVID=%d, chNo=%d, stripNo=%d, \n",fAPVID, chno, chNo) ;
    return chNo;
}

//=====================================================
int GemMapping::GetStripMapping(int apvID, int chNo) {
    chNo = APVchannelCorrection(chNo);
    std::string detectorType = fDetectorTypeFromDetectorMap[fDetectorFromAPVIDMap[apvID]];
    if (detectorType == "CMSGEM") return CMSStripMapping(chNo);
    else if (detectorType == "EICPROTO1") return EICStripMapping(chNo);
    else if (detectorType == "HMSGEM") return HMSStripMapping(chNo);
    else if (detectorType == "PRADGEM") return PRadStripMapping(apvID, chNo);
    else return StandardMapping(chNo);
}

//=====================================================
int GemMapping::APVchannelCorrection(int chNo) {
    return (32 * (chNo % 4)) + (8 * (int) (chNo / 4)) - (31 * (int) (chNo / 16));
}


//======================================================================================
// Get Bank IDs 
set<int> GemMapping::GetBankIDSet() {
    set<int> fec;
    fec.clear();
    map<int, list<int> >::const_iterator it;
    for (it = fAPVIDListFromFECIDMap.begin(); it != fAPVIDListFromFECIDMap.end(); ++it) {
        fec.insert(it->first);
    }
    return fec;
}

std::string GemMapping::GetFECIPFromFECID(int id) {
    return fFECIPFromFECID[id];
}

int GemMapping::GetFECIDFromFECIP(std::string ip) {
    using namespace std;
    string str1 = ip;

    str1.erase(remove(str1.begin(), str1.end(), ' '), str1.end());
    map<std::string, int>::iterator it = fFECIDFromFECIP.begin();
    for (; it != fFECIDFromFECIP.end(); ++it) {
        string str2 = it->first;
        str2.erase(remove(str2.begin(), str2.end(), ' '), str2.end());
        if (str1.compare(str2) == 0) {
            return it->second;
        }
    }
    return -1;
}

//=====================================================
int GemMapping::GetPlaneID(std::string planeName) {
    std::string readoutType = GetReadoutBoardFromDetector(GetDetectorFromPlane(planeName));
    if (readoutType == "CARTESIAN") return (int) (fCartesianPlaneMap[planeName])[0];
    if (readoutType == "1DSTRIPS") return (int) (f1DStripsPlaneMap[planeName])[0];
    if (readoutType == "CMSGEM") return (int) (fCMSGEMDetectorMap[planeName])[0];
    if (readoutType == "UV_ANGLE") return (int) (fUVangleReadoutMap[planeName])[0];
    return -1;
}

//=====================================================
int GemMapping::GetEtaSector(std::string planeName) {
    std::string readoutType = GetReadoutBoardFromDetector(GetDetectorFromPlane(planeName));
    int planeIDorEtaSector = (int) (fCartesianPlaneMap[planeName])[0];
    if (readoutType == "1DSTRIPS") planeIDorEtaSector = (int) (f1DStripsPlaneMap[planeName])[0];
    if (readoutType == "CMSGEM") planeIDorEtaSector = (int) (fCMSGEMDetectorMap[planeName])[0];
    if (readoutType == "UV_ANGLE") planeIDorEtaSector = (int) (fUVangleReadoutMap[planeName])[0];
    return planeIDorEtaSector;
}

//=====================================================
float GemMapping::GetPlaneSize(std::string planeName) {
    std::string readoutType = GetReadoutBoardFromDetector(GetDetectorFromPlane(planeName));
    float planeSize = 0;
    if (readoutType == "CARTESIAN") planeSize = (fCartesianPlaneMap[planeName])[1];
    if (readoutType == "1DSTRIPS") planeSize = (f1DStripsPlaneMap[planeName])[1];
    if (readoutType == "CMSGEM") planeSize = (fCMSGEMDetectorMap[planeName])[1];
    if (readoutType == "UV_ANGLE") planeSize = (fUVangleReadoutMap[planeName])[3];
    return planeSize;
}

//=====================================================
int GemMapping::GetNbOfAPVsOnPlane(std::string planeName) {
    std::string readoutType = GetReadoutBoardFromDetector(GetDetectorFromPlane(planeName));
    int nbOfAPVs = (int) (fCartesianPlaneMap[planeName])[2];
    if (readoutType == "1DSTRIPS") nbOfAPVs = (int) (f1DStripsPlaneMap[planeName])[2];
    if (readoutType == "CMSGEM") nbOfAPVs = (int) (fCMSGEMDetectorMap[planeName])[2];
    if (readoutType == "UV_ANGLE") nbOfAPVs = (int) (fUVangleReadoutMap[planeName])[1];
    return nbOfAPVs;
}

//=====================================================
int GemMapping::GetPlaneOrientation(std::string planeName) {
    std::string readoutType = GetReadoutBoardFromDetector(GetDetectorFromPlane(planeName));
    int orient = (int) (fCartesianPlaneMap[planeName])[3];
    if (readoutType == "1DSTRIPS") orient = (int) (f1DStripsPlaneMap[planeName])[3];
    if (readoutType == "CMSGEM") orient = (int) (fCMSGEMDetectorMap[planeName])[3];
    if (readoutType == "UV_ANGLE") orient = (int) (fUVangleReadoutMap[planeName])[2];
    return orient;
} 
