#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameMode.h"
#include "GameFramework/GameState.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Engine/GameInstance.h"
#include "Engine/DataAsset.h"
#include "AbilitySystemInterface.h"
#include "PersonaFramework.generated.h"

class UAbilitySystemComponent;
class UPersonaAttributes;
class UPersonaPanel;

UENUM(BlueprintType)
enum class EPersonaMode : uint8 { Cooperative, Teams };

UCLASS(BlueprintType)
class PERSONATRIAL_API UPersonaTrialDefinition : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FText DisplayName;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UWorld> Map;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 MaxNPCs = 30;
    UPROPERTY(EditAnywhere, BlueprintReadOnly) EPersonaMode Mode = EPersonaMode::Cooperative;
};

UCLASS()
class PERSONATRIAL_API APersonaPlayerState : public APlayerState, public IAbilitySystemInterface
{
    GENERATED_BODY()
public:
    APersonaPlayerState();
    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override { return AbilitySystem; }
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void CopyProperties(APlayerState* Target) override;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UAbilitySystemComponent> AbilitySystem;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UPersonaAttributes> Attributes;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 Team = 0;
    UPROPERTY(Replicated, BlueprintReadOnly) bool bReady = false;
};

UCLASS()
class PERSONATRIAL_API APersonaGameState : public AGameState
{
    GENERATED_BODY()
public:
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    UPROPERTY(Replicated, BlueprintReadOnly) EPersonaMode Mode = EPersonaMode::Cooperative;
    UPROPERTY(Replicated, BlueprintReadOnly) bool bInTrial = false;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 NPCCount = 0;
};

UCLASS(Config=Game)
class PERSONATRIAL_API APersonaGameMode : public AGameMode
{
    GENERATED_BODY()
public:
    APersonaGameMode();
    virtual void InitGame(const FString& MapName, const FString& Options, FString& Error) override;
    virtual void BeginPlay() override;
    virtual void PostLogin(APlayerController* NewPlayer) override;
    virtual void Logout(AController* Exiting) override;
    virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& Error) override;
    bool CanDamage(const AController* Instigator, const AController* Victim) const;
    void SetMode(EPersonaMode Mode);
    void Travel(bool bToTrial);
    void SetNPCCount(int32 Count);
    UPROPERTY(EditDefaultsOnly, Config, BlueprintReadOnly, Category="Rules") bool bFriendlyFire = true;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Rules") TSubclassOf<APawn> NPCClass;
private:
    EPersonaMode InitialMode = EPersonaMode::Cooperative;
    bool bInitialTrial = false;
    void AssignTeams();
};

UCLASS()
class PERSONATRIAL_API UPersonaGameInstance : public UGameInstance
{
    GENERATED_BODY()
public:
    virtual void Init() override;
    virtual void Shutdown() override;
    UPROPERTY(BlueprintReadOnly) FString Status = TEXT("Choose Host or Find. Local tests: -CustomConfig=Local -nosteam");
private:
    FDelegateHandle FailureHandle;
    void NetworkFailure(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type Type, const FString& Error);
};

UCLASS()
class PERSONATRIAL_API APersonaPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    virtual void BeginPlay() override;
    virtual void SetupInputComponent() override;
    virtual void Tick(float DeltaSeconds) override;
    UFUNCTION(BlueprintCallable) void TogglePanel();
    UFUNCTION(Server, Reliable) void ServerSetReady(bool Ready);
    UFUNCTION(Server, Reliable) void ServerSetMode(EPersonaMode Mode);
    UFUNCTION(Server, Reliable) void ServerTravel(bool ToTrial);
    UFUNCTION(Server, Reliable) void ServerNPCCount(int32 Count);
    UFUNCTION(Server, Reliable) void ServerTestVitals(bool Food);
    UFUNCTION(Exec) void PersonaNPCs(int32 Count);
    UFUNCTION(Exec) void PersonaTrial();
    UFUNCTION(Exec) void PersonaHub();
    UFUNCTION(Exec) void PersonaMode(int32 Mode);
    UFUNCTION(Exec) void PersonaJoinIP(const FString& Address);
    UFUNCTION(Exec) void PersonaReady();
    UPROPERTY(BlueprintReadOnly) float VoiceVolume = 1.f;
    void SetVoiceVolume(float Value);
    void ToggleMute(APersonaPlayerState* State);
    bool IsMuted(APersonaPlayerState* State) const;
    bool IsHost() const;
    bool IsPanelInViewport() const;
    UPROPERTY(EditDefaultsOnly, Category="UI") TSubclassOf<UPersonaPanel> PanelClass;
private:
    void EnsureLocalPanel();
    UPROPERTY() TObjectPtr<UPersonaPanel> Panel;
    bool bPanelOpen = false;
    TSet<FString> MutedIds;
};
