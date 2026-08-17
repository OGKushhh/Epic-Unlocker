#pragma once
#include "pch.h"
#include "Config.h"
#include "eos_resolve.h"
#include <Overlay_types.h>
#include <eos-sdk/eos_sdk.h>

namespace ScreamAPI{

extern HMODULE thisDLL;
extern HMODULE originalDLL;
void init(HMODULE hModule);
void destroy();

class FunctionNotFoundException : public std::exception{};

// Have to define template function in header (-_-)
template <typename RetType, typename... ArgTypes>
struct proxyTraits{
	using funcType = RetType(EOS_CALL*)(ArgTypes...);
};

// A function that returns a type-safe reference to the requested EOS SDK function.
// Delegates to EOS_Resolve::resolve so 32-bit __stdcall decoration is handled
// by a single shared walker (was previously a single-guess _Name@4*N that
// broke if any parameter wasn't 4 bytes).
template <typename RetType, typename... ArgTypes>
auto proxyFunction(RetType(EOS_CALL*)(ArgTypes...), LPCSTR rawFunctionName){
	using funcType = typename proxyTraits<RetType, ArgTypes...>::funcType;
	auto funcPtr = EOS_Resolve::resolve(originalDLL, rawFunctionName);
	if(funcPtr){
		Logger::debug("Successfully proxied function: %s", rawFunctionName);
		return reinterpret_cast<funcType>(funcPtr);
	} else{
		Logger::error("Failed to proxy function: %s", rawFunctionName);
		throw FunctionNotFoundException();
	}

}

struct OriginalDataContainer{
	void* originalClientData;
	void (EOS_CALL *originalCompletionDelegate)(const void*);
	OriginalDataContainer(void* clientData, void* completionDelegate) {
		originalClientData = clientData;
		reinterpret_cast<void*&>(originalCompletionDelegate) = completionDelegate;
	}
};

template <typename T>
void proxyCallback(const T* Data, void* const* clientData, std::function<void(T*)> customCallback){
	auto container = reinterpret_cast<OriginalDataContainer*>(*clientData);

	// Restore original client data
	auto mClientData = const_cast<void**>(clientData);
	*mClientData = container->originalClientData;

	// Call our custom callback
	T* mData = const_cast<T*>(Data);
	customCallback(mData);

	// Call original completion delegate with our modified data
	container->originalCompletionDelegate(Data);

	// Free the heap
	delete container;
}

#define EOS_IMPLEMENT_FUNC(function, ...)								\
	Logger::debug(__func__);											\
	static auto proxy = ScreamAPI::proxyFunction(&function, __func__);	\
	return proxy(__VA_ARGS__);

}
