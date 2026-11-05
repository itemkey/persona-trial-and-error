#pragma once
#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "PersonaSubjectState.h"
#include "PersonaBattleTypes.generated.h"

UENUM()
enum class EPersonaSessionPurpose : uint8 { Hub, SectorBattle };
UENUM()
enum class EPersonaBattlePhase : uint8 { Idle, Offered, Gathering, Prepared, Travelling, Waiting, Descending, Fighting, Looting, Settling, Returning, Recovering };
UENUM()
enum class EPersonaBattleJournalPhase : uint8 { Prepared, Started, ResultPrepared, Committed, Applied, Aborted };

USTRUCT()
struct FPersonaBattleCargo
{
    GENERATED_BODY()
    UPROPERTY() FPersonaInventoryRecord Item;
    UPROPERTY() FTransform Relative;
};
USTRUCT()
struct FPersonaBattleParty
{
    GENERATED_BODY()
    UPROPERTY() FString Owner;
    UPROPERTY() FString Sector;
    UPROPERTY() FString Title;
    UPROPERTY() TArray<FPersonaSubjectRecord> Subjects;
    UPROPERTY() TArray<FPersonaBattleCargo> Cargo;
};
USTRUCT()
struct FPersonaBattleTicket
{
    GENERATED_BODY()
    UPROPERTY() int32 Protocol=1;
    UPROPERTY() FString Match;
    UPROPERTY() FString Master;
    UPROPERTY() FString Secret;
    UPROPERTY() TArray<FPersonaBattleParty> Parties;
    int32 TeamFor(const FString& Id) const
    { for(int32 T=0;T<Parties.Num();++T) for(const auto& S:Parties[T].Subjects) if(S.Id==Id) return T;return INDEX_NONE; }
    bool Contains(const FString& Id) const { return TeamFor(Id)!=INDEX_NONE; }
};
USTRUCT()
struct FPersonaBattleSettlement
{
    GENERATED_BODY()
    UPROPERTY() FString Match;
    UPROPERTY() FString ManifestHash;
    UPROPERTY() int32 Winner=-1;
    UPROPERTY() TArray<FPersonaSubjectRecord> Survivors;
    UPROPERTY() TArray<FPersonaBattleCargo> Cargo;
    UPROPERTY() TArray<FPersonaBattleCargo> DrawCargo;
};
UCLASS()
class UPersonaBattleJournal : public USaveGame
{
    GENERATED_BODY()
public:
    UPROPERTY() FPersonaBattleTicket Ticket;
    UPROPERTY() FString LocalOwner;
    UPROPERTY() FString LocalSector;
    UPROPERTY() FString PeerEndpoint;
    UPROPERTY() int32 LocalTeam=-1;
    UPROPERTY() EPersonaBattleJournalPhase Phase=EPersonaBattleJournalPhase::Prepared;
    UPROPERTY() TArray<uint8> OriginalSector;
    UPROPERTY() FPersonaBattleSettlement Settlement;
    UPROPERTY() FString ResultHash;
};

struct FPersonaBattleListing
{
    FString Owner, Sector, Title, Endpoint;
    int32 Players=0;
    bool Friend=false;
};
