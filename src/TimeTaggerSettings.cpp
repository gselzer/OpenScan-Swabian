#include "TimeTaggerPrivate.h"
#include <TimeTagger.h>
#include <OpenScanDeviceLib.h>

#include <cstring>
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

class LineDelaySetting {
    static OScDev_Error Get(OScDev_Setting *setting, int32_t *value) {
        *value = GetSettingDeviceData(setting)->lineDelay;
        return OScDev_OK;
    }
    static OScDev_Error Set(OScDev_Setting *setting, int32_t value) {
        GetSettingDeviceData(setting)->lineDelay = value;
        return OScDev_OK;
    }
    static OScDev_Error GetNumericConstraintType(OScDev_Setting *, OScDev_ValueConstraint *constraintType) {
        *constraintType = OScDev_ValueConstraint_Range;
        return OScDev_OK;
    }
    static OScDev_Error GetRange(OScDev_Setting *, int32_t *min, int32_t *max) {
        // Line delay should be non-negative integers
        *min = 0;
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
    static OScDev_Error GetNumericConstraintType(OScDev_Setting *, OScDev_ValueConstraint *constraintType) {
        *constraintType = OScDev_ValueConstraint_Range;
        return OScDev_OK;
    }
    static OScDev_Error GetRange(OScDev_Setting *, int32_t *min, int32_t *max) {
        // Must be positive: it's used as a histogram bin_width divisor and
        // as a pair_all_between time_window in EventPipeline.cpp, both of
        // which are nonsensical (or crash-prone) at zero or negative.
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

class SaveHistogramsSetting {
    static OScDev_Error Get(OScDev_Setting *setting, bool *value) {
        *value = GetSettingDeviceData(setting)->saveHistograms;
        return OScDev_OK;
    }
    static OScDev_Error Set(OScDev_Setting *setting, bool value) {
        GetSettingDeviceData(setting)->saveHistograms = value;
        return OScDev_OK;
    }

public:
    static inline OScDev_SettingImpl impl = {
        .GetBool = Get,
        .SetBool = Set,
    };
};

class HistogramBinsSetting {
    static OScDev_Error Get(OScDev_Setting *setting, int32_t *value) {
        *value = GetSettingDeviceData(setting)->histogramBins;
        return OScDev_OK;
    }
    static OScDev_Error Set(OScDev_Setting *setting, int32_t value) {
        GetSettingDeviceData(setting)->histogramBins = value;
        return OScDev_OK;
    }
    static OScDev_Error GetNumericConstraintType(OScDev_Setting *, OScDev_ValueConstraint *constraintType) {
        *constraintType = OScDev_ValueConstraint_DiscreteValues;
        return OScDev_OK;
    }
    static OScDev_Error GetDiscreteValues(OScDev_Setting *, OScDev_NumArray **values) {
        // A discrete range is enforced, but purely for convenience
        // (i.e. no reason I know of that we couldn't widen it later).
        static const int32_t values_array[] = {
            16, 32, 64, 128, 256, 512, 1024, 2048, 4096,
        };
        *values = OScDev_NumArray_Create();
        for (int32_t v : values_array) {
            OScDev_NumArray_Append(*values, v);
        }
        return OScDev_OK;
    }

public:
    static inline OScDev_SettingImpl impl = {
        .GetNumericConstraintType = GetNumericConstraintType,
        .GetInt32 = Get,
        .SetInt32 = Set,
        .GetInt32DiscreteValues = GetDiscreteValues,
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

class SaveRawDataSetting {
    static OScDev_Error Get(OScDev_Setting *setting, bool *value) {
        *value = GetSettingDeviceData(setting)->saveRawData;
        return OScDev_OK;
    }
    static OScDev_Error Set(OScDev_Setting *setting, bool value) {
        GetSettingDeviceData(setting)->saveRawData = value;
        return OScDev_OK;
    }

public:
    static inline OScDev_SettingImpl impl = {
        .GetBool = Get,
        .SetBool = Set,
    };
};

class FileNamePrefixSetting {
    static OScDev_Error Get(OScDev_Setting *setting, char *value) {
        strncpy_s(value, OScDev_MAX_STR_SIZE,
                  GetSettingDeviceData(setting)->fileNamePrefix.c_str(),
                  _TRUNCATE);
        return OScDev_OK;
    }
    static OScDev_Error Set(OScDev_Setting *setting, const char *value) {
        GetSettingDeviceData(setting)->fileNamePrefix = value;
        return OScDev_OK;
    }

public:
    static inline OScDev_SettingImpl impl = {
        .GetString = Get,
        .SetString = Set,
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
        "Line Clock Channel",
        OScDev_ValueType_Int32,
        &ChannelSetting<&TimeTagger_PrivateData::lineClockChannel>::impl,
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
        "Line Delay",
        OScDev_ValueType_Int32,
        &LineDelaySetting::impl,
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
        "Save Histograms",
        OScDev_ValueType_Bool,
        &SaveHistogramsSetting::impl,
        device
    ));
    if (err) {
        goto error;
    }
    OScDev_PtrArray_Append(*settings, s);

    err = OScDev_Error_AsRichError(OScDev_Setting_Create(
        &s,
        "Histogram Bins",
        OScDev_ValueType_Int32,
        &HistogramBinsSetting::impl,
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

    err = OScDev_Error_AsRichError(OScDev_Setting_Create(
        &s,
        "Save Raw Data",
        OScDev_ValueType_Bool,
        &SaveRawDataSetting::impl,
        device
    ));
    if (err) {
        goto error;
    }
    OScDev_PtrArray_Append(*settings, s);

    err = OScDev_Error_AsRichError(OScDev_Setting_Create(
        &s,
        "File Name Prefix",
        OScDev_ValueType_String,
        &FileNamePrefixSetting::impl,
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
    OScDev_PtrArray_Destroy(*settings);
    *settings = NULL;
    return OScDev_Error_ReturnAsCode(
        OScDev_Error_Wrap(err, "Failed to create settings"));
}
