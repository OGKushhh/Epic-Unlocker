#include "pch.h"
#include "ScreamAPI.h"
#include "eos-sdk/eos_ecom.h"
#include "eos_intercept.h"
#include "util.h"

// ---------------------------------------------------------------------------
// EOS_Ecom_QueryOwnership
// ---------------------------------------------------------------------------
EOS_DECLARE_FUNC(void) EOS_Ecom_QueryOwnership(
        EOS_HEcom Handle,
        const EOS_Ecom_QueryOwnershipOptions* Options,
        void* ClientData,
        const EOS_Ecom_OnQueryOwnershipCallback CompletionDelegate
){
    static auto original = ScreamAPI::proxyFunction(&EOS_Ecom_QueryOwnership, "EOS_Ecom_QueryOwnership");
    Intercept::Ecom_QueryOwnership(original, Handle, Options, ClientData, CompletionDelegate);
}

// ---------------------------------------------------------------------------
// EOS_Ecom_QueryOwnershipBySandboxIds
// ---------------------------------------------------------------------------
EOS_DECLARE_FUNC(void) EOS_Ecom_QueryOwnershipBySandboxIds(
        EOS_HEcom Handle,
        const EOS_Ecom_QueryOwnershipBySandboxIdsOptions* Options,
        void* ClientData,
        const EOS_Ecom_OnQueryOwnershipBySandboxIdsCallback CompletionDelegate
){
    static auto original = ScreamAPI::proxyFunction(&EOS_Ecom_QueryOwnershipBySandboxIds, "EOS_Ecom_QueryOwnershipBySandboxIds");
    Intercept::Ecom_QueryOwnershipBySandboxIds(original, Handle, Options, ClientData, CompletionDelegate);
}

// ---------------------------------------------------------------------------
// EOS_Ecom_QueryOwnershipToken
// ---------------------------------------------------------------------------
EOS_DECLARE_FUNC(void) EOS_Ecom_QueryOwnershipToken(
        EOS_HEcom Handle,
        const EOS_Ecom_QueryOwnershipTokenOptions* Options,
        void* ClientData,
        const EOS_Ecom_OnQueryOwnershipTokenCallback CompletionDelegate
){
    static auto original = ScreamAPI::proxyFunction(&EOS_Ecom_QueryOwnershipToken, "EOS_Ecom_QueryOwnershipToken");
    Intercept::Ecom_QueryOwnershipToken(original, Handle, Options, ClientData, CompletionDelegate);
}
