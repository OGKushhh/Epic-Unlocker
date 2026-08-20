#include "pch.h"
#include "eos-sdk/eos_connect.h"
#include "ScreamAPI.h"
#include "eos_intercept.h"
#include "achievement_manager.h"

EOS_DECLARE_FUNC(void) EOS_Connect_Login(EOS_HConnect Handle, const EOS_Connect_LoginOptions* Options, void* ClientData, const EOS_Connect_OnLoginCallback CompletionDelegate){
    static auto original = ScreamAPI::proxyFunction(&EOS_Connect_Login, "EOS_Connect_Login");
    Intercept::Connect_Login(original, Handle, Options, ClientData, CompletionDelegate);
}


EOS_DECLARE_FUNC(EOS_ProductUserId) EOS_Connect_GetLoggedInUserByIndex(EOS_HConnect Handle, int32_t Index){
    static auto original = ScreamAPI::proxyFunction(&EOS_Connect_GetLoggedInUserByIndex, "EOS_Connect_GetLoggedInUserByIndex");
    return Intercept::Connect_GetLoggedInUserByIndex(original, Handle, Index);
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Connect_AddNotifyLoginStatusChanged(EOS_HConnect Handle, const EOS_Connect_AddNotifyLoginStatusChangedOptions* Options, void* ClientData, const EOS_Connect_OnLoginStatusChangedCallback NotificationFn){
    static auto original = ScreamAPI::proxyFunction(&EOS_Connect_AddNotifyLoginStatusChanged, "EOS_Connect_AddNotifyLoginStatusChanged");
    return Intercept::Connect_AddNotifyLoginStatusChanged(original, Handle, Options, ClientData, NotificationFn);
}
