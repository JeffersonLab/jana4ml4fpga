// Mapping class is implemented as a singleton

#ifndef __PRDMAPPING__
#define __PRDMAPPING__
/*******************************************************************************
*  AMORE FOR PRD - PRD                                                         *
*  GemMapping                                                                  *
*  PRD Module Class                                                            *
*  Author: Kondo GNANVO 18/08/2010                                             *
*******************************************************************************/

#if !defined(__CINT__) || defined(__MAKECINT__)

#include <sstream>
#include <map>
#include <list>
#include <vector>
#include <set>
#include <fstream>
#include <iostream>
#include <algorithm>
#include "TList.h"
#include "TObject.h"
#include "TSystem.h"
#include "TObjArray.h"
#include "TObjString.h"

#endif



class GemMapping /*: public TObject*/ {

public:

    ~GemMapping() {
        Clear();
        ClearMapOfList(fDetectorListFromDetectorTypeMap);
        ClearMapOfList(fDetectorListFromReadoutBoardMap);
        ClearMapOfList(fPlaneListFromDetectorMap);
        ClearMapOfList(fFECIDListFromPlaneMap);
        ClearMapOfList(fAPVIDListFromFECIDMap);
        ClearMapOfList(fAPVIDListFromPlaneMap);
        ClearMapOfList(fAPVIDListFromDetectorMap);
        ClearMapOfList(fActiveADCchannelsMap);
        ClearMapOfList(fADCchannelsFromFECMap);

        ClearMapOfList(fAPVToPadChannelMap);
        ClearMapOfList(fPadDetectorMap);
        ClearMapOfList(f1DStripsPlaneMap);
        ClearMapOfList(fCartesianPlaneMap);
        ClearMapOfList(fCMSGEMDetectorMap);
        ClearMapOfList(fUVangleReadoutMap);
    }

    template<typename M>
    void ClearMapOfList(M &amap);

    static GemMapping *GetInstance() {
        if (!instance) instance = new GemMapping();
        return instance;
    }

    void PrintMapping();

    void SaveMapping(const char *mappingCfgFilename);

    void LoadMapping(const char *mappingCfgFilename);

    std::set<int> GetBankIDSet();

    std::string GetFECIPFromFECID(int id);

    int GetFECIDFromFECIP(std::string);

    void LoadAPVtoPadMapping(const char *mappingCfgFilename);

    void SetAPVtoPadMapping(int fecId, int adcCh, int padId, int apvCh);;

    void
    SetAPVMap(std::string detPlane, int fecId, int adcCh, int apvNo, int apvOrient, int apvIndex, int apvHdr,
              std::string defaultAPV);

    void Set1DStripsReadoutMap(std::string readoutBoard, std::string detType, std::string detName, int detID, std::string plane,
                               float size, int connectors, int orient);

    void SetCartesianStripsReadoutMap(std::string readoutBoard, std::string detectorType, std::string detector, int detID,
                                      std::string planeX, float sizeX, int connectorsX, int orientX, std::string planeY,
                                      float sizeY, int connectorsY, int orientY);

    void SetUVStripsReadoutMap(std::string readoutBoard, std::string detType, std::string detector, int detID, float length,
                               float outerR, float innerR, std::string planeTop, int conectTop, int orientTop,
                               std::string planeBot, int connectBot, int orientBot);

    void SetPadsReadoutMap(std::string readoutBoard, std::string detType, std::string detName, int detID, std::string padPlane,
                           float sizeX, float sizeY, float nbOfPadX, float nbOfPadY, float nbOfConnectors);

    void
    SetCMSGEMReadoutMap(std::string readoutBoard, std::string detectorType, std::string detector, int detID, std::string EtaSector,
                        float etaSectorPos, float etaSectorSize, float nbOfSectorConnectors,
                        int apvOrientOnEtaSector);

    int APVchannelCorrection(int chNo);

    int PRadStripMapping(int apvID, int chNo);

    int CMSStripMapping(int chNo);;

    int StandardMapping(int chNo);

    int EICStripMapping(int chNo);

    int HMSStripMapping(int chNo);

    int GetStripMapping(int apvID, int chNo);

    void ComputeActiveADCchannelsMap();

    std::vector<int> GetActiveADCchannels(int fecID) { return fActiveADCchannelsMap[fecID]; }

    void Clear();

    std::map<int, int> GetAPVNoFromIDMap() { return fAPVNoFromIDMap; }

    std::map<int, int> GetAPVIDFromAPVNoMap() { return fAPVIDFromAPVNoMap; }

    std::map<int, int> GetAPVGainFromIDMap() { return fAPVGainFromIDMap; }

