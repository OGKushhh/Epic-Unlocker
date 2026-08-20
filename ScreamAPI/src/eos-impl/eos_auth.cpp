#include "pch.h"
#include "eos-sdk/eos_auth.h"
#include "ScreamAPI.h"
#include "eos_intercept.h"
#include "achievement_manager.h"
#include <future>

EOS_DECLARE_FUNC(void) EOS_Auth_Login(EOS_HAuth Handle, const EOS_Auth_LoginOptions* Options, void* ClientData, const EOS_Auth_OnLoginCallback CompletionDelegate){
    static auto original = ScreamAPI::proxyFunction(&EOS_Auth_Login, "EOS_Auth_Login");
    Intercept::Auth_Login(original, Handle, Options, ClientData, CompletionDelegate);
}

EOS_DECLARE_FUNC(EOS_EpicAccountId) EOS_Auth_GetLoggedInAccountByIndex(EOS_HAuth Handle, int32_t Index){
    static auto original = ScreamAPI::proxyFunction(&EOS_Auth_GetLoggedInAccountByIndex, "EOS_Auth_GetLoggedInAccountByIndex");
    return Intercept::Auth_GetLoggedInAccountByIndex(original, Handle, Index);
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Auth_AddNotifyLoginStatusChanged(EOS_HAuth Handle, const EOS_Auth_AddNotifyLoginStatusChangedOptions* Options, void* ClientData, const EOS_Auth_OnLoginStatusChangedCallback NotificationFn){
    static auto original = ScreamAPI::proxyFunction(&EOS_Auth_AddNotifyLoginStatusChanged, "EOS_Auth_AddNotifyLoginStatusChanged");
    return Intercept::Auth_AddNotifyLoginStatusChanged(original, Handle, Options, ClientData, NotificationFn);
}
