#include "TimeTaggerPrivate.h"
#include <TimeTagger.h>
#include <OpenScanDeviceLib.h>

#include <limits>

static inline TimeTagger_PrivateData *
GetSettingDeviceData(OScDev_Setting *setting) {
    return (TimeTagger_PrivateData *)OScDev_Device_GetImplData(
        (OScDev_Device *)OScDev_Setting_GetImplData(setting));
}

template <int32_t TimeTagger_PrivateData::*Member>
class ChannelSetting {
    static OScDev_Error Get(OScDev_Setting *setting, int32_t *value) {
        *value = GetSettingDeviceData(setting)->*Member;
        return OScDev_OK;
    }
    static OScDev_Error Set(OScDev_Setting *setting, int32_t value) {
        GetSettingDeviceData(setting)->*Member = value;
        return OScDev_OK;
    }
    static OScDev_Error GetNumericConstraintType(OScDev_Setting *, OScDev_ValueConstraint *constraintType) {
        *constraintType = OScDev_ValueConstraint_Range;
        return OScDev_OK;
    }
    static OScDev_Error GetRange(OScDev_Setting *, int32_t *min, int32_t *max) {
        *min = 1; *max = 8; // TODO: Get the actual number of channels from the device
        return OScDev_OK;
    }

public:
    static inline OScDev_SettingImpl impl = {
        .GetNumericConstraintType = GetNumericConstraintType,
        .GetInt32 = Get,
        .SetInt32 = Set,
        .GetInt32Range = GetRange
    };

};

class SyncDelaySetting {
    static OScDev_Error Get(OScDev_Setting *setting, int32_t *value) {
        *value = GetSettingDeviceData(setting)->syncDelay;
        return OScDev_OK;
    }
    static OScDev_Error Set(OScDev_Setting *setting, int32_t value) {
        GetSettingDeviceData(setting)->syncDelay = value;
        return OScDev_OK;
    }

public:
    static inline OScDev_SettingImpl impl = {
        .GetInt32 = Get,
        .SetInt32 = Set,
    };
};

class MaxPhotonPulseWidthSetting {
    static OScDev_Error Get(OScDev_Setting *setting, int32_t *value) {
        *value = GetSettingDeviceData(setting)->maxPhotonPulseWidth;
        return OScDev_OK;
    }
    static OScDev_Error Set(OScDev_Setting *setting, int32_t value) {
        GetSettingDeviceData(setting)->maxPhotonPulseWidth = value;
        return OScDev_OK;
    }
    static OScDev_Error GetNumericConstraintType(OScDev_Setting *, OScDev_ValueConstraint *constraintType) {
        *constraintType = OScDev_ValueConstraint_Range;
        return OScDev_OK;
    }
    static OScDev_Error GetRange(OScDev_Setting *, int32_t *min, int32_t *max) {
        // Widths should be positive integers
        *min = 1;
        *max = std::numeric_limits<int32_t>::max();
        return OScDev_OK;
    }
public:
    static inline OScDev_SettingImpl impl = {
        .GetNumericConstraintType = GetNumericConstraintType,
        .GetInt32 = Get,
        .SetInt32 = Set,
        .GetInt32Range = GetRange,
    };
};

class MaxDiffTimeSetting {
    static OScDev_Error Get(OScDev_Setting *setting, int32_t *value) {
        *value = GetSettingDeviceData(setting)->maxDiffTime;
        return OScDev_OK;
    }
    static OScDev_Error Set(OScDev_Setting *setting, int32_t value) {
        GetSettingDeviceData(setting)->maxDiffTime = value;
        return OScDev_OK;
    }

public:
    static inline OScDev_SettingImpl impl = {
        .GetInt32 = Get,
        .SetInt32 = Set,
    };
};

class SaveFilesSetting {
    static OScDev_Error Get(OScDev_Setting *setting, bool *value) {
        *value = GetSettingDeviceData(setting)->saveFiles;
        return OScDev_OK;
    }
    static OScDev_Error Set(OScDev_Setting *setting, bool value) {
        GetSettingDeviceData(setting)->saveFiles = value;
        return OScDev_OK;
    }

public:
    static inline OScDev_SettingImpl impl = {
        .GetBool = Get,
        .SetBool = Set,
    };
};

class BinWidthSetting {
    static OScDev_Error Get(OScDev_Setting *setting, int32_t *value) {
        *value = GetSettingDeviceData(setting)->binWidth;
        return OScDev_OK;
    }
    static OScDev_Error Set(OScDev_Setting *setting, int32_t value) {
        GetSettingDeviceData(setting)->binWidth = value;
        return OScDev_OK;
    }

public:
    static inline OScDev_SettingImpl impl = {
        .GetInt32 = Get,
        .SetInt32 = Set,
    };
};

