#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataAsset.h"
#include "GameFramework/Actor.h"
#include "PersonaItemState.h"
#include "PersonaWeapon.generated.h"
class APersonaProp;
class APersonaCharacter;

UCLASS(BlueprintType)
class PERSONATRIAL_API UPersonaFXFrames : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere) TArray<TObjectPtr<UStaticMesh>> Frames;
};
UCLASS()
class PERSONATRIAL_API APersonaFX : public AActor
{
    GENERATED_BODY()
public:
    APersonaFX();
    virtual void Tick(float Delta) override;
    UPROPERTY() TObjectPtr<UPersonaFXFrames> Frames;
    UPROPERTY() TObjectPtr<class UStaticMeshComponent> Mesh;
    float Age=0;
    static void Play(UWorld* World,FName Kind,const FVector& Location,const FRotator& Rotation);
};

UCLASS(ClassGroup=(Persona), meta=(BlueprintSpawnableComponent))
class PERSONATRIAL_API UPersonaWeapon : public UActorComponent
{
    GENERATED_BODY()
public:
    UPersonaWeapon();
    virtual void TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    void Fire();
    void SetAim(bool Value);
    void Load(bool Held);
    void Unload(bool Held);
    void Pump();
    void Inspect();
    void Safety();
    void Cancel();
    APersonaProp* Item() const;
    FVector Grip(bool Left, FVector Base) const;
    FTransform ViewTransform(FTransform Base) const;
    UFUNCTION(Server, Reliable) void ServerAction(uint8 Command, bool Held = false);
    UFUNCTION(Server, Reliable) void ServerFire(FVector_NetQuantizeNormal Aim, int32 Sequence);
    UFUNCTION(Client, Reliable) void ClientFireState(APersonaProp* Gun, FPersonaWeaponState State);
    void ResetSequence() { LocalSequence=LastServerSequence=0; NextShot=0; }
    UFUNCTION(NetMulticast, Unreliable) void ShotFX(int32 Sequence, FVector_NetQuantize Muzzle);
    UFUNCTION(NetMulticast, Unreliable) void ImpactFX(FVector_NetQuantize Point, FVector_NetQuantizeNormal Normal, bool Human);
    UFUNCTION(NetMulticast, Unreliable) void MechanicalFX(FName Sound);
    UFUNCTION(NetMulticast, Unreliable) void EjectShell();
    UPROPERTY(Replicated, BlueprintReadOnly) EPersonaWeaponAction Action = EPersonaWeaponAction::Idle;
    UPROPERTY(Replicated) float ActionStart = 0;
    UPROPERTY(Replicated) bool bAiming = false;
    float AimAlpha = 0;
    float Recoil = 0;
    float InspectUntil = 0;
    FString Inspection() const;
private:
    TWeakObjectPtr<APersonaProp> ActiveItem;
    bool bLoadHeld = false, bUnloadHeld = false;
    float ActionEnd = 0, NextShot = 0;
    int32 LocalSequence = 0, LastServerSequence = 0;
    bool CanAct() const;
    void StartAction(EPersonaWeaponAction Value, float Duration);
    void FinishAction();
    void LocalShot();
};
