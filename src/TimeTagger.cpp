#include <OpenScanDeviceLib.h>
#include <TimeTagger.h>
#include <TimeTaggerPrivate.h>
#include <EventPipeline.h>

#include <string>
#include <vector>

// Forward declarations
static OScDev_Error TimeTagger_GetModelName(const char **name);
static OScDev_Error TimeTagger_EnumerateInstances(OScDev_PtrArray **devices);
static OScDev_Error TimeTagger_ReleaseInstance(OScDev_Device *device);
static OScDev_Error TimeTagger_GetName(OScDev_Device *device, char *name);
static OScDev_Error TimeTagger_Open(OScDev_Device *device);
static OScDev_Error TimeTagger_Close(OScDev_Device *device);
static OScDev_Error TimeTagger_HasClock(OScDev_Device *device, bool *hasClock);
static OScDev_Error TimeTagger_HasScanner(OScDev_Device *device, bool *hasScanner);
static OScDev_Error TimeTagger_HasDetector(OScDev_Device *device, bool *hasDetector);
static OScDev_Error TimeTagger_GetPixelRates(OScDev_Device *device, OScDev_NumRange **pixelRatesHz);
static OScDev_Error TimeTagger_GetNumberOfChannels(OScDev_Device *device, uint32_t *numChannels);
static OScDev_Error TimeTagger_GetBytesPerSample(OScDev_Device *device, uint32_t *bytesPerSample);
static OScDev_Error Arm(OScDev_Device *device, OScDev_Acquisition *acquisition);
static OScDev_Error Start(OScDev_Device *device);
static OScDev_Error Stop(OScDev_Device *device);
static OScDev_Error IsRunning(OScDev_Device *device, bool *isRunning);
static OScDev_Error Wait(OScDev_Device *device);

static OScDev_DeviceImpl SwabianTimeTaggerImpl = {
    .GetModelName = TimeTagger_GetModelName,
    .EnumerateInstances = TimeTagger_EnumerateInstances,
    .ReleaseInstance = TimeTagger_ReleaseInstance,
    .GetName = TimeTagger_GetName,
    .Open = TimeTagger_Open,
    .Close = TimeTagger_Close,
    .HasClock = TimeTagger_HasClock,
    .HasScanner = TimeTagger_HasScanner,
    .HasDetector = TimeTagger_HasDetector,
    .MakeSettings = TimeTagger_MakeSettings,
    .GetPixelRates = TimeTagger_GetPixelRates,
    .GetNumberOfChannels = TimeTagger_GetNumberOfChannels,
    .GetBytesPerSample = TimeTagger_GetBytesPerSample,
    .Arm = Arm,
    .Start = Start,
    .Stop = Stop,
    .IsRunning = IsRunning,
    .Wait = Wait,
};


static OScDev_Error TimeTagger_GetModelName(const char **name) {
    *name = "Swabian Time Tagger";
    return OScDev_OK;
}

static OScDev_Error TimeTagger_EnumerateInstances(OScDev_PtrArray **devices) {
    std::vector<std::string> serials = scanTimeTagger(false);

    *devices = OScDev_PtrArray_Create();

    for (const std::string &serial : serials) {
        OScDev_Device *device;
        TimeTagger_PrivateData *data = new TimeTagger_PrivateData{serial, nullptr};
        OScDev_RichError *err = OScDev_Error_AsRichError(OScDev_Device_Create(&device, &SwabianTimeTaggerImpl, data));
        if (err) {
            delete data; // This one was never handed to OpenScanLib
            return OScDev_Error_ReturnAsCode(
                OScDev_Error_Wrap(
                    err,
                    ("Failed to create device for serial " + serial).c_str()
                )
            );
        }
        OScDev_PtrArray_Append(*devices, device);
    }

    // TODO
    return OScDev_OK;
}

static OScDev_Error TimeTagger_ReleaseInstance(OScDev_Device *device) {
    delete GetData(device);
    return OScDev_OK;
}

static OScDev_Error TimeTagger_GetName(OScDev_Device *device, char *name) {
    TimeTagger_PrivateData *data = GetData(device);
    std::string s = "Swabian Time Tagger";
    try {
        s += " " + getTimeTaggerModel(data->serial);
    } catch (const std::runtime_error &) {
        ; // make do without the model name
    }
    strncpy_s(name, OScDev_MAX_STR_LEN, s.c_str(), _TRUNCATE);
    return OScDev_OK;
}

static OScDev_Error TimeTagger_Open(OScDev_Device *device) {
    TimeTagger_PrivateData *data = GetData(device);
    try {
        data->tagger = createTimeTagger(data->serial);
    } catch (const std::runtime_error &e) {
        return OScDev_Error_ReturnAsCode(OScDev_Error_Create(e.what()));
    }
    return OScDev_OK;
}

