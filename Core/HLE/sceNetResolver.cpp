// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#if __linux__ || __APPLE__ || defined(__OpenBSD__)
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
#endif

// TODO: fixme move Core/Net to Common/Net
#include "Common/Net/Resolve.h"
#include "Core/Net/InetCommon.h"
#include "Common/Data/Text/Parsers.h"

#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/Serialize/SerializeMap.h"
#include "Core/Config.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/MIPS/MIPS.h"
#include "Core/Config.h"
#include "Core/MemMapHelpers.h"
#include "Core/Util/PortManager.h"

#include "sceKernel.h"
#include "sceKernelThread.h"
#include "sceKernelMutex.h"
#include "sceUtility.h"

#include "Core/HLE/proAdhoc.h"
#include "Core/HLE/sceNetResolver.h"

#include <iostream>
#include <shared_mutex>

#include "sceNet.h"
#include "Core/HLE/sceNp.h"
#include "Core/Reporting.h"
#include "Core/Instance.h"
#include "Core/Net/InetSocket.h"

#if PPSSPP_PLATFORM(SWITCH) && !defined(INADDR_NONE)
// Missing toolchain define
#define INADDR_NONE 0xFFFFFFFF
#endif

static int sceNetResolverInit() {
    ERROR_LOG(SCENET, "UNTESTED %s()", __FUNCTION__);
    g_Config.mHostToAlias["socomftb2.psp.online.scea.com"] = "67.222.156.250";
    g_Config.mHostToAlias["socompsp-prod.muis.pdonline.scea.com"] = "67.222.156.250";
    SceNetResolver::Init();
    return 0;
}

static int sceNetResolverTerm() {
    ERROR_LOG(SCENET, "UNTESTED %s()", __FUNCTION__);
    SceNetResolver::Shutdown();
    return 0;
}

// Note: timeouts are in seconds
int NetResolver_StartNtoA(u32 resolverId, u32 hostnamePtr, u32 inAddrPtr, int timeout, int retry) {
    auto sceNetResolver = SceNetResolver::Get();
    if (!sceNetResolver) {
        return hleLogError(SCENET, ERROR_NET_RESOLVER_STOPPED, "Net Resolver Subsystem Shutdown: Resolver %i",
                           resolverId);
    }

    const auto resolver = sceNetResolver->GetNetResolver(resolverId);
    if (resolver == nullptr) {
        return hleLogError(SCENET, ERROR_NET_RESOLVER_BAD_ID, "Bad Resolver Id: %i", resolverId);
    }

    addrinfo* resolved = nullptr;
    std::string err, hostname = std::string(safe_string(Memory::GetCharPointer(hostnamePtr)));
    SockAddrIN4 addr{};
    addr.in.sin_addr.s_addr = INADDR_NONE;

    // Flag resolver as in-progress - not relevant for sync functions but potentially relevant for async
    resolver->SetIsRunning(true);

    // Resolve any aliases
    if (g_Config.mHostToAlias.find(hostname) != g_Config.mHostToAlias.end()) {
        const std::string& alias = g_Config.mHostToAlias[hostname];
        INFO_LOG(SCENET, "%s - Resolved alias %s from hostname %s", __FUNCTION__, alias.c_str(), hostname.c_str());
        hostname = alias;
    }

    // Attempt to execute a DNS resolution
    if (!net::DNSResolve(hostname, "", &resolved, err)) {
        // TODO: Return an error based on the outputted "err" (unfortunately it's already converted to string)
        return hleLogError(SCENET, ERROR_NET_RESOLVER_INVALID_HOST, "DNS Error Resolving %s (%s)\n", hostname.c_str(),
                           err.c_str());
    }

    // If successful, write to memory
    if (resolved != nullptr) {
        for (auto ptr = resolved; ptr != nullptr; ptr = ptr->ai_next) {
            switch (ptr->ai_family) {
                case AF_INET:
                    addr.in = *(sockaddr_in *) ptr->ai_addr;
                    break;
            }
        }
        net::DNSResolveFree(resolved);

        Memory::Write_U32(addr.in.sin_addr.s_addr, inAddrPtr);
        INFO_LOG(SCENET, "%s - Hostname: %s => IPv4: %s", __FUNCTION__, hostname.c_str(),
                 ip2str(addr.in.sin_addr, false).c_str());
    }

    // Flag resolver as complete
    resolver->SetIsRunning(false);
    return 0;
}

static int sceNetResolverStartNtoA(int resolverId, u32 hostnamePtr, u32 inAddrPtr, int timeout, int retry) {
    for (int attempt = 0; attempt < retry; ++attempt) {
        if (const int status = NetResolver_StartNtoA(resolverId, hostnamePtr, inAddrPtr, timeout, retry); status >= 0) {
            return status;
        }
    }
    return -1;
}