    std::map<int, int> GetAPVOrientationFromIDMap() { return fAPVOrientationFromIDMap; }

    std::map<int, unsigned int> GetAPVHeaderLevelFromIDMap() { return fAPVHeaderLevelFromIDMap; }

    std::map<int, int> GetAPVIndexOnPlaneFromIDMap() { return fAPVIndexOnPlaneFromIDMap; }

    std::map<int, std::string> GetAPVFromIDMap() { return fAPVFromIDMap; }

    std::map<int, std::string> GetDetectorFromIDMap() { return fDetectorFromIDMap; }

    std::map<int, std::string> GetDetectorFromAPVIDMap() {
        return fDetectorFromAPVIDMap;
    }

    std::map<std::string, int> GetPlaneIDFromPlaneMap() { return fPlaneIDFromPlaneMap; }
    int GetPlaneIDFromPlane(const std::string & planeName) { return fPlaneIDFromPlaneMap[planeName]; }

    std::string  GetPlaneFromPlaneID(int planeId) { return fPlaneFromPlaneIDMap[planeId]; }
    std::map<int, std::string>  GetPlaneFromPlaneIDMap() { return fPlaneFromPlaneIDMap; }

    std::map<std::string, std::vector<float> > GetPadDetectorMap() { return fPadDetectorMap; }

    std::map<std::string, std::vector<float> > GetCartesianPlaneMap() { return fCartesianPlaneMap; }

    std::map<std::string, std::list<int> > GetAPVIDListFromDetectorMap() { return fAPVIDListFromDetectorMap; }

    std::map<std::string, std::list<int> > GetAPVIDListFromPlaneMap() { return fAPVIDListFromPlaneMap; }

    std::list<int> GetAPVIDListFromDetector(std::string detName) {
        fAPVIDListFromDetectorMap[detName].unique();
        return fAPVIDListFromDetectorMap[detName];
    }

    std::list<int> GetAPVIDListFromPlane(std::string planeName) {
        fAPVIDListFromPlaneMap[planeName].unique();
        return fAPVIDListFromPlaneMap[planeName];
    }

    std::list<int> GetFECIDListFromPlane(std::string planeName) {
        fFECIDListFromPlaneMap[planeName].unique();
        return fFECIDListFromPlaneMap[planeName];
    }

    std::list<int> GetAPVIDListFromFECID(int fecId) {
        fAPVIDListFromFECIDMap[fecId].unique();
        return fAPVIDListFromFECIDMap[fecId];
    }

    std::list<std::string> GetPlaneListFromDetector(std::string detName) {
        fPlaneListFromDetectorMap[detName].unique();
        return fPlaneListFromDetectorMap[detName];
    }

    int GetFECIDFromAPVID(int apvID) { return (apvID >> 4) & 0xF; }

    int GetADCChannelFromAPVID(int apvID) { return apvID & 0xF; }

    int GetAPVNoFromID(int apvID) { return fAPVNoFromIDMap[apvID]; }

    int GetAPVIDFromAPVNo(int apvID) { return fAPVIDFromAPVNoMap[apvID]; }

    int GetAPVIndexOnPlane(int apvID) { return fAPVIndexOnPlaneFromIDMap[apvID]; }

    int GetAPVOrientation(int apvID) { return fAPVOrientationFromIDMap[apvID]; }

    int GetAPVIDFromName(std::string apvName) { return fAPVIDFromNameMap[apvName]; }

    unsigned int GetAPVHeaderLevelFromID(int apvID) { return fAPVHeaderLevelFromIDMap[apvID]; }

    int GetDetectorID(std::string detName) { return fDetectorIDFromDetectorMap[detName]; }

    std::string GetDetectorFromID(int detID) { return fDetectorFromIDMap[detID]; }

    std::string GetDetectorTypeFromDetector(std::string detectorName) { return fDetectorTypeFromDetectorMap[detectorName]; }

    std::string GetReadoutBoardFromDetector(std::string detectorName) { return fReadoutBoardFromDetectorMap[detectorName]; }

    std::string GetDetectorFromPlane(std::string planeName) { return fDetectorFromPlaneMap[planeName]; }

    std::vector<float> GetCartesianReadoutMap(std::string plane) { return fCartesianPlaneMap[plane]; }

    std::vector<float> GetUVangleReadoutMap(std::string plane) { return fUVangleReadoutMap[plane]; }

    std::vector<float> Get1DStripsReadoutMap(std::string plane) { return f1DStripsPlaneMap[plane]; }

    std::vector<float> GetCMSGEMReadoutMap(std::string etaSector) { return fCMSGEMDetectorMap[etaSector]; }

