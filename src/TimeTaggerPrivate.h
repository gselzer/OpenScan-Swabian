#pragma once

#include <memory>
#include <string>
#include <TimeTagger.h>
#include <OpenScanDeviceLib.h>

class TimeTagger_PrivateData {
public:
    std::string serial;
    TimeTaggerBase *tagger = nullptr;
    std::unique_ptr<IteratorBase> pipeline = nullptr;

    int32_t lineClockChannel = 1;
    int32_t syncChannel = 2;
    int32_t photonChannel = 3;

    int32_t syncDelay = 0; // picoseconds
    int32_t lineDelay = 0; // picoseconds
    int32_t maxPhotonPulseWidth = 100'000; // picoseconds
    int32_t maxDiffTime = 15'000; // picoseconds

    bool cumulative = false;
    int32_t histogramBins = 256;

    bool saveHistograms = false;
    bool saveRawData = false;
    std::string fileNamePrefix = "OpenScan-Swabian";
};

static inline class TimeTagger_PrivateData *GetData(OScDev_Device *device) {
    return static_cast<class TimeTagger_PrivateData *>(OScDev_Device_GetImplData(device));
}

OScDev_Error TimeTagger_MakeSettings(OScDev_Device *device, OScDev_PtrArray **settings);