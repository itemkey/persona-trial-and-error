#include "PersonaBattleTransport.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
THIRD_PARTY_INCLUDES_START
#include "steam/steam_api.h"
#include "steam/isteamnetworkingmessages.h"
THIRD_PARTY_INCLUDES_END

namespace { constexpr int32 Channel=24101, Limit=1024*1024, PacketChunkBytes=24*1024; }
struct FPersonaBattleTransport::FImpl
{
    bool Steam=false,Open=false;
    int32 Port=0;
    TSet<FString> Owners;
    FSocket* Listener=nullptr;
    struct FConnection { FSocket* Socket=nullptr;TArray<uint8> In,Out;int32 Sent=0;double Started=0; };
    TArray<FConnection> Connections;
    struct FAssembly { int32 Count=0,Total=0;double Started=0;TMap<int32,TArray<uint8>> Parts; };
    TMap<FString,FAssembly> Assemblies;
    TUniquePtr<CCallback<FImpl,SteamNetworkingMessagesSessionRequest_t>> Requests;
    void Request(SteamNetworkingMessagesSessionRequest_t* E)
    {
        const FString Owner=FString::Printf(TEXT("%llu"),E->m_identityRemote.GetSteamID64());
        if(Open || Owners.Contains(Owner)) SteamNetworkingMessages()->AcceptSessionWithUser(E->m_identityRemote);
    }
    void Close(FSocket*& S) { if(S) { S->Close();ISocketSubsystem::Get()->DestroySocket(S);S=nullptr; } }
    void Deliver(const FString& From,const TArray<uint8>& Data,TFunctionRef<void(const FString&,const FString&)> Receive)
    {
        if(Data.IsEmpty() || Data.Num()>Limit) return;
        FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Data.GetData()),Data.Num());
        TSharedPtr<FJsonObject> Envelope;
        if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(Text.Length(),Text.Get())),Envelope) || !Envelope) return;
        FString Owner,Body;
        if(!Envelope->TryGetStringField(TEXT("owner"),Owner) || !Envelope->TryGetStringField(TEXT("body"),Body)) return;
        if(Steam && Owner!=From) return;
        if(!Open && !Owners.Contains(Owner)) return;
        Receive(Owner,Body);
    }
};
FPersonaBattleTransport::FPersonaBattleTransport() : Impl(MakeUnique<FImpl>()) {}
FPersonaBattleTransport::~FPersonaBattleTransport()
{
    Impl->Requests.Reset();Impl->Close(Impl->Listener);
    for(auto& C:Impl->Connections) Impl->Close(C.Socket);
}
bool FPersonaBattleTransport::Start(bool Steam,int32 Port)
{
    Impl->Steam=Steam;Impl->Port=Port;
    if(Steam)
    {
        if(!SteamNetworkingMessages()) return false;
        Impl->Requests=MakeUnique<CCallback<FImpl,SteamNetworkingMessagesSessionRequest_t>>(Impl.Get(),&FImpl::Request);
        return true;
    }
    auto SS=ISocketSubsystem::Get();Impl->Listener=SS->CreateSocket(NAME_Stream,TEXT("PERSONA sector control"),false);
    if(!Impl->Listener) return false;
    Impl->Listener->SetNonBlocking(true);Impl->Listener->SetReuseAddr(false);
    auto Address=SS->CreateInternetAddr();Address->SetAnyAddress();Address->SetPort(Port);
    if(!Impl->Listener->Bind(*Address) || !Impl->Listener->Listen(8)) { Impl->Close(Impl->Listener);return false; }
    return true;
}
void FPersonaBattleTransport::Allow(const FString& Owner,bool Open)
{ if(!Owner.IsEmpty()) Impl->Owners.Add(Owner);Impl->Open=Open; }
FString FPersonaBattleTransport::Endpoint(const FString& Owner) const
{
    if(Impl->Steam) return TEXT("steam:")+Owner;
    bool BindAll=false;const auto Address=ISocketSubsystem::Get()->GetLocalHostAddr(*GLog,BindAll);
    return Address->ToString(false)+FString::Printf(TEXT(":%d"),Impl->Port);
}
bool FPersonaBattleTransport::Send(const FString& Endpoint,const FString& Owner,const FString& Message)
{
    auto Envelope=MakeShared<FJsonObject>();Envelope->SetStringField(TEXT("owner"),Owner);Envelope->SetStringField(TEXT("body"),Message);
    FString Text;FJsonSerializer::Serialize(Envelope,TJsonWriterFactory<>::Create(&Text));
    FTCHARToUTF8 Utf(*Text);if(Utf.Length()>Limit) return false;
    TArray<uint8> Data;Data.Append(reinterpret_cast<const uint8*>(Utf.Get()),Utf.Length());
    if(Impl->Steam)
    {
        if(!Endpoint.StartsWith(TEXT("steam:")) || !SteamNetworkingMessages()) return false;
        SteamNetworkingIdentity Peer;Peer.SetSteamID64(FCString::Strtoui64(*Endpoint.Mid(6),nullptr,10));
        const int32 Count=FMath::DivideAndRoundUp(Data.Num(),PacketChunkBytes);const FGuid Id=FGuid::NewGuid();
        for(int32 I=0;I<Count;++I)
        {
            TArray<uint8> Packet;FMemoryWriter W(Packet);uint32 Magic=0x50424D31;FGuid Guid=Id;int32 N=Count,Index=I,Total=Data.Num();
            W<<Magic<<Guid<<N<<Index<<Total;const int32 Bytes=FMath::Min(PacketChunkBytes,Total-I*PacketChunkBytes);W.Serialize(Data.GetData()+I*PacketChunkBytes,Bytes);
            if(SteamNetworkingMessages()->SendMessageToUser(Peer,Packet.GetData(),Packet.Num(),k_nSteamNetworkingSend_Reliable,Channel)!=k_EResultOK) return false;
        }
        return true;
    }
    if(Impl->Connections.Num()>=16) return false;
    FString Host,PortText;if(!Endpoint.Split(TEXT(":"),&Host,&PortText,ESearchCase::CaseSensitive,ESearchDir::FromEnd)) return false;
    auto SS=ISocketSubsystem::Get();auto Address=SS->CreateInternetAddr();bool Valid=false;Address->SetIp(*Host,Valid);Address->SetPort(FCString::Atoi(*PortText));if(!Valid) return false;
    FImpl::FConnection C;C.Socket=SS->CreateSocket(NAME_Stream,TEXT("PERSONA sector message"),false);if(!C.Socket) return false;
    C.Socket->SetNonBlocking(true);C.Socket->SetNoDelay(true);C.Started=FPlatformTime::Seconds();
    const bool Connected=C.Socket->Connect(*Address);const auto Error=SS->GetLastErrorCode();
    if(!Connected && Error!=SE_EWOULDBLOCK && Error!=SE_EINPROGRESS && Error!=SE_NO_ERROR) { Impl->Close(C.Socket);return false; }
    FMemoryWriter W(C.Out);int32 Size=Data.Num();W<<Size;W.Serialize(Data.GetData(),Data.Num());Impl->Connections.Add(MoveTemp(C));return true;
}
void FPersonaBattleTransport::Tick(TFunctionRef<void(const FString&,const FString&)> Receive)
{
    const double Now=FPlatformTime::Seconds();
    if(Impl->Steam)
    {
        if(!SteamNetworkingMessages()) return;
        SteamNetworkingMessage_t* Messages[32];int32 Count=SteamNetworkingMessages()->ReceiveMessagesOnChannel(Channel,Messages,UE_ARRAY_COUNT(Messages));
        for(int32 I=0;I<Count;++I)
        {
            auto M=Messages[I];TArray<uint8> Packet;Packet.Append(static_cast<uint8*>(M->m_pData),M->m_cbSize);
            const FString From=FString::Printf(TEXT("%llu"),M->m_identityPeer.GetSteamID64());M->Release();
            if(Packet.Num()<32 || Packet.Num()>PacketChunkBytes+32 || (!Impl->Open && !Impl->Owners.Contains(From))) continue;
            FMemoryReader R(Packet);uint32 Magic;FGuid Id;int32 N,Index,Total;R<<Magic<<Id<<N<<Index<<Total;
            if(Magic!=0x50424D31 || N<1 || N>FMath::DivideAndRoundUp(Limit,PacketChunkBytes) || Index<0 || Index>=N || Total<1 || Total>Limit || N!=FMath::DivideAndRoundUp(Total,PacketChunkBytes)) continue;
            const int32 Bytes=FMath::Min(PacketChunkBytes,Total-Index*PacketChunkBytes);if(Packet.Num()-R.Tell()!=Bytes) continue;
            const FString Key=From+Id.ToString();if(!Impl->Assemblies.Contains(Key) && Impl->Assemblies.Num()>=16) continue;
            auto& A=Impl->Assemblies.FindOrAdd(Key);if(!A.Started) { A.Started=Now;A.Count=N;A.Total=Total; }
            if(A.Count!=N || A.Total!=Total) continue;
            TArray<uint8> Part;Part.SetNumUninitialized(Bytes);R.Serialize(Part.GetData(),Bytes);A.Parts.Add(Index,MoveTemp(Part));
            if(A.Parts.Num()==N)
            {
                TArray<uint8> Data;Data.Reserve(Total);for(int32 J=0;J<N;++J) Data.Append(A.Parts[J]);Impl->Assemblies.Remove(Key);Impl->Deliver(From,Data,Receive);
            }
        }
        for(auto It=Impl->Assemblies.CreateIterator();It;++It) if(Now-It->Value.Started>30) It.RemoveCurrent();
        return;
    }
    if(Impl->Listener)
    {
        bool Pending=false;
        while(Impl->Connections.Num()<16 && Impl->Listener->HasPendingConnection(Pending) && Pending)
        {
            FImpl::FConnection C;C.Socket=Impl->Listener->Accept(TEXT("PERSONA incoming sector"));if(!C.Socket) break;
            C.Socket->SetNonBlocking(true);C.Socket->SetNoDelay(true);C.Started=Now;Impl->Connections.Add(MoveTemp(C));
        }
    }
    for(int32 I=Impl->Connections.Num()-1;I>=0;--I)
    {
        TArray<uint8> Delivered;
        auto& C=Impl->Connections[I];bool Close=Now-C.Started>12 || C.Socket->GetConnectionState()==SCS_ConnectionError;
        if(!Close && C.Socket->GetConnectionState()==SCS_Connected)
        {
            if(C.Sent<C.Out.Num()) { int32 Sent=0;C.Socket->Send(C.Out.GetData()+C.Sent,C.Out.Num()-C.Sent,Sent);C.Sent+=Sent; }
            uint32 Available=0;
            while(C.Socket->HasPendingData(Available) && Available>0 && C.In.Num()<=Limit+4)
            { const int32 At=C.In.Num(),N=FMath::Min<int32>(Available,65536);C.In.AddUninitialized(N);int32 Read=0;C.Socket->Recv(C.In.GetData()+At,N,Read);C.In.SetNum(At+Read);if(!Read) break; }
            if(C.In.Num()>=4)
            {
                int32 Size;FMemory::Memcpy(&Size,C.In.GetData(),4);
                if(Size<1 || Size>Limit) Close=true;
                else if(C.In.Num()>=Size+4) { Delivered.Append(C.In.GetData()+4,Size);Close=true; }
            }
            if(!C.Out.IsEmpty() && C.Sent==C.Out.Num()) Close=true;
        }
        if(Close) { Impl->Close(C.Socket);Impl->Connections.RemoveAtSwap(I); }
        // A handler can reply immediately, growing Connections. Finish all use
        // of its element reference before invoking application code.
        if(!Delivered.IsEmpty()) Impl->Deliver(FString(),Delivered,Receive);
    }
}
