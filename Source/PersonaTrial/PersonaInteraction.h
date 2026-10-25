#pragma once
#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/DataAsset.h"
#include "PersonaItemState.h"
#include "PersonaInteraction.generated.h"
class APersonaCharacter;
class APersonaProp;
UCLASS(BlueprintType)
class PERSONATRIAL_API UPersonaItemDefinition : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FText DisplayName;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="1")) int32 MaxStack = 1;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UStaticMesh> Mesh;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector CarryOffset = FVector(55, 14, -30);
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FRotator CarryRotation = FRotator::ZeroRotator;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector LeftGrip = FVector(0,-10,0);
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector RightGrip = FVector(0,10,0);
    UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bShotgun = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UStaticMesh> PumpMesh;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UStaticMesh> BoltMesh;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector Muzzle = FVector(49,0,1);
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector AimOffset = FVector(42,0,-7);
    UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 Pellets = 8;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float PelletDamage = 20;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float SpreadDegrees = 2.5f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float Range = 6000;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float FalloffStart = 1000;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float FalloffEnd = 4000;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float MinimumDamageScale = .25f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float HeadDamageScale = 2;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float LimbDamageScale = .65f;
};
UINTERFACE(BlueprintType)
class UPersonaInteractable : public UInterface { GENERATED_BODY() };
class PERSONATRIAL_API IPersonaInteractable
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable) FText InteractionLabel() const;
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable) void Interact(APersonaCharacter* Character);
};
UCLASS(ClassGroup=(Persona), meta=(BlueprintSpawnableComponent))
class PERSONATRIAL_API UPersonaInteraction : public UActorComponent
{
    GENERATED_BODY()
public:
    UPersonaInteraction();
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    AActor* TraceTarget() const;
    UFUNCTION(BlueprintCallable) void TryInteract();
    UFUNCTION(Server, Reliable) void ServerInteract(AActor* Target);
    UFUNCTION(Server, Reliable) void ServerRelease(bool Throw);
    UFUNCTION(Server, Reliable) void ServerSelectSlot(int32 Slot);
    void BeginDrop();
    void EndDrop();
    void CancelActions();
    float ThrowAlpha() const;
    UFUNCTION(Server, Reliable) void ServerCharge(bool Begin, bool Cancel = false);
    UFUNCTION(Server, Reliable) void ServerLoot(AActor* Target, int32 Index, bool Deposit, const FString& ExpectedId);
    bool CanReachLoot(AActor* Target) const;
    UFUNCTION(Server, Reliable) void ServerCloseLoot(AActor* Target);
    UFUNCTION(Client, Reliable) void ClientOpenLoot(AActor* Target);
    void CloseLoot();
    UPROPERTY() TObjectPtr<AActor> LootTarget;
    bool bCharging = false;
    double ChargeStart = 0;
    TWeakObjectPtr<APersonaProp> ChargedItem;
    double ServerChargeStart = -1;
    TWeakObjectPtr<APersonaProp> ServerChargedItem;
    UFUNCTION(Client, Reliable) void ClientFeedback(const FString& Message);
    bool Store(APersonaProp* Item);
    void Remove(APersonaProp* Item);
    void RefreshEquipped();
    void ReleaseAll();
    void ReleaseHeld(bool Throw);
    UPROPERTY(Replicated, BlueprintReadOnly) TObjectPtr<APersonaProp> Held;
    UPROPERTY(Replicated, BlueprintReadOnly) TArray<TObjectPtr<APersonaProp>> Slots;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 SelectedSlot = 0;
    FString Feedback;
    double FeedbackUntil = 0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float Reach = 280;
};
UCLASS()
class PERSONATRIAL_API APersonaProp : public AActor, public IPersonaInteractable
{
    GENERATED_BODY()
public:
    APersonaProp();
    virtual void BeginPlay() override;
    virtual void Tick(float Delta) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual FText InteractionLabel_Implementation() const override;
    virtual void Interact_Implementation(APersonaCharacter* Character) override;
    void Release(bool Throw);
    UFUNCTION() void OnRep_Holder();
    UFUNCTION() void OnRep_Item();
    int32 StackLimit() const;
    FText ItemName() const;
    FVector Grip(bool Left) const;
    FTransform CarryLocal() const;
    UPROPERTY(EditAnywhere, ReplicatedUsing=OnRep_Item, BlueprintReadOnly) TObjectPtr<UPersonaItemDefinition> Definition;
    UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, meta=(ClampMin="1")) int32 Quantity = 1;
    UPROPERTY(ReplicatedUsing=OnRep_Holder, BlueprintReadOnly) bool bStowed = false;
    UPROPERTY(Replicated, BlueprintReadOnly) FPersonaWeaponState WeaponState;
    UPROPERTY(ReplicatedUsing=OnRep_Holder) bool bInTransit = false;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> PumpVisual;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> BoltVisual;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> WorldPump;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> WorldBolt;
    // Stable identity also covers actors spawned by dropping/restoring inventory.
    UPROPERTY(Replicated) FString PersistentItemId;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> Mesh;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> OwnerViewMesh;
    // Contact positions in centimetres, relative to the unscaled carry frame.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Grip") FVector LeftGrip = FVector(-13,-24,-1);
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Grip") FVector RightGrip = FVector(-13,24,-1);
    UPROPERTY(ReplicatedUsing=OnRep_Holder, BlueprintReadOnly) TObjectPtr<APersonaCharacter> Holder;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float ThrowImpulse = 700;
};
UCLASS()
class PERSONATRIAL_API APersonaDoor : public AActor, public IPersonaInteractable
{
    GENERATED_BODY()
public:
    APersonaDoor();
    virtual FText InteractionLabel_Implementation() const override;
    virtual void Interact_Implementation(APersonaCharacter* Character) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    UFUNCTION() void OnRep_Open();
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<USceneComponent> Hinge;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> Mesh;
    UPROPERTY(ReplicatedUsing=OnRep_Open, BlueprintReadOnly) bool bOpen = false;
};
UCLASS()
class PERSONATRIAL_API APersonaTerminal : public AActor, public IPersonaInteractable
{
    GENERATED_BODY()
public:
    APersonaTerminal();
    virtual FText InteractionLabel_Implementation() const override;
    virtual void Interact_Implementation(APersonaCharacter* Character) override;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> Mesh;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bGoToTrial = true;
};
