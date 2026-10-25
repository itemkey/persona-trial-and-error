#pragma once
#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Engine/LocalPlayer.h"
#include "Tickable.h"
#include "PersonaItemState.h"
#include "PersonaHub.h"
#include "PersonaSave.generated.h"

USTRUCT()
struct FPersonaSubjectRecord
{
    GENERATED_BODY()
    UPROPERTY() FString Id;
    UPROPERTY() FString Name;
    UPROPERTY() FTransform Transform;
    UPROPERTY() FVector Velocity = FVector::ZeroVector;
    UPROPERTY() FRotator View = FRotator::ZeroRotator;
    UPROPERTY() float Health = 100;
    UPROPERTY() float MaxHealth = 100;
    UPROPERTY() float DamageScale = 1;
    UPROPERTY() bool Dead = false;
    UPROPERTY() bool Spectator = false;
    UPROPERTY() bool Transit = false;
    UPROPERTY() TArray<FTransform> CorpsePose;
    UPROPERTY() FTransform CorpseMesh;
    UPROPERTY() TArray<FPersonaInventoryRecord> Loot;
    UPROPERTY() float Stamina = 100;
    UPROPERTY() float Hunger = 100;
    UPROPERTY() int32 Team = 0;
    UPROPERTY() uint8 Movement = 1;
    UPROPERTY() bool Crouched = false;
    UPROPERTY() bool Sprinting = false;
    UPROPERTY() bool Ready = false;
    UPROPERTY() bool NPC = false;
    UPROPERTY() FString Target;
    UPROPERTY() FVector Investigate = FVector::ZeroVector;
    UPROPERTY() FVector Patrol = FVector::ZeroVector;
    UPROPERTY() FVector MoveGoal = FVector::ZeroVector;
    UPROPERTY() bool Moving = false;
    UPROPERTY() float InvestigationRemaining = 0;
    UPROPERTY() float AttackRemaining = 0;
    UPROPERTY() FName LastSense;
    UPROPERTY() TArray<FPersonaInventoryRecord> Inventory;
    UPROPERTY() int32 SelectedSlot = 0;
};
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
    UPROPERTY() int32 Version = 3;
    UPROPERTY() FPersonaRunRecord Run;
    UPROPERTY() FString Slot;
    UPROPERTY() FString Title;
    UPROPERTY() FDateTime Updated;
    UPROPERTY() FString CurrentMap = TEXT("/Game/Persona/Maps/L_Hub");
    UPROPERTY() bool Teams = false;
    UPROPERTY() bool FriendlyFire = true;
    UPROPERTY() TArray<FPersonaMapRecord> Maps;
    UPROPERTY() TArray<FPersonaSubjectRecord> Players;
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
    bool RestoreWorld(class APersonaGameMode* Mode);
    bool Rejoin(class APlayerController* Player);
    void RestorePlayerInventory(class APersonaCharacter* Character);
    void RestoreHubObjects();
    void PrepareTravel();
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
