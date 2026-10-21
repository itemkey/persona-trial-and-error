#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "PersonaDiagnostics.generated.h"

// Explicit opt-in integration driver. Never ticks in Shipping or ordinary play.
UCLASS()
class UPersonaDiagnostics : public UGameInstanceSubsystem, public FTickableGameObject
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Tick(float DeltaTime) override;
    virtual bool IsTickable() const override { return bEnabled && !IsTemplate(); }
    virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UPersonaDiagnostics, STATGROUP_Tickables); }
    virtual UWorld* GetTickableGameObjectWorld() const override { return GetWorld(); }
private:
    bool bEnabled = false;
    FString Role, Directory;
    double NextSample = 0;
    FVector MoveInput = FVector::ZeroVector;
    void Execute(const TSharedPtr<class FJsonObject>& Command);
    void Snapshot();
};
