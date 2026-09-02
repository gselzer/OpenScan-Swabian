#include <OpenScanDeviceLib.h>

#include <string.h>

// A trivial device module used only by this project's own integration
// tests, to fill the "clock" and "scanner" roles that a detector-only
// acquisition like OpenScanSwabian's still needs.
//
// It does not generate any signal itself -- OpenScanSwabian's fake Time
// Tagger backend already synthesizes its own line-clock/sync/photon
// signals in simulate mode (see src/fake_timetagger/include/TimeTagger.h).

static OScDev_Error GetModelName(const char **name);
static OScDev_Error EnumerateInstances(OScDev_PtrArray **devices);
static OScDev_Error ReleaseInstance(OScDev_Device *device);
static OScDev_Error GetName(OScDev_Device *device, char *name);
static OScDev_Error Open(OScDev_Device *device);
static OScDev_Error Close(OScDev_Device *device);
static OScDev_Error HasClock(OScDev_Device *device, bool *hasClock);
static OScDev_Error HasScanner(OScDev_Device *device, bool *hasScanner);
static OScDev_Error HasDetector(OScDev_Device *device, bool *hasDetector);
static OScDev_Error Arm(OScDev_Device *device, OScDev_Acquisition *acq);
static OScDev_Error Start(OScDev_Device *device);
static OScDev_Error Stop(OScDev_Device *device);
static OScDev_Error IsRunning(OScDev_Device *device, bool *isRunning);
static OScDev_Error Wait(OScDev_Device *device);

static OScDev_DeviceImpl FakeClockScannerImpl = {
    .GetModelName = GetModelName,
    .EnumerateInstances = EnumerateInstances,
    .ReleaseInstance = ReleaseInstance,
    .GetName = GetName,
    .Open = Open,
    .Close = Close,
    .HasClock = HasClock,
    .HasScanner = HasScanner,
    .HasDetector = HasDetector,
    .Arm = Arm,
    .Start = Start,
    .Stop = Stop,
    .IsRunning = IsRunning,
    .Wait = Wait,
};

static OScDev_Error GetModelName(const char **name) {
    *name = "OpenScanSwabian test fake clock/scanner";
    return OScDev_OK;
}

static OScDev_Error EnumerateInstances(OScDev_PtrArray **devices) {
    *devices = OScDev_PtrArray_Create();
    OScDev_Device *device;
    OScDev_Error err =
        OScDev_Device_Create(&device, &FakeClockScannerImpl, NULL);
    if (err) {
        OScDev_PtrArray_Destroy(*devices);
        *devices = NULL;
        return err;
    }
    OScDev_PtrArray_Append(*devices, device);
    return OScDev_OK;
}

static OScDev_Error ReleaseInstance(OScDev_Device *device) {
    (void)device;
    return OScDev_OK;
}

static OScDev_Error GetName(OScDev_Device *device, char *name) {
    (void)device;
    strncpy_s(name, OScDev_MAX_STR_LEN, "FakeClockScanner", _TRUNCATE);
    return OScDev_OK;
}

static OScDev_Error Open(OScDev_Device *device) {
    (void)device;
    return OScDev_OK;
}

static OScDev_Error Close(OScDev_Device *device) {
    (void)device;
    return OScDev_OK;
}

static OScDev_Error HasClock(OScDev_Device *device, bool *hasClock) {
    (void)device;
    *hasClock = true;
    return OScDev_OK;
}

static OScDev_Error HasScanner(OScDev_Device *device, bool *hasScanner) {
    (void)device;
    *hasScanner = true;
    return OScDev_OK;
}

static OScDev_Error HasDetector(OScDev_Device *device, bool *hasDetector) {
    (void)device;
    *hasDetector = false;
    return OScDev_OK;
}

static OScDev_Error Arm(OScDev_Device *device, OScDev_Acquisition *acq) {
    (void)device;
    (void)acq;
    return OScDev_OK;
}

static OScDev_Error Start(OScDev_Device *device) {
    (void)device;
    return OScDev_OK;
}

static OScDev_Error Stop(OScDev_Device *device) {
    (void)device;
    return OScDev_OK;
}

static OScDev_Error IsRunning(OScDev_Device *device, bool *isRunning) {
    (void)device;
    *isRunning = false;
    return OScDev_OK;
}

static OScDev_Error Wait(OScDev_Device *device) {
    (void)device;
    return OScDev_OK;
}

static OScDev_Error GetDeviceImpls(OScDev_PtrArray **impls) {
    void *devImpls[] = {&FakeClockScannerImpl, NULL};
    *impls = OScDev_PtrArray_CreateFromNullTerminated(devImpls);
    return OScDev_OK;
}

OScDev_MODULE_IMPL = {
    .displayName = "OpenScanSwabian test fake clock/scanner module",
    .GetDeviceImpls = GetDeviceImpls,
};