static int sceNetResolverStartNtoAAsync(int resolverId, u32 hostnamePtr, u32 inAddrPtr, int timeout, int retry) {
    ERROR_LOG_REPORT_ONCE(sceNetResolverStartNtoAAsync, SCENET, "UNIMPL %s(%d, %08x, %08x, %d, %d) at %08x",
                          __FUNCTION__, resolverId, hostnamePtr, inAddrPtr, timeout, retry, currentMIPS->pc);
    return NetResolver_StartNtoA(resolverId, hostnamePtr, inAddrPtr, timeout, retry);
}

static int sceNetResolverPollAsync(int resolverId, u32 unknown) {
    ERROR_LOG_REPORT_ONCE(sceNetResolverPollAsync, SCENET, "UNIMPL %s(%d, %08x) at %08x", __FUNCTION__, resolverId,
                          unknown, currentMIPS->pc);
    // TODO: Implement after confirming that this returns the state of resolver.isRunning
    return 0;
}

static int sceNetResolverWaitAsync(int resolverId, u32 unknown) {
    ERROR_LOG_REPORT_ONCE(sceNetResolverWaitAsync, SCENET, "UNIMPL %s(%d, %08x) at %08x", __FUNCTION__, resolverId,
                          unknown, currentMIPS->pc);
    // TODO: Implement after confirming that this blocks current thread until resolver.isRunning flips to false
    return 0;
}

static int sceNetResolverStartAtoN(int resolverId, u32 inAddr, u32 hostnamePtr, int hostnameLength, int timeout,
                                   int retry) {
    ERROR_LOG_REPORT_ONCE(sceNetResolverStartAtoN, SCENET, "UNIMPL %s(%d, %08x[%s], %08x, %i, %i, %i) at %08x",
                          __FUNCTION__, resolverId, inAddr, ip2str(*(in_addr*)&inAddr, false).c_str(), hostnamePtr,
                          hostnameLength, timeout, retry, currentMIPS->pc);
    // TODO: Implement via getnameinfo
    return 0;
}

static int sceNetResolverStartAtoNAsync(int resolverId, u32 inAddr, u32 hostnamePtr, int hostnameLength, int timeout,
                                        int retry) {
    ERROR_LOG_REPORT_ONCE(sceNetResolverStartAtoNAsync, SCENET, "UNIMPL %s(%d, %08x[%s], %08x, %i, %i, %i) at %08x",
                          __FUNCTION__, resolverId, inAddr, ip2str(*(in_addr*)&inAddr, false).c_str(), hostnamePtr,
                          hostnameLength, timeout, retry, currentMIPS->pc);
    return 0;
}

static int sceNetResolverCreate(u32 resolverIdPtr, u32 bufferPtr, int bufferLen) {
    WARN_LOG(SCENET, "UNTESTED %s(%08x[%d], %08x, %d) at %08x", __FUNCTION__, resolverIdPtr,
             Memory::Read_U32(resolverIdPtr), bufferPtr, bufferLen, currentMIPS->pc);
    if (!Memory::IsValidRange(resolverIdPtr, 4))
        return hleLogError(SCENET, ERROR_NET_RESOLVER_INVALID_PTR, "Invalid Ptr: %08x", resolverIdPtr);

    if (Memory::IsValidRange(bufferPtr, 4) && bufferLen < 1)
        return hleLogError(SCENET, ERROR_NET_RESOLVER_INVALID_BUFLEN, "Invalid Buffer Length: %i", bufferLen);

    auto sceNetResolver = SceNetResolver::Get();
    if (!sceNetResolver)
        return hleLogError(SCENET, ERROR_NET_RESOLVER_STOPPED, "Resolver Subsystem Stopped");

    const auto resolver = sceNetResolver->CreateNetResolver(bufferPtr, bufferLen);

    Memory::Write_U32(resolver->GetId(), resolverIdPtr);
    return 0;
}

static int sceNetResolverStop(u32 resolverId) {
    auto sceNetResolver = SceNetResolver::Get();
    if (!sceNetResolver) {
        return hleLogError(SCENET, ERROR_NET_RESOLVER_STOPPED, "Resolver Subsystem Stopped (Resolver Id: %i)",
                           resolverId);
    }

    const auto resolver = sceNetResolver->GetNetResolver(resolverId);

    WARN_LOG(SCENET, "UNTESTED %s(%d) at %08x", __FUNCTION__, resolverId, currentMIPS->pc);
    if (resolver == nullptr)
        return hleLogError(SCENET, ERROR_NET_RESOLVER_BAD_ID, "Bad Resolver Id: %i", resolverId);

    if (!resolver->GetIsRunning())
        return hleLogError(SCENET, ERROR_NET_RESOLVER_ALREADY_STOPPED, "Resolver Already Stopped (Id: %i)", resolverId);

    resolver->SetIsRunning(false);
    return 0;
}

