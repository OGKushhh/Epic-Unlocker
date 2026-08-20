#include "pch.h"
#include "eos-sdk/eos_common.h"
#include "ScreamAPI.h"
#include "eos_intercept.h"
#include "util.h"

EOS_DECLARE_FUNC(EOS_Bool) EOS_EpicAccountId_IsValid(EOS_EpicAccountId AccountId){
    static auto original = ScreamAPI::proxyFunction(&EOS_EpicAccountId_IsValid, "EOS_EpicAccountId_IsValid");
    return Intercept::EpicAccountId_IsValid(original, AccountId);
}

EOS_DECLARE_FUNC(const char*) EOS_EResult_ToString(EOS_EResult Result){
    try{
        static auto original = ScreamAPI::proxyFunction(&EOS_EResult_ToString, "EOS_EResult_ToString");
        return Intercept::EResult_ToString(original, Result);
    } catch(ScreamAPI::FunctionNotFoundException){
        return Util::copy_c_string((std::to_string((int)Result).c_str()));
    }
}
