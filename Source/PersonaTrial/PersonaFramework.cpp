#include "PersonaFramework.h"
#include "PersonaAttributes.h"
#include "PersonaCharacter.h"
#include "PersonaAI.h"
#include "PersonaInteraction.h"
#include "PersonaOnline.h"
#include "PersonaPanel.h"
#include "PersonaSave.h"
#include "PersonaHub.h"
#include "PersonaInteraction.h"
#include "PersonaMenu.h"
#include "PersonaInput.h"
#include "PersonaBattle.h"
#include "PersonaBattleArena.h"
#include "GameFramework/GameSession.h"
#include "Framework/Application/SlateApplication.h"
#include "PersonaLoot.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
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
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(APersonaPlayerState, Team); DOREPLIFETIME(APersonaPlayerState, bReady); DOREPLIFETIME(APersonaPlayerState, PersistentId);DOREPLIFETIME(APersonaPlayerState,bBattleReady);
}
void APersonaPlayerState::CopyProperties(APlayerState* Target)
{
    Super::CopyProperties(Target);
    if (auto P = Cast<APersonaPlayerState>(Target))
    {
        P->Team = Team; P->bReady = bReady;
        P->bBattleReady=bBattleReady;
        P->PersistentId = PersistentId;
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
    bDelayedStart = true; // Restore the world before any player receives a pawn.
}
void APersonaGameMode::InitGame(const FString& MapName, const FString& Options, FString& Error)
{
    Super::InitGame(MapName, Options, Error);
    if(MapName.Contains(TEXT("L_SectorBattle")) && GameSession) GameSession->MaxPlayers=8;
    InitialMode = UGameplayStatics::ParseOption(Options, TEXT("Mode")) == TEXT("Teams") ? EPersonaMode::Teams : EPersonaMode::Cooperative;
    bInitialTrial = MapName.Contains(TEXT("TestChamber"));
    if (UClass* PlayerBP = LoadClass<APawn>(nullptr, TEXT("/Game/Persona/Characters/BP_Persona.BP_Persona_C"))) DefaultPawnClass = PlayerBP;
    if (!NPCClass) NPCClass = LoadClass<APawn>(nullptr, TEXT("/Game/Persona/AI/BP_PersonaNPC.BP_PersonaNPC_C"));
}
void APersonaGameMode::BeginPlay()
{
    Super::BeginPlay();
    auto GS = GetGameState<APersonaGameState>(); GS->Mode = InitialMode; GS->bInTrial = bInitialTrial;
    if(GetWorld()->GetMapName().Contains(TEXT("L_SectorBattle")))
    { GS->Mode=EPersonaMode::Teams;GS->bInTrial=true;bFriendlyFire=true;GetWorld()->SpawnActor<APersonaBattleArena>();StartMatch();return; }
    const bool Restored = GetGameInstance()->GetSubsystem<UPersonaSaveSubsystem>()->RestoreWorld(this);
    StartMatch();
    GetGameInstance()->GetSubsystem<UPersonaBattleSubsystem>()->HubReady();
    if (bInitialTrial && !Restored)
        GetWorldTimerManager().SetTimer(InitialNPCTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
        {
            if (auto State=GetGameState<APersonaGameState>(); State && State->NPCCount==0) SetNPCCount(1);
        }),2,false);
}
void APersonaGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
    // TimerManager is shared with the game instance during seamless travel.
    // A quick return to the hub must not invoke the old chamber's GameMode.
    GetWorldTimerManager().ClearTimer(InitialNPCTimer);
    Super::EndPlay(Reason);
}
FString APersonaGameMode::InitNewPlayer(APlayerController* Player, const FUniqueNetIdRepl& Id, const FString& Options, const FString& Portal)
{
    FString Error = Super::InitNewPlayer(Player,Id,Options,Portal);
    if (!Error.IsEmpty()) return Error;
    auto P = Player->GetPlayerState<APersonaPlayerState>();
    auto OSS = IOnlineSubsystem::Get();
    FString Identity = OSS && OSS->GetSubsystemName() == FName(TEXT("STEAM")) && Id.IsValid() ? Id.ToString() : UGameplayStatics::ParseOption(Options,TEXT("PersonaId"));
    if (Identity.IsEmpty() && Player->IsLocalController()) Identity = GetGameInstance()->GetSubsystem<UPersonaSaveSubsystem>()->LocalIdentity();
    if (Identity.IsEmpty() || Identity.Len() > 80) return TEXT("Missing persistent player identity. Restart PERSONA.");
    for (auto State : GetGameState<APersonaGameState>()->PlayerArray)
        if (auto Existing = Cast<APersonaPlayerState>(State); Existing && Existing != P && Existing->PersistentId == Identity) return TEXT("This profile is already connected.");
    P->PersistentId = Identity; return Error;
}
void APersonaGameMode::RestartPlayer(AController* Player)
{
    if(auto A=APersonaBattleArena::Get(GetWorld())) { A->SpawnPlayer(Cast<APersonaPlayerController>(Player));return; }
    auto PC = Cast<APlayerController>(Player);
    auto Saves = GetGameInstance()->GetSubsystem<UPersonaSaveSubsystem>();
    if (PC && Saves->Rejoin(PC)) return;
    Super::RestartPlayer(Player);
    if (auto C = Cast<APersonaCharacter>(Player->GetPawn()))
    {
        if (auto P = Player->GetPlayerState<APersonaPlayerState>())
        {
            C->PersistentId = P->PersistentId; C->SavedTeam = P->Team;
            if (Saves->Active)
                if (const auto R = Saves->Active->Players.FindByPredicate([&](const FPersonaSubjectRecord& S) { return S.Id == P->PersistentId; }))
                { C->Water=R->Water; C->bDryCollapse=R->DryCollapse; C->SetVitals(R->Health,R->Stamina,R->Hunger); P->Team = R->Team; P->bReady = R->Ready; Saves->RestorePlayerInventory(C); }
        }
        if(auto D=APersonaTrialDirector::Get(GetWorld()); D && D->Running() && D->Run.Lift!=EPersonaLiftPhase::Hub)
        { C->bSpectator=true; C->OnRep_Transit(); C->ForceNetUpdate(); }
    }
}
void APersonaGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& ID, FString& Error)
{
    Super::PreLogin(Options, Address, ID, Error);
    auto B=GetGameInstance()->GetSubsystem<UPersonaBattleSubsystem>();
    if(B->IsArena())
    {
        const auto OSS=IOnlineSubsystem::Get();
        const FString Who=OSS && OSS->GetSubsystemName()==FName(TEXT("STEAM")) && ID.IsValid() ? ID.ToString() : UGameplayStatics::ParseOption(Options,TEXT("PersonaId"));
        if(!B->Ticket.Contains(Who)) Error=TEXT("Only the registered battle roster may join.");
        if(GetNumPlayers()>=8) Error=TEXT("PERSONA battle is full (8 players).");
    }
    else if(B->Locked()) Error=TEXT("This sector has locked its battle roster. Wait until the groups return.");
    else if(GetNumPlayers()>=4) Error=TEXT("PERSONA session is full (4 players).");
}
void APersonaGameMode::PostLogin(APlayerController* NewPlayer)
{
    Super::PostLogin(NewPlayer);
    if(auto B=GetGameInstance()->GetSubsystem<UPersonaBattleSubsystem>();B->IsArena())
    { if(auto P=NewPlayer->GetPlayerState<APersonaPlayerState>()) { P->Team=B->Ticket.TeamFor(P->PersistentId);if(auto C=Cast<APersonaCharacter>(NewPlayer->GetPawn())) C->SavedTeam=P->Team; }return; }
    if (auto P = NewPlayer->GetPlayerState<APersonaPlayerState>())
    {
        const auto Saves = GetGameInstance()->GetSubsystem<UPersonaSaveSubsystem>();
        const bool Returning = Saves->Active && Saves->Active->Players.ContainsByPredicate([&](const FPersonaSubjectRecord& S) { return S.Id == P->PersistentId; });
        auto GS = GetGameState<APersonaGameState>();
        int32 TeamCounts[2] = {0,0};
        for (auto State : GS->PlayerArray)
            if (auto Existing = Cast<APersonaPlayerState>(State); Existing && Existing != P)
                ++TeamCounts[FMath::Clamp(Existing->Team,0,1)];
        if (!Returning) P->Team = GS->Mode == EPersonaMode::Teams && TeamCounts[0] > TeamCounts[1] ? 1 : 0;
    }
    UE_LOG(LogTemp, Display, TEXT("PERSONA JOIN: players=%d"), GetNumPlayers());
}
void APersonaGameMode::Logout(AController* Exiting)
{
    if (auto C = Cast<APersonaCharacter>(Exiting->GetPawn())) C->BecomeOffline();
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
    if(GetGameInstance()->GetSubsystem<UPersonaBattleSubsystem>()->Locked()) return;
    if(auto D=APersonaTrialDirector::Get(GetWorld()); D && D->Running()) return;
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
    if(GetGameInstance()->GetSubsystem<UPersonaBattleSubsystem>()->Locked()) return;
    if(auto D=APersonaTrialDirector::Get(GetWorld()); D && D->Running()) return;
    auto GS = GetGameState<APersonaGameState>(); if (!GS || GS->bInTrial == ToTrial) return;
    if (ToTrial)
        for (auto State : GS->PlayerArray)
            if (auto P = Cast<APersonaPlayerState>(State); P && !P->bReady)
            { if (auto GI = Cast<UPersonaGameInstance>(GetGameInstance())) GI->Status = TEXT("All players must be ready before entering the chamber."); return; }
    const FString Map = ToTrial ? TEXT("/Game/Persona/Maps/L_TestChamber") : TEXT("/Game/Persona/Maps/L_Hub");
    const FString Mode = GS->Mode == EPersonaMode::Teams ? TEXT("Teams") : TEXT("Coop");
    auto Saves = GetGameInstance()->GetSubsystem<UPersonaSaveSubsystem>();
    if (!Saves->SaveWorld()) { CastChecked<UPersonaGameInstance>(GetGameInstance())->Status = Saves->LastError; return; }
    Saves->PrepareTravel();
    GetWorld()->ServerTravel(Map + TEXT("?Mode=") + Mode);
}
void APersonaGameMode::SetNPCCount(int32 Count)
{
    if(GetGameInstance()->GetSubsystem<UPersonaBattleSubsystem>()->Locked()) return;
    if(auto D=APersonaTrialDirector::Get(GetWorld()); D && D->Running()) return;
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
    if(FSlateApplication::IsInitialized())
    { PhysicalInput=MakeShared<FPersonaInput>();FSlateApplication::Get().RegisterInputPreProcessor(PhysicalInput); }
}
void UPersonaGameInstance::Shutdown()
{
    if(PhysicalInput && FSlateApplication::IsInitialized()) FSlateApplication::Get().UnregisterInputPreProcessor(PhysicalInput);
    PhysicalInput.Reset(); if(GEngine) GEngine->OnNetworkFailure().Remove(FailureHandle); Super::Shutdown();
}
void UPersonaGameInstance::NetworkFailure(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type Type, const FString& Error)
{
    GetSubsystem<UPersonaOnline>()->ConnectionArrived();
    if(GetSubsystem<UPersonaBattleSubsystem>()->HandleFailure()) return;
    Status = TEXT("Connection ended. The host left or the connection was lost. ") + Error;
    GetSubsystem<UPersonaOnline>()->ClearSessionAfterFailure();
    UGameplayStatics::OpenLevel(this, TEXT("/Game/Persona/Maps/L_MainMenu"));
}
void APersonaPlayerController::BeginPlay()
{
    Super::BeginPlay();
    if(IsLocalController()) GetGameInstance()->GetSubsystem<UPersonaOnline>()->ConnectionArrived();
    // Standalone/-game sessions should not display editor map-build notices.
    // Keep warnings available inside the editor and keep normal debug messages.
    if(IsLocalController() && !GIsEditor && GEngine) GEngine->bSuppressMapWarnings=true;
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
    Super::SetupInputComponent(); InputComponent->BindKey(EKeys::Tab, IE_Pressed, this, &ThisClass::HandleTab);
    InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &ThisClass::TogglePauseMenu);
}
void APersonaPlayerController::PawnLeavingGame()
{
    if (auto C = Cast<APersonaCharacter>(GetPawn())) C->BecomeOffline();
    else Super::PawnLeavingGame();
}
void APersonaPlayerController::TogglePauseMenu()
{
    if(auto C=Cast<APersonaCharacter>(GetPawn())) { C->Interaction->CancelActions(); if(C->Interaction->LootTarget) { C->Interaction->CloseLoot(); return; } }
    if(bTerminalOpen) { CloseOverlays(); return; }
    if (PauseMenu && PauseMenu->IsInViewport())
    {
        CloseOverlays(); return;
    }
    if (bPanelOpen) { CloseOverlays(); return; }
    PauseMenu = CreateWidget<UPersonaMenu>(this); PauseMenu->bInGame = true; PauseMenu->AddToViewport(10);
    SetIgnoreMoveInput(true); SetIgnoreLookInput(true); StopTalking();
    FInputModeGameAndUI Mode; Mode.SetWidgetToFocus(PauseMenu->TakeWidget()); Mode.SetHideCursorDuringCapture(false); SetInputMode(Mode); bShowMouseCursor = true;
}
void APersonaPlayerController::CloseOverlays()
{
    bTerminalOpen=false; bPanelOpen=false;
    if(Panel) Panel->SetPanelOpen(false);
    if(PauseMenu) PauseMenu->RemoveFromParent();
    ResetIgnoreMoveInput();ResetIgnoreLookInput();SetInputMode(FInputModeGameOnly());bShowMouseCursor=false;
    if(auto C=Cast<APersonaCharacter>(GetPawn())) C->SetSprintIntent(IsInputKeyDown(EKeys::LeftShift));
}
void APersonaPlayerController::HandleTab()
{
    if(bTerminalOpen) { CloseOverlays(); return; }
    if(auto C=Cast<APersonaCharacter>(GetPawn()); C && C->Interaction->LootTarget) C->Interaction->CloseLoot();
}
bool APersonaPlayerController::IsPauseOpen() const { return PauseMenu && PauseMenu->IsInViewport(); }
void APersonaPlayerController::TogglePanel()
{
    if(!IsHost()) return;
    if(GetGameInstance()->GetSubsystem<UPersonaBattleSubsystem>()->Locked())
    { if(auto C=Cast<APersonaCharacter>(GetPawn())) C->Interaction->ClientFeedback(TEXT("Администрирование недоступно во время командного боя."));return; }
    if(bPanelOpen) { CloseOverlays(); return; }
    CloseOverlays();
    if(auto C=Cast<APersonaCharacter>(GetPawn())) { C->Interaction->CancelActions(); if(C->Interaction->LootTarget) C->Interaction->CloseLoot(); }
    bPanelOpen=true;if(Panel) Panel->SetPanelOpen(true);
    bShowMouseCursor=true; FInputModeGameAndUI Mode;if(Panel) Mode.SetWidgetToFocus(Panel->TakeWidget());Mode.SetHideCursorDuringCapture(false);SetInputMode(Mode);
    SetIgnoreLookInput(true);SetIgnoreMoveInput(true);StopTalking();
}
void APersonaPlayerController::ServerGiveItem_Implementation(const FString& ItemPath,int32 Quantity)
{
    if(GetGameInstance()->GetSubsystem<UPersonaBattleSubsystem>()->Locked()) return;
    auto C=Cast<APersonaCharacter>(GetPawn());auto Catalog=UPersonaItemCatalog::Get();
    if(!IsHost() || !C || !C->IsAlive() || C->bTransit || !Catalog || Quantity<1 || Quantity>999) return;
    auto Item=Catalog->Find(ItemPath);if(!Item) return;
    const int32 Requested=Quantity;
    int32 Placed=0;
    while(Quantity>0)
    {
        FPersonaInventoryRecord R;R.Definition=ItemPath;R.Quantity=FMath::Min(Quantity,Item->MaxStack);
        FTransform At=C->GetActorTransform();At.AddToTranslation(C->GetActorForwardVector()*110);
        auto P=UPersonaLoot::Spawn(GetWorld(),R,At);if(!P) break;
        C->Interaction->Store(P);
        if(IsValid(P) && !P->Holder)
        {
            bool Safe=false;
            for(int I=0;I<80 && !Safe;++I)
            {
                const float Angle=I*2.39996f;const float Radius=110+15*I;
                const FVector Start=C->GetActorLocation()+FVector(FMath::Cos(Angle)*Radius,FMath::Sin(Angle)*Radius,120);
                FHitResult Floor;FCollisionQueryParams Q(SCENE_QUERY_STAT(AdminItemFloor),false,C);Q.AddIgnoredActor(P);
                if(!GetWorld()->LineTraceSingleByChannel(Floor,Start,Start-FVector(0,0,700),ECC_Visibility,Q) || Floor.ImpactNormal.Z<.6) continue;
                FTransform Candidate=At;Candidate.SetLocation(Floor.ImpactPoint+FVector(0,0,P->Mesh->Bounds.BoxExtent.Z+8));
                FTransform Clear;
                if(P->FindSafePlacement(Candidate,Start,Clear)) { P->SetActorTransform(Clear,false,nullptr,ETeleportType::TeleportPhysics);P->OnRep_Holder();Safe=true; }
            }
            if(!Safe) { P->Destroy(); break; }
        }
        Quantity-=R.Quantity;Placed+=R.Quantity;
    }
    C->Interaction->ClientFeedback(FString::Printf(TEXT("Выдано: %s ×%d / %d"),*Item->DisplayName.ToString(),Placed,Requested));
}
bool APersonaPlayerController::IsHost() const { return HasAuthority() && IsLocalController(); }
void APersonaPlayerController::ServerAcceptTrial_Implementation(FName TrialId) { if(auto D=APersonaTrialDirector::Get(GetWorld())) D->AcceptTrial(this,TrialId); }
void APersonaPlayerController::ServerArenaReady_Implementation(bool Ready) { bArenaReady=Ready; }
void APersonaPlayerController::ClientTrialTerminal_Implementation()
{
    CloseOverlays();bTerminalOpen=true; if(auto C=Cast<APersonaCharacter>(GetPawn())) C->Interaction->CancelActions();
    SetIgnoreMoveInput(true); SetIgnoreLookInput(true); bShowMouseCursor=true; SetInputMode(FInputModeGameAndUI());
}
void APersonaPlayerController::ServerSetReady_Implementation(bool Ready) { if (auto P = GetPlayerState<APersonaPlayerState>()) P->bReady = Ready; }
void APersonaPlayerController::ServerSetMode_Implementation(EPersonaMode Mode) { if (IsHost()) if (auto GM = GetWorld()->GetAuthGameMode<APersonaGameMode>()) GM->SetMode(Mode); }
void APersonaPlayerController::ServerTravel_Implementation(bool ToTrial) { if (IsHost()) if (auto GM = GetWorld()->GetAuthGameMode<APersonaGameMode>()) GM->Travel(ToTrial); }
void APersonaPlayerController::ServerNPCCount_Implementation(int32 Count) { if (IsHost()) if (auto GM = GetWorld()->GetAuthGameMode<APersonaGameMode>()) GM->SetNPCCount(Count); }
void APersonaPlayerController::ServerTestVitals_Implementation(bool Food)
{
    if(GetGameInstance()->GetSubsystem<UPersonaBattleSubsystem>()->Locked()) return;
#if !UE_BUILD_SHIPPING
    if(!IsHost()) return;
    if (auto C = Cast<APersonaCharacter>(GetPawn()))
    {
        auto ASC = C->GetAbilitySystemComponent();
        if (Food) { auto S = ASC->MakeOutgoingSpec(UPersonaFoodEffect::StaticClass(), 1, ASC->MakeEffectContext()); ASC->ApplyGameplayEffectSpecToSelf(*S.Data.Get()); }
        else if (!C->IsAlive()) C->ReviveAt(C->GetActorLocation()+FVector(0,0,100));
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
bool APersonaPlayerController::IsMuted(APersonaPlayerState* P) const
{ auto Own=GetPlayerState<APersonaPlayerState>();return P && ((GetGameInstance()->GetSubsystem<UPersonaBattleSubsystem>()->IsArena() && Own && P->Team!=Own->Team) || MutedIds.Contains(P->GetUniqueId().ToString())); }
void APersonaPlayerController::ToggleMute(APersonaPlayerState* P)
{
    if (!P || P == PlayerState || !P->GetUniqueId().IsValid()) return;
    if(GetGameInstance()->GetSubsystem<UPersonaBattleSubsystem>()->IsArena() && P->Team!=GetPlayerState<APersonaPlayerState>()->Team) return;
    const FString ID = P->GetUniqueId().ToString(); const bool Mute = !MutedIds.Contains(ID);
    if (Mute) MutedIds.Add(ID); else MutedIds.Remove(ID);
    if (auto OSS = IOnlineSubsystem::Get()) if (auto V = OSS->GetVoiceInterface(); V.IsValid())
    { if (Mute) V->MuteRemoteTalker(0, *P->GetUniqueId().GetUniqueNetId(), false); else V->UnmuteRemoteTalker(0, *P->GetUniqueId().GetUniqueNetId(), false); }
}
void APersonaPlayerController::Tick(float Delta)
{
    Super::Tick(Delta); if (!IsLocalController()) return;
    EnsureLocalPanel();
    if(auto A=APersonaBattleArena::Get(GetWorld()))
    {
        if(!bBattleArenaReadySent && GetPawn() && A->ClientReady()) { bBattleArenaReadySent=true;ServerBattleArenaReady(); }
        auto B=GetGameInstance()->GetSubsystem<UPersonaBattleSubsystem>();
        if(B->Journal && (B->Phase==EPersonaBattlePhase::Prepared || B->Phase==EPersonaBattlePhase::Travelling))
        { B->Journal->Phase=EPersonaBattleJournalPhase::Started;B->SetPhase(EPersonaBattlePhase::Waiting,TEXT("Ожидание готовности арены…")); }
    }
    else bBattleArenaReadySent=false;
    if(bPanelOpen && GetGameInstance()->GetSubsystem<UPersonaBattleSubsystem>()->Locked()) CloseOverlays();
    for (TActorIterator<APersonaCharacter> It(GetWorld()); It; ++It)
        if (!It->IsLocallyControlled() && !It->bNPC) It->Voice->SetOutputVolume(IsMuted(It->GetPlayerState<APersonaPlayerState>()) ? 0 : VoiceVolume);
    if(auto Own=Cast<APersonaCharacter>(GetPawn()))
    {
        if(!Own->IsAlive())
        {
            if(!Spectated.IsValid() || !Spectated->IsAlive() || Spectated->bOffline) CycleSpectator();
            if(Spectated.IsValid() && SpectatorCamera)
            {
                const FVector Eye=Spectated->GetMesh()->GetComponentLocation()+FVector(0,0,Spectated->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+Spectated->Camera->GetRelativeLocation().Z);
                SpectatorCamera->SetActorLocation(FMath::VInterpTo(SpectatorCamera->GetActorLocation(),Eye,Delta,18));
                SpectatorCamera->SetActorRotation(Spectated->GetBaseAimRotation());
                SpectatorCamera->GetCameraComponent()->SetFieldOfView(Spectated->Camera->FieldOfView);
            }
        }
        else if(Spectated.IsValid() || GetViewTarget()!=Own)
        { if(Spectated.IsValid()) Spectated->GetMesh()->SetVisibility(true); Spectated.Reset(); SetViewTarget(Own); }
    }
}
void APersonaPlayerController::CycleSpectator()
{
    auto Own=Cast<APersonaCharacter>(GetPawn()); if(!Own || Own->IsAlive()) return;
    TArray<APersonaCharacter*> Candidates;
    const bool Battle=GetGameInstance()->GetSubsystem<UPersonaBattleSubsystem>()->IsArena();
    for(TActorIterator<APersonaCharacter> It(GetWorld());It;++It) if(!It->bNPC && !It->bOffline && It->IsAlive() && *It!=Own && (!Battle || It->SavedTeam==Own->SavedTeam)) Candidates.Add(*It);
    const int32 Previous=Candidates.IndexOfByKey(Spectated.Get());
    if(Spectated.IsValid()) Spectated->GetMesh()->SetVisibility(true);
    Spectated=Candidates.Num() ? Candidates[(Previous+1)%Candidates.Num()] : nullptr;
    if(Spectated.IsValid())
    {
        Spectated->GetMesh()->SetVisibility(false);
        if(!SpectatorCamera) { FActorSpawnParameters P;P.Owner=this;SpectatorCamera=GetWorld()->SpawnActor<ACameraActor>(P); }
        SpectatorCamera->SetActorLocation(Spectated->Camera->GetComponentLocation());SpectatorCamera->SetActorRotation(Spectated->GetBaseAimRotation());SetViewTarget(SpectatorCamera);
    }
    else SetViewTarget(Own);
}
void APersonaPlayerController::ServerBattleOpen_Implementation(bool Open) { GetGameInstance()->GetSubsystem<UPersonaBattleSubsystem>()->SetOpen(this,Open); }
void APersonaPlayerController::ServerBattleAnswer_Implementation(bool Accept) { GetGameInstance()->GetSubsystem<UPersonaBattleSubsystem>()->Answer(this,Accept); }
void APersonaPlayerController::ServerBattleReady_Implementation(bool Ready) { GetGameInstance()->GetSubsystem<UPersonaBattleSubsystem>()->SetReady(this,Ready); }
void APersonaPlayerController::ServerBattleTicketAck_Implementation(const FString& Match) { GetGameInstance()->GetSubsystem<UPersonaBattleSubsystem>()->AcknowledgeTicket(this,Match); }
void APersonaPlayerController::ServerBattleArenaReady_Implementation()
{ if(APersonaBattleArena::Get(GetWorld()) && GetGameInstance()->GetSubsystem<UPersonaBattleSubsystem>()->Ticket.Contains(GetPlayerState<APersonaPlayerState>()->PersistentId)) bArenaReady=true; }
void APersonaPlayerController::ClientBattleTicket_Implementation(const FPersonaBattleTicket& T,int32 Team)
{ bBattleArenaReadySent=false;GetGameInstance()->GetSubsystem<UPersonaBattleSubsystem>()->AcceptTicket(T,Team); }
void APersonaPlayerController::ClientBattleConnect_Implementation() { GetGameInstance()->GetSubsystem<UPersonaBattleSubsystem>()->ConnectArena(); }
void APersonaPlayerController::ClientBattleReceipt_Implementation(const FString& Match,const FString& Hash,bool Commit) { GetGameInstance()->GetSubsystem<UPersonaBattleSubsystem>()->ReceiveReceiptStatus(Match,Hash,Commit); }
void APersonaPlayerController::ClientBattleReturn_Implementation(bool Aborted) { GetGameInstance()->GetSubsystem<UPersonaBattleSubsystem>()->Return(Aborted); }
