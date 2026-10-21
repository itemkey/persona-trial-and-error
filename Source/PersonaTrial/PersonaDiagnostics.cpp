#include "PersonaDiagnostics.h"
#include "PersonaFramework.h"
#include "PersonaCharacter.h"
#include "PersonaInteraction.h"
#include "PersonaAttributes.h"
#include "PersonaOnline.h"
#include "PersonaAI.h"
#include "BrainComponent.h"
#include "Perception/AISense_Hearing.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EngineUtils.h"
#include "Engine/NetDriver.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Json.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"
#include "DynamicRHI.h"
#include "RenderTimer.h"
#include "Kismet/KismetSystemLibrary.h"

void UPersonaDiagnostics::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
#if !UE_BUILD_SHIPPING
    bEnabled = FParse::Value(FCommandLine::Get(), TEXT("PersonaDiagnostics="), Role)
        && !Role.IsEmpty() && Role.Len() < 32;
    for (TCHAR C : Role) bEnabled &= FChar::IsAlnum(C) || C == TEXT('_');
    Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT(".work/Diagnostics"));
    if (bEnabled) IFileManager::Get().MakeDirectory(*Directory, true);
#endif
}
void UPersonaDiagnostics::Tick(float DeltaTime)
{
    UWorld* World = GetWorld();
    if (!World || !World->IsGameWorld()) return;
    auto PC = Cast<APersonaPlayerController>(GetGameInstance()->GetFirstLocalPlayerController());
    if (!PC || !PC->GetPawn()) return;
    if (!MoveInput.IsNearlyZero())
    {
        PC->GetPawn()->AddMovementInput(PC->GetPawn()->GetActorForwardVector(), MoveInput.X);
        PC->GetPawn()->AddMovementInput(PC->GetPawn()->GetActorRightVector(), MoveInput.Y);
    }
    if (FPlatformTime::Seconds() < NextSample) return;
    NextSample = FPlatformTime::Seconds() + .5;
    FString Text;
    const FString Inbox = Directory / (Role + TEXT(".command"));
    if (FFileHelper::LoadFileToString(Text, *Inbox))
    {
        IFileManager::Get().Delete(*Inbox);
        TSharedPtr<FJsonObject> Command;
        if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Command)) Execute(Command);
    }
    Snapshot();
}
void UPersonaDiagnostics::Execute(const TSharedPtr<FJsonObject>& Command)
{
    auto PC = Cast<APersonaPlayerController>(GetGameInstance()->GetFirstLocalPlayerController());
    auto C = Cast<APersonaCharacter>(PC->GetPawn());
    const FString Op = Command->GetStringField(TEXT("op"));
    UE_LOG(LogTemp, Display, TEXT("PERSONA DIAGNOSTIC %s: %s"), *Role, *Op);
    if (Op == TEXT("ready")) PC->ServerSetReady(true);
    else if (Op == TEXT("mode")) PC->PersonaMode(Command->GetIntegerField(TEXT("value")));
    else if (Op == TEXT("trial")) PC->PersonaTrial();
    else if (Op == TEXT("hub")) PC->PersonaHub();
    else if (Op == TEXT("npcs")) PC->PersonaNPCs(Command->GetIntegerField(TEXT("value")));
    else if (Op == TEXT("damage")) PC->ServerTestVitals(false);
    else if (Op == TEXT("food")) PC->ServerTestVitals(true);
    else if (Op == TEXT("interact")) C->Interaction->TryInteract();
    else if (Op == TEXT("claim"))
    {
        // Deliberately bypass the local trace to exercise server rejection.
        const FString Name = Command->GetStringField(TEXT("actor"));
        for (TActorIterator<APersonaProp> It(GetWorld()); It; ++It)
            if (It->GetName() == Name) C->Interaction->ServerInteract(*It);
    }
    else if (Op == TEXT("throw")) C->Interaction->ServerRelease(true);
    else if (Op == TEXT("drop")) C->Interaction->ServerRelease(false);
    else if (Op == TEXT("leave")) GetGameInstance()->GetSubsystem<UPersonaOnline>()->Leave();
    else if (Op == TEXT("host")) GetGameInstance()->GetSubsystem<UPersonaOnline>()->Host();
    else if (Op == TEXT("find")) GetGameInstance()->GetSubsystem<UPersonaOnline>()->Find();
    else if (Op == TEXT("join")) GetGameInstance()->GetSubsystem<UPersonaOnline>()->Join(0);
    else if (Op == TEXT("panel")) PC->TogglePanel();
    else if (Op == TEXT("sprint")) C->ServerSetSprinting(Command->GetBoolField(TEXT("value")));
    else if (Op == TEXT("crouch")) { if (Command->GetBoolField(TEXT("value"))) C->Crouch(); else C->UnCrouch(); }
    else if (Op == TEXT("jump")) C->Jump();
    else if (Op == TEXT("noise")) UAISense_Hearing::ReportNoiseEvent(C, C->GetActorLocation(), 1.f, C, 2000, TEXT("Diagnostic"));
    else if (Op == TEXT("arms_offset")) C->FirstPersonArms->SetRelativeLocation(FVector(Command->GetNumberField(TEXT("x")),Command->GetNumberField(TEXT("y")),Command->GetNumberField(TEXT("z"))));
    else if (Op == TEXT("place_npc") && PC->HasAuthority())
    {
        for (TActorIterator<APersonaCharacter> It(GetWorld()); It; ++It)
            if (It->bNPC && It->GetName() == Command->GetStringField(TEXT("actor")))
                It->TeleportTo(FVector(Command->GetNumberField(TEXT("x")),Command->GetNumberField(TEXT("y")),Command->GetNumberField(TEXT("z"))),
                    FRotator(0,Command->GetNumberField(TEXT("yaw")),0),false,true);
    }
    else if (Op == TEXT("move")) MoveInput = FVector(Command->GetNumberField(TEXT("x")), Command->GetNumberField(TEXT("y")), 0);
    else if (Op == TEXT("look")) PC->SetControlRotation(FRotator(Command->GetNumberField(TEXT("pitch")),Command->GetNumberField(TEXT("yaw")),0));
    else if (Op == TEXT("place") && PC->HasAuthority())
    {
        const int32 ID = Command->GetIntegerField(TEXT("player"));
        for (FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator(); It; ++It)
        {
            auto Other = It->Get();
            if (Other->PlayerState && Other->PlayerState->GetPlayerId()==ID && Other->GetPawn())
            {
                Other->GetPawn()->TeleportTo(FVector(Command->GetNumberField(TEXT("x")),Command->GetNumberField(TEXT("y")),Command->GetNumberField(TEXT("z"))),
                    FRotator(0,Command->GetNumberField(TEXT("yaw")),0),false,true);
                const FRotator View(Command->GetNumberField(TEXT("pitch")),Command->GetNumberField(TEXT("yaw")),0);
                Other->SetControlRotation(View); Other->ClientSetRotation(View,true);
            }
        }
    }
    else if (Op == TEXT("shot"))
    {
        const FString Path=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("Artifacts/Previews")/(Role+TEXT("_game.png")));
        FScreenshotRequest::RequestScreenshot(Path, true, false);
    }
    else if (Op == TEXT("quit")) UKismetSystemLibrary::QuitGame(this,PC,EQuitPreference::Quit,false);
}
void UPersonaDiagnostics::Snapshot()
{
    auto Root = MakeShared<FJsonObject>();
    auto World=GetWorld();
    auto PC=Cast<APersonaPlayerController>(GetGameInstance()->GetFirstLocalPlayerController());
    auto GS=World->GetGameState<APersonaGameState>();
    Root->SetStringField(TEXT("map"),World->GetMapName());
    Root->SetStringField(TEXT("utc"),FDateTime::UtcNow().ToIso8601());
    Root->SetStringField(TEXT("status"),CastChecked<UPersonaGameInstance>(GetGameInstance())->Status);
    Root->SetNumberField(TEXT("netMode"),World->GetNetMode());
    Root->SetNumberField(TEXT("localPlayer"),PC->PlayerState ? PC->PlayerState->GetPlayerId() : -1);
    Root->SetBoolField(TEXT("hudInViewport"),PC->IsPanelInViewport());
    Root->SetNumberField(TEXT("frameMs"),FApp::GetDeltaTime()*1000);
    Root->SetNumberField(TEXT("gameThreadMs"),FPlatformTime::ToMilliseconds(GGameThreadTime));
    Root->SetNumberField(TEXT("renderThreadMs"),FPlatformTime::ToMilliseconds(GRenderThreadTime));
    Root->SetNumberField(TEXT("gpuMs"),GDynamicRHI ? FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()) : 0);
    Root->SetNumberField(TEXT("screenPercentage"),IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"))->GetFloat());
    Root->SetNumberField(TEXT("npcCount"),GS ? GS->NPCCount : -1);
    Root->SetNumberField(TEXT("mode"),GS ? static_cast<int32>(GS->Mode) : -1);
    if (auto Viewport = GetGameInstance()->GetGameViewportClient(); Viewport && Viewport->Viewport)
    {
        const FIntPoint Size = Viewport->Viewport->GetSizeXY();
        Root->SetNumberField(TEXT("viewportWidth"), Size.X);
        Root->SetNumberField(TEXT("viewportHeight"), Size.Y);
    }
    if (auto Driver=World->GetNetDriver())
    {
        Driver->bCollectNetStats = true;
        Root->SetStringField(TEXT("driver"),Driver->GetClass()->GetName());
        Root->SetNumberField(TEXT("inBytesSec"),Driver->InBytesPerSecond);
        Root->SetNumberField(TEXT("outBytesSec"),Driver->OutBytesPerSecond);
#if DO_ENABLE_NET_TEST
        Root->SetNumberField(TEXT("packetLagMs"),Driver->PacketSimulationSettings.PktLag);
        Root->SetNumberField(TEXT("packetLossPercent"),Driver->PacketSimulationSettings.PktLoss);
#endif
    }
    if (auto Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
    {
        Root->SetBoolField(TEXT("navReady"),Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)!=nullptr);
        FNavLocation Point;
        Root->SetBoolField(TEXT("navProjected"),Nav->ProjectPointToNavigation(FVector(1800,0,100),Point,FVector(1600,1600,1000)));
        Root->SetBoolField(TEXT("navBuilding"),Nav->IsNavigationBuildInProgress());
        if (auto Data=Cast<ARecastNavMesh>(Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)))
        {
            Root->SetNumberField(TEXT("navTiles"),Data->GetNumActiveTiles());
            Root->SetStringField(TEXT("navBounds"),Data->GetNavMeshBounds().ToString());
        }
    }
    TArray<TSharedPtr<FJsonValue>> Players, Props, Doors;
    for (TActorIterator<APersonaCharacter> It(World); It; ++It)
    {
        auto Item=MakeShared<FJsonObject>();
        auto State=It->GetPlayerState<APersonaPlayerState>();
        Item->SetStringField(TEXT("actor"),It->GetName());
        Item->SetNumberField(TEXT("id"),State ? State->GetPlayerId() : -1);
        Item->SetBoolField(TEXT("npc"),It->bNPC);
        Item->SetNumberField(TEXT("team"),State ? State->Team : -1);
        Item->SetBoolField(TEXT("ready"),State && State->bReady);
        Item->SetNumberField(TEXT("health"),It->GetVitals()->GetHealth());
        Item->SetNumberField(TEXT("stamina"),It->GetVitals()->GetStamina());
        Item->SetNumberField(TEXT("hunger"),It->GetVitals()->GetHunger());
        Item->SetNumberField(TEXT("speed"),It->GetVelocity().Size2D());
        Item->SetBoolField(TEXT("crouched"),It->bIsCrouched);
        Item->SetBoolField(TEXT("falling"),It->GetCharacterMovement()->IsFalling());
        if (auto AI = Cast<APersonaAIController>(It->GetController()))
        {
            Item->SetStringField(TEXT("target"),GetNameSafe(AI->Target));
            Item->SetBoolField(TEXT("investigating"),AI->bInvestigating);
            Item->SetStringField(TEXT("lastSense"),AI->LastSense.ToString());
            Item->SetBoolField(TEXT("brainRunning"),AI->GetBrainComponent() && AI->GetBrainComponent()->IsRunning());
        }
        Item->SetStringField(TEXT("position"),It->GetActorLocation().ToString());
        Item->SetStringField(TEXT("head"),It->GetMesh()->GetBoneLocation(TEXT("head")).ToString());
        Item->SetStringField(TEXT("footL"),It->GetMesh()->GetBoneLocation(TEXT("foot_l")).ToString());
        Item->SetStringField(TEXT("mesh"),GetNameSafe(It->GetMesh()->GetSkeletalMeshAsset()));
        Item->SetStringField(TEXT("animation"),GetNameSafe(It->GetMesh()->GetAnimInstance()));
        Item->SetStringField(TEXT("held"),GetNameSafe(It->Interaction->Held));
        if (It->IsLocallyControlled())
        {
            const FTransform CameraTransform=It->Camera->GetComponentTransform();
            Item->SetStringField(TEXT("handInView"),CameraTransform.InverseTransformPosition(It->FirstPersonArms->GetBoneLocation(TEXT("hand_r"))).ToString());
            Item->SetStringField(TEXT("armsAnim"),GetNameSafe(It->FirstPersonArms->GetAnimInstance()));
            Item->SetBoolField(TEXT("armsVisible"),It->FirstPersonArms->IsVisible());
        }
        Players.Add(MakeShared<FJsonValueObject>(Item));
    }
    for (TActorIterator<APersonaProp> It(World); It; ++It)
    {
        auto Item=MakeShared<FJsonObject>();
        auto Holder=It->Holder ? It->Holder->GetPlayerState() : nullptr;
        Item->SetStringField(TEXT("actor"),It->GetName());
        Item->SetNumberField(TEXT("holder"),Holder ? Holder->GetPlayerId() : -1);
        Item->SetStringField(TEXT("position"),It->GetActorLocation().ToString());
        Props.Add(MakeShared<FJsonValueObject>(Item));
    }
    for (TActorIterator<APersonaDoor> It(World); It; ++It)
    {
        auto Item=MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("actor"),It->GetName());
        Item->SetBoolField(TEXT("open"),It->bOpen);
        Item->SetStringField(TEXT("position"),It->GetActorLocation().ToString());
        Doors.Add(MakeShared<FJsonValueObject>(Item));
    }
    Root->SetArrayField(TEXT("characters"),Players);
    Root->SetArrayField(TEXT("props"),Props);
    Root->SetArrayField(TEXT("doors"),Doors);
    FString Text;
    FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Text));
    const FString Target=Directory/(Role+TEXT(".json"));
    FFileHelper::SaveStringToFile(Text,*(Target+TEXT(".tmp")));
    IFileManager::Get().Move(*Target,*(Target+TEXT(".tmp")),true,true);
}