    int GetPlaneID(std::string planeName);

    int GetEtaSector(std::string planeName);

    float GetPlaneSize(std::string planeName);

    int GetNbOfAPVsOnPlane(std::string planeName);

    int GetPlaneOrientation(std::string planeName);

    std::string GetPlaneFromAPVID(int apvID) { return fPlaneFromAPVIDMap[apvID]; }

    std::string GetAPVstatus(int apvID) { return fAPVstatusMap[apvID]; }

    std::string GetAPVFromID(int apvID) { return fAPVFromIDMap[apvID]; }

    std::string GetAPV(std::string detPlane, int fecId, int adcChannel, int apvNo, int apvIndex);

    std::vector<int> GetPadChannelsMapping(int apvID) { return fAPVToPadChannelMap[apvID]; }

    std::map<int, std::vector<int> > GetPadChannelsMapping() { return fAPVToPadChannelMap; }

    int GetNbOfAPVs() { return fAPVNoFromIDMap.size(); }

    int GetNbOfFECs() { return fAPVIDListFromFECIDMap.size(); }

    int GetNbOfPlane() { return fDetectorFromPlaneMap.size(); }

    int GetNbOfDetectors() { return fReadoutBoardFromDetectorMap.size(); }

    float GetTrapezoidDetLength(std::string detector) { return (fUVangleReadoutMap[detector])[0]; }

    float GetTrapezoidDetInnerRadius(std::string detector) { return (fUVangleReadoutMap[detector])[1]; }

    float GetTrapezoidDetOuterRadius(std::string detector) { return (fUVangleReadoutMap[detector])[2]; }

    Bool_t IsAPVIDMapped(int apvID) {
        std::map<int, std::string>::iterator itr;
        itr = fAPVFromIDMap.find(apvID);
        if (itr != fAPVFromIDMap.end())
            return kTRUE;
        else
            return kFALSE;
    }


private:

    GemMapping() { fNbOfAPVs = 0; }

    static GemMapping *instance;
    int fNbOfAPVs;

    std::map<int, unsigned int> fAPVHeaderLevelFromIDMap;
    std::map<int, int> fAPVNoFromIDMap, fAPVIDFromAPVNoMap, fAPVIndexOnPlaneFromIDMap, fAPVOrientationFromIDMap;
    std::map<std::string, int> fNbOfAPVsFromDetectorMap;

    std::map<int, int> fAPVGainFromIDMap;
    std::map<std::string, int> fAPVIDFromNameMap;
    std::map<int, std::string> fAPVFromIDMap;
    std::map<int, std::string> fAPVstatusMap;

    std::map<std::string, int> fPlaneIDFromPlaneMap;
    std::map<int, std::string> fPlaneFromPlaneIDMap;
    std::map<int, std::string> fPlaneFromAPVIDMap;

    std::map<int, std::string> fReadoutBoardFromIDMap;

    std::map<std::string, std::string> fDetectorTypeFromDetectorMap;
    std::map<std::string, std::string> fReadoutBoardFromDetectorMap;
    std::map<std::string, int> fDetectorIDFromDetectorMap;
    std::map<int, std::string> fDetectorFromIDMap;
    std::map<int, std::string> fDetectorFromAPVIDMap;
    std::map<std::string, std::string> fDetectorFromPlaneMap;

    std::map<int, std::list<int> > fAPVIDListFromFECIDMap;

    std::map<std::string, std::list<int> > fFECIDListFromPlaneMap;
    std::map<std::string, std::list<int> > fAPVIDListFromDetectorMap;
    std::map<std::string, std::list<int> > fAPVIDListFromPlaneMap;

    std::map<std::string, std::list<std::string> > fDetectorListFromDetectorTypeMap;
    std::map<std::string, std::list<std::string> > fDetectorListFromReadoutBoardMap;
    std::map<std::string, std::list<std::string> > fPlaneListFromDetectorMap;

    std::map<int, std::vector<int> > fAPVToPadChannelMap;
    std::map<int, std::vector<int> > fActiveADCchannelsMap;

    std::map<int, std::vector<float> > fADCchannelsFromFECMap;
    std::map<std::string, std::vector<float> > fPadDetectorMap;
    std::map<std::string, std::vector<float> > fUVangleReadoutMap;
    std::map<std::string, std::vector<float> > f1DStripsPlaneMap;
    std::map<std::string, std::vector<float> > fCartesianPlaneMap;
    std::map<std::string, std::vector<float> > fCMSGEMDetectorMap;

    std::map<int, std::string> fFECIPFromFECID;
    std::map<std::string, int> fFECIDFromFECIP;
};

#endif



