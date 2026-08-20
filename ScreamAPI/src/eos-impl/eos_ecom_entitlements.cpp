#include "pch.h"
#include "ScreamAPI.h"
#include "eos-sdk/eos_ecom.h"
#include "eos_intercept.h"
#include "dlc_catalog.h"
#include "util.h"

// ---------------------------------------------------------------------------
// EOS_Ecom_QueryEntitlements
// ---------------------------------------------------------------------------
EOS_DECLARE_FUNC(void) EOS_Ecom_QueryEntitlements(
        EOS_HEcom Handle,
        const EOS_Ecom_QueryEntitlementsOptions* Options,
        void* ClientData,
        const EOS_Ecom_OnQueryEntitlementsCallback CompletionDelegate
){
    static auto original = ScreamAPI::proxyFunction(&EOS_Ecom_QueryEntitlements, "EOS_Ecom_QueryEntitlements");
    Intercept::Ecom_QueryEntitlements(original, Handle, Options, ClientData, CompletionDelegate);
}

// ---------------------------------------------------------------------------
// EOS_Ecom_GetEntitlementsCount
// ---------------------------------------------------------------------------
EOS_DECLARE_FUNC(uint32_t) EOS_Ecom_GetEntitlementsCount(
        EOS_HEcom Handle,
        const EOS_Ecom_GetEntitlementsCountOptions* Options
){
    static auto original = ScreamAPI::proxyFunction(&EOS_Ecom_GetEntitlementsCount, "EOS_Ecom_GetEntitlementsCount");
    return Intercept::Ecom_GetEntitlementsCount(original, Handle, Options);
}

// ---------------------------------------------------------------------------
// EOS_Ecom_GetEntitlementsByNameCount
// ---------------------------------------------------------------------------
EOS_DECLARE_FUNC(uint32_t) EOS_Ecom_GetEntitlementsByNameCount(
        EOS_HEcom Handle,
        const EOS_Ecom_GetEntitlementsByNameCountOptions* Options
){
    static auto original = ScreamAPI::proxyFunction(&EOS_Ecom_GetEntitlementsByNameCount, "EOS_Ecom_GetEntitlementsByNameCount");
    return Intercept::Ecom_GetEntitlementsByNameCount(original, Handle, Options);
}

// ---------------------------------------------------------------------------
// EOS_Ecom_CopyEntitlementByIndex
// ---------------------------------------------------------------------------
EOS_DECLARE_FUNC(EOS_EResult) EOS_Ecom_CopyEntitlementByIndex(
        EOS_HEcom Handle,
        const EOS_Ecom_CopyEntitlementByIndexOptions* Options,
        EOS_Ecom_Entitlement** OutEntitlement
){
    static auto original = ScreamAPI::proxyFunction(&EOS_Ecom_CopyEntitlementByIndex, "EOS_Ecom_CopyEntitlementByIndex");
    return Intercept::Ecom_CopyEntitlementByIndex(original, Handle, Options, OutEntitlement);
}

// ---------------------------------------------------------------------------
// EOS_Ecom_CopyEntitlementByNameAndIndex
// ---------------------------------------------------------------------------
EOS_DECLARE_FUNC(EOS_EResult) EOS_Ecom_CopyEntitlementByNameAndIndex(
        EOS_HEcom Handle,
        const EOS_Ecom_CopyEntitlementByNameAndIndexOptions* Options,
        EOS_Ecom_Entitlement** OutEntitlement
){
    static auto original = ScreamAPI::proxyFunction(&EOS_Ecom_CopyEntitlementByNameAndIndex, "EOS_Ecom_CopyEntitlementByNameAndIndex");
    return Intercept::Ecom_CopyEntitlementByNameAndIndex(original, Handle, Options, OutEntitlement);
}

// ---------------------------------------------------------------------------
// EOS_Ecom_CopyEntitlementById
// ---------------------------------------------------------------------------
EOS_DECLARE_FUNC(EOS_EResult) EOS_Ecom_CopyEntitlementById(
        EOS_HEcom Handle,
        const EOS_Ecom_CopyEntitlementByIdOptions* Options,
        EOS_Ecom_Entitlement** OutEntitlement
){
    static auto original = ScreamAPI::proxyFunction(&EOS_Ecom_CopyEntitlementById, "EOS_Ecom_CopyEntitlementById");
    return Intercept::Ecom_CopyEntitlementById(original, Handle, Options, OutEntitlement);
}

// ---------------------------------------------------------------------------
// EOS_Ecom_Entitlement_Release
// ---------------------------------------------------------------------------
EOS_DECLARE_FUNC(void) EOS_Ecom_Entitlement_Release(EOS_Ecom_Entitlement* Entitlement){
    static auto original = ScreamAPI::proxyFunction(&EOS_Ecom_Entitlement_Release, "EOS_Ecom_Entitlement_Release");
    Intercept::Ecom_Entitlement_Release(original, Entitlement);
}

// ---------------------------------------------------------------------------
// EOS_Ecom_QueryEntitlementToken
// ---------------------------------------------------------------------------
EOS_DECLARE_FUNC(void) EOS_Ecom_QueryEntitlementToken(
        EOS_HEcom Handle,
        const EOS_Ecom_QueryEntitlementTokenOptions* Options,
        void* ClientData,
        const EOS_Ecom_OnQueryEntitlementTokenCallback CompletionDelegate
){
    static auto original = ScreamAPI::proxyFunction(&EOS_Ecom_QueryEntitlementToken, "EOS_Ecom_QueryEntitlementToken");
    Intercept::Ecom_QueryEntitlementToken(original, Handle, Options, ClientData, CompletionDelegate);
}

// ---------------------------------------------------------------------------
// EOS_Ecom_RedeemEntitlements
// ---------------------------------------------------------------------------
EOS_DECLARE_FUNC(void) EOS_Ecom_RedeemEntitlements(
        EOS_HEcom Handle,
        const EOS_Ecom_RedeemEntitlementsOptions* Options,
        void* ClientData,
        const EOS_Ecom_OnRedeemEntitlementsCallback CompletionDelegate
){
    static auto original = ScreamAPI::proxyFunction(&EOS_Ecom_RedeemEntitlements, "EOS_Ecom_RedeemEntitlements");
    Intercept::Ecom_RedeemEntitlements(original, Handle, Options, ClientData, CompletionDelegate);
}
