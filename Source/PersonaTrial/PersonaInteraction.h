#pragma once
#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "PersonaInteraction.generated.h"
class APersonaCharacter;
class APersonaProp;
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
    void ReleaseHeld(bool Throw);
    UPROPERTY(Replicated, BlueprintReadOnly) TObjectPtr<APersonaProp> Held;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float Reach = 280;
};
UCLASS()
class PERSONATRIAL_API APersonaProp : public AActor, public IPersonaInteractable
{
    GENERATED_BODY()
public:
    APersonaProp();
    virtual void Tick(float Delta) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual FText InteractionLabel_Implementation() const override;
    virtual void Interact_Implementation(APersonaCharacter* Character) override;
    void Release(bool Throw);
    UFUNCTION() void OnRep_Holder();
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> Mesh;
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