class MaxBinIndexSetting {
    static OScDev_Error Get(OScDev_Setting *setting, int32_t *value) {
        *value = GetSettingDeviceData(setting)->maxBinIndex;
        return OScDev_OK;
    }
    static OScDev_Error Set(OScDev_Setting *setting, int32_t value) {
        GetSettingDeviceData(setting)->maxBinIndex = value;
        return OScDev_OK;
    }

public:
    static inline OScDev_SettingImpl impl = {
        .GetInt32 = Get,
        .SetInt32 = Set,
    };
};

class CumulativeSetting {
    static OScDev_Error Get(OScDev_Setting *setting, bool *value) {
        *value = GetSettingDeviceData(setting)->cumulative;
        return OScDev_OK;
    }
    static OScDev_Error Set(OScDev_Setting *setting, bool value) {
        GetSettingDeviceData(setting)->cumulative = value;
        return OScDev_OK;
    }

public:
    static inline OScDev_SettingImpl impl = {
        .GetBool = Get,
        .SetBool = Set,
    };
};

OScDev_Error TimeTagger_MakeSettings(OScDev_Device *device, OScDev_PtrArray **settings) {
    OScDev_RichError *err = OScDev_RichError_OK;
    *settings = OScDev_PtrArray_Create();

    OScDev_Setting *s;
    err = OScDev_Error_AsRichError(OScDev_Setting_Create(
        &s,
        "Sync Channel",
        OScDev_ValueType_Int32,
        &ChannelSetting<&TimeTagger_PrivateData::syncChannel>::impl,
        device
    ));
    if (err) {
        goto error;
    }
    OScDev_PtrArray_Append(*settings, s);

    err = OScDev_Error_AsRichError(OScDev_Setting_Create(
        &s,
        "Photon Channel",
        OScDev_ValueType_Int32,
        &ChannelSetting<&TimeTagger_PrivateData::photonChannel>::impl,
        device
    ));
    if (err) {
        goto error;
    }
    OScDev_PtrArray_Append(*settings, s);

    err = OScDev_Error_AsRichError(OScDev_Setting_Create(
        &s,
        "Pixel Marker Channel",
        OScDev_ValueType_Int32,
        &ChannelSetting<&TimeTagger_PrivateData::pixelMarkerChannel>::impl,
        device
    ));
    if (err) {
        goto error;
    }
    OScDev_PtrArray_Append(*settings, s);

    err = OScDev_Error_AsRichError(OScDev_Setting_Create(
        &s,
        "Sync Delay",
        OScDev_ValueType_Int32,
        &SyncDelaySetting::impl,
        device
    ));
    if (err) {
        goto error;
    }
    OScDev_PtrArray_Append(*settings, s);

    err = OScDev_Error_AsRichError(OScDev_Setting_Create(
        &s,
        "Max Photon Pulse Width",
        OScDev_ValueType_Int32,
        &MaxPhotonPulseWidthSetting::impl,
        device
    ));
    if (err) {
        goto error;
    }
    OScDev_PtrArray_Append(*settings, s);

    err = OScDev_Error_AsRichError(OScDev_Setting_Create(
        &s,
        "Max Diff Time",
        OScDev_ValueType_Int32,
        &MaxDiffTimeSetting::impl,
        device
    ));
    if (err) {
        goto error;
    }
    OScDev_PtrArray_Append(*settings, s);

    err = OScDev_Error_AsRichError(OScDev_Setting_Create(
        &s,
        "Save Files",
        OScDev_ValueType_Bool,
        &SaveFilesSetting::impl,
        device
    ));
    if (err) {
        goto error;
    }
    OScDev_PtrArray_Append(*settings, s);

    err = OScDev_Error_AsRichError(OScDev_Setting_Create(
        &s,
        "Bin Width",
        OScDev_ValueType_Int32,
        &BinWidthSetting::impl,
        device
    ));
    if (err) {
        goto error;
    }
    OScDev_PtrArray_Append(*settings, s);

    err = OScDev_Error_AsRichError(OScDev_Setting_Create(
        &s,
        "Max Bin Index",
        OScDev_ValueType_Int32,
        &MaxBinIndexSetting::impl,
        device
    ));
    if (err) {
        goto error;
    }
    OScDev_PtrArray_Append(*settings, s);

    err = OScDev_Error_AsRichError(OScDev_Setting_Create(
        &s,
        "Cumulative",
        OScDev_ValueType_Bool,
        &CumulativeSetting::impl,
        device
    ));
    if (err) {
        goto error;
    }
    OScDev_PtrArray_Append(*settings, s);

    return OScDev_OK;
error:
    for (size_t i = 0; i < OScDev_PtrArray_Size(*settings); ++i) {
        OScDev_Setting_Destroy((OScDev_Setting *)OScDev_PtrArray_At(*settings, i));
    }
    free(*settings);
    *settings = NULL;
    return OScDev_Error_ReturnAsCode(
        OScDev_Error_Wrap(err, "Failed to create settings"));
}
