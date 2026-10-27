#pragma once
#include "CoreMinimal.h"
#include "PersonaInteraction.h"
#include "PersonaItemState.h"
#include "PersonaHub.generated.h"

UENUM(BlueprintType)
enum class EPersonaHubKind : uint8 { Storage, Switch, Stove, Sink, Vice, Workbench, Terminal, Lift, Gate, Screen };
USTRUCT(BlueprintType)
struct FPersonaHubPart
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere) TObjectPtr<UStaticMesh> Mesh;
    UPROPERTY(EditAnywhere) FVector Offset=FVector::ZeroVector;
    UPROPERTY(EditAnywhere) FRotator ClosedRotation=FRotator::ZeroRotator;
    UPROPERTY(EditAnywhere) FRotator OpenRotation=FRotator::ZeroRotator;
    UPROPERTY(EditAnywhere) FVector OpenOffset=FVector::ZeroVector;
};
UCLASS(BlueprintType)
class PERSONATRIAL_API UPersonaHubAsset : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere) TObjectPtr<UStaticMesh> Mesh;
    UPROPERTY(EditAnywhere) TArray<FPersonaHubPart> Parts;
    UPROPERTY(EditAnywhere) EPersonaHubKind Kind=EPersonaHubKind::Storage;
    UPROPERTY(EditAnywhere) FText Label;
};
UCLASS()
class PERSONATRIAL_API APersonaHubObject : public AActor, public IPersonaInteractable
{
    GENERATED_BODY()
public:
    APersonaHubObject();
    virtual void BeginPlay() override;
    virtual void Tick(float Delta) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    virtual FText InteractionLabel_Implementation() const override;
    virtual void Interact_Implementation(APersonaCharacter* Character) override;
    UFUNCTION(BlueprintCallable) void Configure(UPersonaHubAsset* Asset);
    UPROPERTY(EditAnywhere, ReplicatedUsing=OnRep_Definition, BlueprintReadOnly) TObjectPtr<UPersonaHubAsset> Definition;
    UFUNCTION() void OnRep_Definition() { Configure(Definition); }
    UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly) FString PersistentId;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Mesh;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<class UPersonaLoot> Loot;
    UPROPERTY(VisibleAnywhere) TObjectPtr<class UAudioComponent> Audio;
    UPROPERTY(VisibleAnywhere) TObjectPtr<class UPointLightComponent> Light;
    UPROPERTY(VisibleAnywhere) TObjectPtr<class UTextRenderComponent> Display;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Water;
    UPROPERTY(Replicated, BlueprintReadOnly) bool bOpen=false;
    UPROPERTY(Replicated, BlueprintReadOnly) bool bPowered=false;
    UPROPERTY(Replicated, BlueprintReadOnly) FString Message;
    UPROPERTY(EditAnywhere) bool bTrialObject=false;
private:
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> MovingParts;
    float OpenAlpha=0;
};

UCLASS()
class PERSONATRIAL_API APersonaPlayerBarrier : public AActor
{
    GENERATED_BODY()
public:
    APersonaPlayerBarrier();
    UPROPERTY(VisibleAnywhere) TObjectPtr<class UBoxComponent> Barrier;
};

UENUM(BlueprintType)
enum class EPersonaTrialPhase : uint8 { Offered, Accepted, Wave, Intermission, Extraction, Purge, Failed };
UENUM(BlueprintType)
enum class EPersonaLiftPhase : uint8 { Hub, Descending, Arena, Ascending };
USTRUCT()
struct FPersonaCargoRecord
{
    GENERATED_BODY()
    UPROPERTY() FString Id;
    UPROPERTY() FTransform Relative;
};
USTRUCT()
struct FPersonaRunRecord
{
    GENERATED_BODY()
    UPROPERTY() FString Id;
    UPROPERTY() EPersonaTrialPhase Phase=EPersonaTrialPhase::Offered;
    UPROPERTY() EPersonaLiftPhase Lift=EPersonaLiftPhase::Hub;
    UPROPERTY() int32 Wave=0;
    UPROPERTY() int32 Seed=0;
    UPROPERTY() int32 DifficultyPlayers=1;
    UPROPERTY() int32 PendingNPC=0;
    UPROPERTY() int32 SpawnedNPC=0;
    UPROPERTY() float ReinforcementRemaining=0;
    UPROPERTY() int32 Respect=0;
    UPROPERTY() int32 Penalty=0;
    UPROPERTY() int32 Bonus=0;
    UPROPERTY() bool Rewarded=false;
    UPROPERTY() bool Departed=false;
    UPROPERTY() bool CratesFilled=false;
    UPROPERTY() float Remaining=0;
    UPROPERTY() float LiftProgress=0;
    UPROPERTY() TArray<FString> Participants;
    UPROPERTY() TArray<FPersonaCargoRecord> Cargo;
    UPROPERTY() TArray<FPersonaCargoRecord> Passengers;
    UPROPERTY() TArray<FTransform> Crates;
};
UCLASS()
class PERSONATRIAL_API APersonaTrialDirector : public AActor
{
    GENERATED_BODY()
public:
    APersonaTrialDirector();
    virtual void BeginPlay() override;
    virtual void Tick(float Delta) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    static APersonaTrialDirector* Get(UWorld* World);
    void Decide(class APersonaPlayerController* PC,bool Accept);
    void UseLift(APersonaCharacter* C);
    bool Running() const { return Run.Phase!=EPersonaTrialPhase::Offered; }
    bool InRun(APersonaCharacter* C) const;
    void Capture(FPersonaRunRecord& Out) const;
    void Restore(const FPersonaRunRecord& In);
    FString ScreenText() const;
    UPROPERTY(Replicated) FPersonaRunRecord Run;
    UPROPERTY(EditAnywhere) TObjectPtr<class UPersonaTrialDefinition> Definition;
    UPROPERTY(EditAnywhere) FVector HubSpawn=FVector(-180,1100,100);
    UPROPERTY(EditAnywhere) FVector LiftTop=FVector(0,0,-500);
    UPROPERTY(EditAnywhere) FVector LiftBottom=FVector(0,0,-12600);
    UPROPERTY(EditAnywhere) TArray<FVector> NPCSpawns;
    UPROPERTY(Replicated) int32 RemainingNPC=0;
    UPROPERTY(Replicated) float PhaseEnd=0;
    bool ArenaReady() const;
private:
    bool bRestorePending=false, bLevelRequested=false;
    UPROPERTY(Replicated) float LiftStart=0;
    float UpdateElapsed=0;
    UPROPERTY() TObjectPtr<class UExponentialHeightFogComponent> PurgeFog;
    UPROPERTY() TObjectPtr<class ULevelStreaming> Arena;
    UPROPERTY() TObjectPtr<APersonaHubObject> Platform;
    void EnsureArena();
    void StartWave();
    void SpawnReinforcements();
    float NextReinforcement=0;
    void StartLift(bool Ascend);
    void FinishLift();
    void FinishRun(bool Success);
    void SetupCrates();
    void UpdateGate();
};