static int sceNetResolverDelete(u32 resolverId) {
    WARN_LOG(SCENET, "UNTESTED %s(%d) at %08x", __FUNCTION__, resolverId, currentMIPS->pc);

    auto sceNetResolver = SceNetResolver::Get();
    if (!sceNetResolver)
        return hleLogError(SCENET, ERROR_NET_RESOLVER_STOPPED, "Resolver Subsystem Stopped (Resolver Id: %i)",
                           resolverId);

    if (!sceNetResolver->DeleteNetResolver(resolverId))
        return hleLogError(SCENET, ERROR_NET_RESOLVER_BAD_ID, "Bad Resolver Id: %i", resolverId);

    return 0;
}

const HLEFunction sceNetResolver[] = {
    {0X224C5F44, &WrapI_IUUII<sceNetResolverStartNtoA>, "sceNetResolverStartNtoA", 'i', "ixxii"},
    {0X244172AF, &WrapI_UUI<sceNetResolverCreate>, "sceNetResolverCreate", 'i', "xxi"},
    {0X94523E09, &WrapI_U<sceNetResolverDelete>, "sceNetResolverDelete", 'i', "i"},
    {0XF3370E61, &WrapI_V<sceNetResolverInit>, "sceNetResolverInit", 'i', ""},
    {0X808F6063, &WrapI_U<sceNetResolverStop>, "sceNetResolverStop", 'i', "i"},
    {0X6138194A, &WrapI_V<sceNetResolverTerm>, "sceNetResolverTerm", 'i', ""},
    {0X629E2FB7, &WrapI_IUUIII<sceNetResolverStartAtoN>, "sceNetResolverStartAtoN", 'i', "ixxiii"},
    {0X14C17EF9, &WrapI_IUUII<sceNetResolverStartNtoAAsync>, "sceNetResolverStartNtoAAsync", 'i', "ixxii"},
    {0XAAC09184, &WrapI_IUUIII<sceNetResolverStartAtoNAsync>, "sceNetResolverStartAtoNAsync", 'i', "ixxiii"},
    {0X12748EB9, &WrapI_IU<sceNetResolverWaitAsync>, "sceNetResolverWaitAsync", 'i', "ix"},
    {0X4EE99358, &WrapI_IU<sceNetResolverPollAsync>, "sceNetResolverPollAsync", 'i', "ix"},
};

std::shared_ptr<SceNetResolver> SceNetResolver::gInstance;
std::shared_mutex SceNetResolver::gLock;

void SceNetResolver::Init() {
    auto lock = std::unique_lock(gLock);
    gInstance = std::make_shared<SceNetResolver>();
}

void SceNetResolver::Shutdown() {
    auto lock = std::unique_lock(gLock);
    gInstance = nullptr;
}

std::shared_ptr<SceNetResolver> SceNetResolver::Get() {
    auto lock = std::shared_lock(gLock);
    return gInstance;
}

std::shared_ptr<NetResolver> SceNetResolver::GetNetResolver(u32 resolverId) {
    std::shared_lock lock(mNetResolversLock);
    const auto it = mNetResolvers.find(resolverId);
    return it != mNetResolvers.end() ? it->second : nullptr;
}

std::shared_ptr<NetResolver> SceNetResolver::CreateNetResolver(u32 bufferPtr, u32 bufferLen) {
    // TODO: Consider using SceUidManager instead of this 1-indexed id
    // TODO: Implement ERROR_NET_RESOLVER_ID_MAX (possibly 32?)
    std::unique_lock lock(mNetResolversLock);
    int currentNetResolverId = mCurrentNetResolverId++;
    return mNetResolvers[currentNetResolverId] = std::make_shared<NetResolver>(
       currentNetResolverId, // id
       bufferPtr, // bufferPtr
       bufferLen // bufferLen
   );
}

bool SceNetResolver::TerminateNetResolver(u32 resolverId) {
    const auto netResolver = GetNetResolver(resolverId);
    if (netResolver == nullptr) {
        return false;
    }
    netResolver->SetIsRunning(false);
    return true;
}

bool SceNetResolver::DeleteNetResolver(u32 resolverId) {
    std::unique_lock lock(mNetResolversLock);
    const auto it = mNetResolvers.find(resolverId);
    if (it == mNetResolvers.end()) {
        return false;
    }
    mNetResolvers.erase(it);
    return true;
}

void SceNetResolver::ClearNetResolvers() {
    std::unique_lock lock(mNetResolversLock);
    for (auto &[first, second]: mNetResolvers) {
        second->SetIsRunning(false);
    }
    mNetResolvers.clear();
}

void Register_sceNetResolver() {
    RegisterModule("sceNetResolver", ARRAY_SIZE(sceNetResolver), sceNetResolver);
}
