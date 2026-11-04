#pragma once
#include "CoreMinimal.h"
#include "Navigation/NavLinkProxy.h"
#include "PersonaTraversal.generated.h"
class APersonaCharacter;

USTRUCT(BlueprintType)
struct FPersonaTraversalState
{
    GENERATED_BODY()
    UPROPERTY() bool Active=false;
    UPROPERTY() FVector Start=FVector::ZeroVector;
    UPROPERTY() FVector End=FVector::ZeroVector;
    UPROPERTY() FVector Ledge=FVector::ZeroVector;
    UPROPERTY() float Started=0;
    UPROPERTY() float Duration=1;
    UPROPERTY() bool Jump=false;
};

UCLASS()
class PERSONATRIAL_API APersonaClimbLink : public ANavLinkProxy
{
    GENERATED_BODY()
public:
    APersonaClimbLink();
    void Configure(const FVector& From,const FVector& To,UPrimitiveComponent* Surface);
    UFUNCTION() void Reached(AActor* Agent,const FVector& Destination);
    virtual void Tick(float Delta) override;
    TWeakObjectPtr<UPrimitiveComponent> Support;
    FTransform SupportTransform;
    TWeakObjectPtr<APersonaCharacter> User;
    FVector Low,High;
    float BlockedUntil=0;
};

UCLASS()
class PERSONATRIAL_API APersonaTraversalGraph : public AActor
{
    GENERATED_BODY()
public:
    APersonaTraversalGraph();
    virtual void Tick(float Delta) override;
    static void Ensure(UWorld* World);
private:
    float Age=0;
    TMap<TWeakObjectPtr<UPrimitiveComponent>,FTransform> Scanned;
    void Scan();
};
