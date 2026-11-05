#pragma once
#include "CoreMinimal.h"

// Reliable control traffic is independent of the travelling game connection.
// Steam messages authenticate the remote owner; LAN tests use framed TCP.
class FPersonaBattleTransport
{
public:
    FPersonaBattleTransport();
    ~FPersonaBattleTransport();
    bool Start(bool Steam,int32 Port);
    void Tick(TFunctionRef<void(const FString&,const FString&)> Receive);
    bool Send(const FString& Endpoint,const FString& Owner,const FString& Message);
    FString Endpoint(const FString& Owner) const;
    void Allow(const FString& Owner,bool Open);
private:
    struct FImpl;
    TUniquePtr<FImpl> Impl;
};
