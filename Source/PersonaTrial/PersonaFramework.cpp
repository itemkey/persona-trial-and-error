#include "PersonaFramework.h"
#include "PersonaAttributes.h"
#include "PersonaCharacter.h"
#include "PersonaAI.h"
#include "PersonaInteraction.h"
#include "PersonaOnline.h"
#include "PersonaPanel.h"
#include "AbilitySystemComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "NavigationSystem.h"
#include "OnlineSubsystem.h"
#include "Interfaces/VoiceInterface.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/CharacterMovementComponent.h"

APersonaPlayerState::APersonaPlayerState()
{
    SetNetUpdateFrequency(20);
    AbilitySystem = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystem"));
    AbilitySystem->SetIsReplicated(true); AbilitySystem->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);
    Attributes = CreateDefaultSubobject<UPersonaAttributes>(TEXT("Attributes"));
}
void APersonaPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(APersonaPlayerState, Team); DOREPLIFETIME(APersonaPlayerState, bReady);
}
void APersonaPlayerState::CopyProperties(APlayerState* Target)
{
    Super::CopyProperties(Target);
    if (auto P = Cast<APersonaPlayerState>(Target))
    {
        P->Team = Team; P->bReady = bReady;
        P->AbilitySystem->SetNumericAttributeBase(UPersonaAttributes::GetHealthAttribute(), Attributes->GetHealth());
        P->AbilitySystem->SetNumericAttributeBase(UPersonaAttributes::GetStaminaAttribute(), Attributes->GetStamina());
        P->AbilitySystem->SetNumericAttributeBase(UPersonaAttributes::GetHungerAttribute(), Attributes->GetHunger());
    }
}
void APersonaGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(APersonaGameState, Mode); DOREPLIFETIME(APersonaGameState, bInTrial); DOREPLIFETIME(APersonaGameState, NPCCount);
}
APersonaGameMode::APersonaGameMode()
{
    PlayerControllerClass = APersonaPlayerController::StaticClass(); PlayerStateClass = APersonaPlayerState::StaticClass();
    GameStateClass = APersonaGameState::StaticClass(); DefaultPawnClass = APersonaCharacter::StaticClass();
    bUseSeamlessTravel = true;
}
void APersonaGameMode::InitGame(const FString& MapName, const FString& Options, FString& Error)
{
    Super::InitGame(MapName, Options, Error);
    InitialMode = UGameplayStatics::ParseOption(Options, TEXT("Mode")) == TEXT("Teams") ? EPersonaMode::Teams : EPersonaMode::Cooperative;
    bInitialTrial = MapName.Contains(TEXT("TestChamber"));
    if (UClass* PlayerBP = LoadClass<APawn>(nullptr, TEXT("/Game/Persona/Characters/BP_Persona.BP_Persona_C"))) DefaultPawnClass = PlayerBP;
    if (!NPCClass) NPCClass = LoadClass<APawn>(nullptr, TEXT("/Game/Persona/AI/BP_PersonaNPC.BP_PersonaNPC_C"));
}
void APersonaGameMode::BeginPlay()
{
    Super::BeginPlay();
    auto GS = GetGameState<APersonaGameState>(); GS->Mode = InitialMode; GS->bInTrial = bInitialTrial;
    StartMatch();
    if (bInitialTrial) { FTimerHandle Handle; GetWorldTimerManager().SetTimer(Handle, [this]() { if (GetGameState<APersonaGameState>()->NPCCount == 0) SetNPCCount(1); }, 2, false); }
}
void APersonaGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& ID, FString& Error)
{
    Super::PreLogin(Options, Address, ID, Error);
    if (GetNumPlayers() >= 4) Error = TEXT("PERSONA session is full (4 players).");
}
void APersonaGameMode::PostLogin(APlayerController* NewPlayer)
{
    Super::PostLogin(NewPlayer);
    if (auto P = NewPlayer->GetPlayerState<APersonaPlayerState>())
    {
        auto GS = GetGameState<APersonaGameState>();
        int32 TeamCounts[2] = {0,0};
        for (auto State : GS->PlayerArray)
            if (auto Existing = Cast<APersonaPlayerState>(State); Existing && Existing != P)
                ++TeamCounts[FMath::Clamp(Existing->Team,0,1)];
        P->Team = GS->Mode == EPersonaMode::Teams && TeamCounts[0] > TeamCounts[1] ? 1 : 0;
    }
    UE_LOG(LogTemp, Display, TEXT("PERSONA JOIN: players=%d"), GetNumPlayers());
}
void APersonaGameMode::Logout(AController* Exiting)
{
    if (auto C = Cast<APersonaCharacter>(Exiting->GetPawn())) C->Interaction->ReleaseHeld(false);
    Super::Logout(Exiting);
}
void APersonaGameMode::AssignTeams()
{
    auto GS = GetGameState<APersonaGameState>(); if (!GS) return;
    int32 Index = 0;
    for (auto PS : GS->PlayerArray) if (auto P = Cast<APersonaPlayerState>(PS)) { P->Team = GS->Mode == EPersonaMode::Teams ? Index++ % 2 : 0; }
}
void APersonaGameMode::SetMode(EPersonaMode Mode)
{
    auto GS = GetGameState<APersonaGameState>(); if (!GS || GS->bInTrial) return;
    GS->Mode = Mode; AssignTeams();
    for (auto State : GS->PlayerArray) if (auto P = Cast<APersonaPlayerState>(State)) P->bReady = false;
}
bool APersonaGameMode::CanDamage(const AController* DamageInstigator, const AController* Victim) const
{
    if (!DamageInstigator || !Victim || DamageInstigator == Victim) return true;
    const auto A = DamageInstigator->GetPlayerState<APersonaPlayerState>(); const auto B = Victim->GetPlayerState<APersonaPlayerState>();
    return !A || !B || A->Team != B->Team || bFriendlyFire;
}
void APersonaGameMode::Travel(bool ToTrial)
{
    auto GS = GetGameState<APersonaGameState>(); if (!GS || GS->bInTrial == ToTrial) return;
    if (ToTrial)
        for (auto State : GS->PlayerArray)
            if (auto P = Cast<APersonaPlayerState>(State); P && !P->bReady)
            { if (auto GI = Cast<UPersonaGameInstance>(GetGameInstance())) GI->Status = TEXT("All players must be ready before entering the chamber."); return; }
    const FString Map = ToTrial ? TEXT("/Game/Persona/Maps/L_TestChamber") : TEXT("/Game/Persona/Maps/L_Hub");
    const FString Mode = GS->Mode == EPersonaMode::Teams ? TEXT("Teams") : TEXT("Coop");
    GetWorld()->ServerTravel(Map + TEXT("?Mode=") + Mode);
}
void APersonaGameMode::SetNPCCount(int32 Count)
{
    Count = FMath::Clamp(Count, 0, 30);
    TArray<APersonaCharacter*> Existing;
    for (TActorIterator<APersonaCharacter> It(GetWorld()); It; ++It) if (It->bNPC) Existing.Add(*It);
    while (Existing.Num() > Count) { APersonaCharacter* C = Existing.Pop(); if (C->GetController()) C->GetController()->Destroy(); C->Destroy(); }
    auto Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    const FVector Origin = GetGameState<APersonaGameState>()->bInTrial ? FVector(1800, 0, 100) : FVector(500, 0, 100);
    FNavLocation ProjectedOrigin;
    const bool bProjected = Nav && Nav->ProjectPointToNavigation(Origin, ProjectedOrigin, FVector(1600,1600,1000));
    if (NPCClass && bProjected)
        for (int32 Attempt = 0, MaxAttempts = (Count - Existing.Num()) * 12;
             Existing.Num() < Count && Attempt < MaxAttempts; ++Attempt)
        {
            FNavLocation Location;
            if (!Nav->GetRandomReachablePointInRadius(ProjectedOrigin.Location, 1600, Location)) break;
            FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;
            auto C = GetWorld()->SpawnActor<APersonaCharacter>(NPCClass, Location.Location + FVector(0,0,100), FRotator::ZeroRotator, Params);
            if (C) { C->SpawnDefaultController(); Existing.Add(C); }
        }
    GetGameState<APersonaGameState>()->NPCCount = Existing.Num();
    UE_LOG(LogTemp, Display, TEXT("PERSONA NPC COUNT: %d requested=%d class=%s projected=%d"), Existing.Num(), Count, *GetNameSafe(NPCClass), bProjected);
}
void UPersonaGameInstance::Init()
{
    Super::Init(); FailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &ThisClass::NetworkFailure);
}
void UPersonaGameInstance::Shutdown() { if (GEngine) GEngine->OnNetworkFailure().Remove(FailureHandle); Super::Shutdown(); }
void UPersonaGameInstance::NetworkFailure(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type Type, const FString& Error)
{
    Status = TEXT("Connection ended. The host left or the connection was lost. ") + Error;
    GetSubsystem<UPersonaOnline>()->ClearSessionAfterFailure();
    UGameplayStatics::OpenLevel(this, TEXT("/Game/Persona/Maps/L_Hub"));
}
void APersonaPlayerController::BeginPlay()
{
    Super::BeginPlay();
    EnsureLocalPanel();
}
void APersonaPlayerController::EnsureLocalPanel()
{
    if (!IsLocalController() || !GetPawn()) return;
    if (!IsValid(Panel))
        Panel = CreateWidget<UPersonaPanel>(this, PanelClass ? PanelClass.Get() : UPersonaPanel::StaticClass());
    if (Panel && !Panel->IsInViewport())
    {
        // Seamless travel retains this controller but removes viewport widgets.
        Panel->AddToViewport(); bPanelOpen = false; Panel->SetPanelOpen(false);
        ResetIgnoreLookInput(); ResetIgnoreMoveInput();
        SetInputMode(FInputModeGameOnly()); bShowMouseCursor = false;
    }
}
bool APersonaPlayerController::IsPanelInViewport() const { return IsValid(Panel) && Panel->IsInViewport(); }
void APersonaPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent(); InputComponent->BindKey(EKeys::Tab, IE_Pressed, this, &ThisClass::TogglePanel);
}
void APersonaPlayerController::TogglePanel()
{
    bPanelOpen = !bPanelOpen; if (Panel) Panel->SetPanelOpen(bPanelOpen);
    bShowMouseCursor = bPanelOpen;
    if (bPanelOpen) { FInputModeGameAndUI Mode; Mode.SetHideCursorDuringCapture(false); SetInputMode(Mode); StopTalking(); }
    else SetInputMode(FInputModeGameOnly());
    SetIgnoreLookInput(bPanelOpen); SetIgnoreMoveInput(bPanelOpen);
}
bool APersonaPlayerController::IsHost() const { return HasAuthority() && IsLocalController(); }
void APersonaPlayerController::ServerSetReady_Implementation(bool Ready) { if (auto P = GetPlayerState<APersonaPlayerState>()) P->bReady = Ready; }
void APersonaPlayerController::ServerSetMode_Implementation(EPersonaMode Mode) { if (IsHost()) if (auto GM = GetWorld()->GetAuthGameMode<APersonaGameMode>()) GM->SetMode(Mode); }
void APersonaPlayerController::ServerTravel_Implementation(bool ToTrial) { if (IsHost()) if (auto GM = GetWorld()->GetAuthGameMode<APersonaGameMode>()) GM->Travel(ToTrial); }
void APersonaPlayerController::ServerNPCCount_Implementation(int32 Count) { if (IsHost()) if (auto GM = GetWorld()->GetAuthGameMode<APersonaGameMode>()) GM->SetNPCCount(Count); }
void APersonaPlayerController::ServerTestVitals_Implementation(bool Food)
{
#if !UE_BUILD_SHIPPING
    if (auto C = Cast<APersonaCharacter>(GetPawn()))
    {
        auto ASC = C->GetAbilitySystemComponent();
        if (Food) { auto S = ASC->MakeOutgoingSpec(UPersonaFoodEffect::StaticClass(), 1, ASC->MakeEffectContext()); ASC->ApplyGameplayEffectSpecToSelf(*S.Data.Get()); }
        else if (!C->IsAlive()) ASC->SetNumericAttributeBase(UPersonaAttributes::GetHealthAttribute(), 100);
        else { UGameplayStatics::ApplyDamage(C, 20, this, C, UDamageType::StaticClass()); ASC->SetNumericAttributeBase(UPersonaAttributes::GetHungerAttribute(), FMath::Max(0.f, C->GetVitals()->GetHunger()-15)); }
    }
#endif
}
void APersonaPlayerController::PersonaNPCs(int32 Count) { ServerNPCCount(Count); }
void APersonaPlayerController::PersonaTrial() { ServerTravel(true); }
void APersonaPlayerController::PersonaHub() { ServerTravel(false); }
void APersonaPlayerController::PersonaMode(int32 Mode) { ServerSetMode(Mode == 1 ? EPersonaMode::Teams : EPersonaMode::Cooperative); }
void APersonaPlayerController::PersonaJoinIP(const FString& Address) { if (Address.Len() < 128 && !Address.Contains(TEXT("?"))) ClientTravel(Address, TRAVEL_Absolute); }
void APersonaPlayerController::PersonaReady() { if (auto P = GetPlayerState<APersonaPlayerState>()) ServerSetReady(!P->bReady); }
void APersonaPlayerController::SetVoiceVolume(float Value) { VoiceVolume = FMath::Clamp(Value, 0.f, 1.f); }
bool APersonaPlayerController::IsMuted(APersonaPlayerState* P) const { return P && MutedIds.Contains(P->GetUniqueId().ToString()); }
void APersonaPlayerController::ToggleMute(APersonaPlayerState* P)
{
    if (!P || P == PlayerState || !P->GetUniqueId().IsValid()) return;
    const FString ID = P->GetUniqueId().ToString(); const bool Mute = !MutedIds.Contains(ID);
    if (Mute) MutedIds.Add(ID); else MutedIds.Remove(ID);
    if (auto OSS = IOnlineSubsystem::Get()) if (auto V = OSS->GetVoiceInterface(); V.IsValid())
    { if (Mute) V->MuteRemoteTalker(0, *P->GetUniqueId().GetUniqueNetId(), false); else V->UnmuteRemoteTalker(0, *P->GetUniqueId().GetUniqueNetId(), false); }
}
void APersonaPlayerController::Tick(float Delta)
{
    Super::Tick(Delta); if (!IsLocalController()) return;
    EnsureLocalPanel();
    for (TActorIterator<APersonaCharacter> It(GetWorld()); It; ++It)
        if (!It->IsLocallyControlled() && !It->bNPC) It->Voice->SetOutputVolume(IsMuted(It->GetPlayerState<APersonaPlayerState>()) ? 0 : VoiceVolume);
}
