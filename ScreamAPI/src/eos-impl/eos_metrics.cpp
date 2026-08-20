#include "pch.h"
#include "eos-sdk/eos_metrics.h"
#include "ScreamAPI.h"
#include "eos_intercept.h"

EOS_DECLARE_FUNC(EOS_EResult) EOS_Metrics_BeginPlayerSession(EOS_HMetrics Handle, const EOS_Metrics_BeginPlayerSessionOptions* Options){
    static auto original = ScreamAPI::proxyFunction(&EOS_Metrics_BeginPlayerSession, "EOS_Metrics_BeginPlayerSession");
    return Intercept::Metrics_BeginPlayerSession(original, Handle, Options);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Metrics_EndPlayerSession(EOS_HMetrics Handle, const EOS_Metrics_EndPlayerSessionOptions* Options){
    static auto original = ScreamAPI::proxyFunction(&EOS_Metrics_EndPlayerSession, "EOS_Metrics_EndPlayerSession");
    return Intercept::Metrics_EndPlayerSession(original, Handle, Options);
}
