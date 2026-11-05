#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "GameFramework/Actor.h"
#include "PersonaBattleTypes.h"
#include "PersonaBattleTransport.h"
#include "PersonaBattle.generated.h"

class APersonaPlayerController;
class APersonaCharacter;
class UPersonaSectorSave;
class FPersonaBattleTransport;

UCLASS()
class PERSONATRIAL_API APersonaBattleState : public AActor
{
    GENERATED_BODY()
public:
    APersonaBattleState();
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    UPROPERTY(Replicated) EPersonaBattlePhase Phase=EPersonaBattlePhase::Idle;
    UPROPERTY(Replicated) FString Status;
    UPROPERTY(Replicated) FString Opponent;
    UPROPERTY(Replicated) bool Open=false;
    UPROPERTY(Replicated) int32 Ready=0;
    UPROPERTY(Replicated) int32 GroupSize=0;
    UPROPERTY(Replicated) int32 Winner=-1;
    static APersonaBattleState* Get(UWorld* World);
};

UCLASS()
class PERSONATRIAL_API UPersonaBattleSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float Delta) override;
    virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(PersonaBattle,STATGROUP_Tickables); }
    virtual UWorld* GetTickableGameObjectWorld() const override { return GetWorld(); }
    virtual bool IsTickable() const override { return !IsTemplate(); }
    void HubReady();
    void SetOpen(APersonaPlayerController* PC,bool Open);
    void Find(bool FriendsOnly=false);
    void Challenge(int32 Index);
    void Answer(APersonaPlayerController* PC,bool Accept);
    void SetReady(APersonaPlayerController* PC,bool Ready);
    bool UseLift(APersonaCharacter* C);
    bool Locked() const;
    bool ClosingCabin() const;
    bool IsArena() const;
    bool Participating() const { return Journal && Phase!=EPersonaBattlePhase::Idle; }
    bool HandleFailure();
    FString Owner() const;
    FString ControlEndpoint() const;
    void AcceptTicket(const FPersonaBattleTicket& Ticket,int32 Team);
    void AcknowledgeTicket(APersonaPlayerController* PC,const FString& Match);
    void ConnectArena();
    void ReceiveSettlement(const FPersonaBattleSettlement& Result,const FString& Hash,bool Commit);
    void ReceiveReceiptStatus(const FString& Match,const FString& Hash,bool Commit);
    void Settle(const FPersonaBattleSettlement& Result);
    void Return(bool Aborted);
    bool RecoverSector(UPersonaSectorSave*& Sector);
    void ArenaStarted();
    void Abort(const FString& Reason);
    void SetPhase(EPersonaBattlePhase Value,const FString& Status);
    FString ManifestHash() const;
    UPROPERTY() TObjectPtr<UPersonaBattleJournal> Journal;
    UPROPERTY() FPersonaBattleTicket Ticket;
    EPersonaBattlePhase Phase=EPersonaBattlePhase::Idle;
    TArray<FPersonaBattleListing> Listings;
    bool FriendsOnly=false;
    bool ReturningHost=false;
    FString ReturningOwner,ReturningSector;
    // Only opt-in Development diagnostics set these crash-test barriers.
    bool bDiagnosticHoldCommit=false,bDiagnosticHoldReturn=false;
private:
    TUniquePtr<FPersonaBattleTransport> Transport;
    TSet<FString> TicketAcks;
    FString Peer,PeerEndpoint;
    bool bPeerManifest=false,bPeerTicketReady=false,bOwnTicketReady=false,bOwnPrepared=false,bLaunching=false,bHostIssued=false;
    double LastAdvertise=0,Deadline=0,NextSearch=0;
    int32 LastGroupSize=-1;
    bool StartTransport();
    bool CanPrepare(FString& Error) const;
    FPersonaBattleParty BuildParty(bool Cargo) const;
    void Receive(const FString& Sender,const FString& Message);
    bool Send(const FString& Kind,const TSharedPtr<FJsonObject>& Payload=nullptr);
    void PrepareParty();
    void DistributeTicket();
    void Launch();
    bool SaveJournal();
    UPersonaBattleJournal* LoadJournal(const FString& Match);
    FString JournalPath(const FString& Match) const;
    bool ApplyResult(bool Aborted);
    void CommitResult();
    void UpdateState(const FString& Status=FString());
    bool ValidateParty(const FPersonaBattleParty& Party,FString& Error) const;
};