static OScDev_Error TimeTagger_Close(OScDev_Device *device) {
    auto *data = GetData(device);
    // Stop the acquisition first
    data->pipeline.reset();
    freeTimeTagger(data->tagger);
    data->tagger = nullptr;
    return OScDev_OK;
}

static OScDev_Error TimeTagger_HasScanner(OScDev_Device *, bool *hasScanner) {
    *hasScanner = false;
    return OScDev_OK;
}

static OScDev_Error TimeTagger_HasDetector(OScDev_Device *, bool *hasDetector) {
    *hasDetector = true;
    return OScDev_OK;
}

static OScDev_Error TimeTagger_HasClock(OScDev_Device *, bool *hasClock) {
    *hasClock = false;
    return OScDev_OK;
}



static OScDev_Error TimeTagger_GetPixelRates(OScDev_Device *, OScDev_NumRange **pixelRatesHz) {
    // NOTE these values are placeholders.
    // The lower bound of the range was copied from similar device modules.
    // The upper bound was computed from the largest minimum pulse width
    // across the three Swabian Time Tagger models (1 ns for the Time Tagger 20
    // vs. 500 ps / 350 ps on the others). Assumes a marker pulse's high
    // and low states each need to hold for at least the minimum pulse
    // width, so max rate = 1 / (2 * min_pulse_width).
    *pixelRatesHz = OScDev_NumRange_CreateContinuous(1e3, 5e8);
    return OScDev_OK;
}

static OScDev_Error TimeTagger_GetNumberOfChannels(OScDev_Device *, uint32_t *numChannels) {
    // could probably be increased later.
    *numChannels = 1;
    return OScDev_OK;
}

static OScDev_Error TimeTagger_GetBytesPerSample(OScDev_Device *, uint32_t *bytesPerSample) {
    *bytesPerSample = 2;
    return OScDev_OK;
}

static OScDev_Error Arm(OScDev_Device *device, OScDev_Acquisition *acq) {
    OScDev_Log_Info(device, "Arming Swabian Time Tagger acquisition");
    bool useClock, useScanner, useDetector;
    OScDev_Acquisition_IsClockRequested(acq, &useClock);
    OScDev_Acquisition_IsScannerRequested(acq, &useScanner);
    OScDev_Acquisition_IsDetectorRequested(acq, &useDetector);
    if (useClock || useScanner || !useDetector) {
        return OScDev_Error_ReturnAsCode(OScDev_Error_Create(
            "Unsupported operation (only detector role supported)"));
    }

    OScDev_ClockSource clockSource;
    OScDev_Acquisition_GetClockSource(acq, &clockSource);
    if (clockSource != OScDev_ClockSource_External) {
        return OScDev_Error_ReturnAsCode(OScDev_Error_Create(
            "Unsupported operation (only external clock source supported)"));
    }

    auto ctx = tcspc::context::create();

    try {
        // Raw-tag dumping (the "Save Raw Data" setting) is handled inside
        // EventPipeline itself now -- see RawTagDumpSink's comment in
        // EventPipeline.cpp for why it's a broadcast branch of the live
        // pipeline rather than a second, independent measurement.
        GetData(device)->pipeline = std::make_unique<EventPipeline>(device, acq, ctx);
    } catch (const std::runtime_error &e) {
        return OScDev_Error_ReturnAsCode(OScDev_Error_Create(e.what()));
    }

    OScDev_Log_Info(device, "Arming Swabian Time Tagger acquisition");
    return OScDev_OK;
}

static OScDev_Error Start(OScDev_Device *) {
    // We have no software start trigger
    return OScDev_OK;
}

static OScDev_Error Stop(OScDev_Device *device) {
    GetData(device)->pipeline.reset();
    return OScDev_OK;
}

static OScDev_Error IsRunning(OScDev_Device *device, bool *isRunning) {
    auto& pipeline = GetData(device)->pipeline;
    *isRunning = pipeline && pipeline->isRunning();
    OScDev_Log_Info(device, ("Swabian Time Tagger acquisition is running: " + std::string(*isRunning ? "true" : "false")).c_str());
    return OScDev_OK;
}

static OScDev_Error Wait(OScDev_Device *device) {
    auto& pipeline = GetData(device)->pipeline;
    if (pipeline) {
        pipeline->waitUntilFinished();
    }
    return OScDev_OK;
}

static OScDev_Error GetDeviceImpls(OScDev_PtrArray **impls) {
    void *devImpls[] = { &SwabianTimeTaggerImpl, nullptr };
    *impls = OScDev_PtrArray_CreateFromNullTerminated(devImpls);
    return OScDev_OK;
}

OScDev_MODULE_IMPL = {
    .displayName = "Swabian Time Tagger",
    .supportsRichErrors = true,
    .GetDeviceImpls = GetDeviceImpls,
};