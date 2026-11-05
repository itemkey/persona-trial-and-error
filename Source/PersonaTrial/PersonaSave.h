#pragma once
#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Engine/LocalPlayer.h"
#include "Tickable.h"
#include "PersonaItemState.h"
#include "PersonaCombat.h"
#include "PersonaHub.h"
#include "PersonaSubjectState.h"
#include "PersonaSave.generated.h"

USTRUCT()
struct FPersonaObjectRecord
{
    GENERATED_BODY()
    UPROPERTY() FString Id;
    UPROPERTY() FTransform Transform;
    UPROPERTY() FVector Velocity = FVector::ZeroVector;
    UPROPERTY() FVector AngularVelocity = FVector::ZeroVector;
    UPROPERTY() FString Holder;
    UPROPERTY() bool Open = false;
    UPROPERTY() bool Sleeping = false;
    UPROPERTY() FString ActorClass;
    UPROPERTY() FString Definition;
    UPROPERTY() int32 Quantity = 1;
    UPROPERTY() FPersonaWeaponState Weapon;
    UPROPERTY() FString ContainerState;
};
USTRUCT()
struct FPersonaHubRecord
{
    GENERATED_BODY()
    UPROPERTY() FString Id;
    UPROPERTY() FString Definition;
    UPROPERTY() FTransform Transform;
    UPROPERTY() bool Open=false;
    UPROPERTY() bool Powered=false;
    UPROPERTY() int32 Capacity=12;
    UPROPERTY() TArray<FPersonaInventoryRecord> Items;
    UPROPERTY() TArray<FPersonaCompartment> Compartments;
};
USTRUCT()
struct FPersonaMapRecord
{
    GENERATED_BODY()
    UPROPERTY() int32 InventoryFormat = 1;
    UPROPERTY() FString Map;
    UPROPERTY() TArray<FPersonaSubjectRecord> Subjects;
    UPROPERTY() TArray<FPersonaObjectRecord> Props;
    UPROPERTY() TArray<FPersonaObjectRecord> Doors;
    UPROPERTY() TArray<FPersonaHubRecord> Hub;
};
UCLASS()
class PERSONATRIAL_API UPersonaSectorSave : public USaveGame
{
    GENERATED_BODY()
public:
    // Keep the legacy default stable: tagged saves can omit default-valued
    // properties. NewSector/SaveWorld explicitly write the current version.
    UPROPERTY() int32 Version = 3;
    UPROPERTY() FPersonaRunRecord Run;
    UPROPERTY() TArray<FForestResult> ForestResults;
    UPROPERTY() TArray<FNorthChartTile> Atlas;
    UPROPERTY() TMap<FIntPoint,float> Harvest;
    UPROPERTY() TArray<FNorthDrop> ForestDrops;
    UPROPERTY() int64 ForestAvailableAt = 0;
    UPROPERTY() FString Slot;
    UPROPERTY() FString Title;
    UPROPERTY() FDateTime Updated;
    UPROPERTY() FString CurrentMap = TEXT("/Game/Persona/Maps/L_Hub");
    UPROPERTY() bool Teams = false;
    UPROPERTY() bool FriendlyFire = true;
    UPROPERTY() TArray<FPersonaMapRecord> Maps;
    UPROPERTY() TArray<FPersonaSubjectRecord> Players;
    UPROPERTY() FString PendingBattle;
    UPROPERTY() TArray<FString> AppliedBattles;
};
UCLASS()
class PERSONATRIAL_API UPersonaLocalPlayer : public ULocalPlayer
{
    GENERATED_BODY()
public:
    virtual FString GetGameLoginOptions() const override;
};
UCLASS()
class PERSONATRIAL_API UPersonaSaveSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float Delta) override;
    virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(PersonaSave, STATGROUP_Tickables); }
    virtual UWorld* GetTickableGameObjectWorld() const override { return GetWorld(); }
    virtual bool IsTickable() const override { return !IsTemplate(); }
    bool NewSector(bool Teams = false);
    bool SelectSector(const FString& Slot);
    TArray<UPersonaSectorSave*> ListSectors();
    bool SaveWorld();
    bool WriteSelectedSector() { return Write(); }
    static FPersonaSubjectRecord CaptureSubject(class APersonaCharacter* Character);
    static void RestoreSubject(class APersonaCharacter* Character,const FPersonaSubjectRecord& Record,bool Inventory=true);
    bool RestoreWorld(class APersonaGameMode* Mode);
    bool Rejoin(class APlayerController* Player);
    void RestorePlayerInventory(class APersonaCharacter* Character);
    void RestoreHubObjects();
    void PrepareTravel();
    void ResumeWorldSaving() { bTravel=false; }
    FString LocalIdentity();
    UPROPERTY() TObjectPtr<UPersonaSectorSave> Active;
    UPROPERTY() TArray<TObjectPtr<UPersonaSectorSave>> Listed;
    FString LastError;
private:
    UPersonaSectorSave* Read(const FString& Path);
    bool Write();
    FString Directory() const;
    FString ProfileId;
    float AutosaveElapsed = 0;
    bool bTravel = false;
    FDelegateHandle TearDownHandle;
    void WorldTearingDown(UWorld* World);
};
