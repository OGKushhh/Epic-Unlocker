#include "pch.h"
#include "ScreamAPI.h"
#include "eos-sdk/eos_ecom.h"
#include "eos_intercept.h"

// ---------------------------------------------------------------------------
// EOS_Ecom_GetItemReleaseCount
// ---------------------------------------------------------------------------
EOS_DECLARE_FUNC(uint32_t) EOS_Ecom_GetItemReleaseCount(
        EOS_HEcom Handle,
        const EOS_Ecom_GetItemReleaseCountOptions* Options
){
    return Intercept::Ecom_GetItemReleaseCount(nullptr, Handle, Options);
}
