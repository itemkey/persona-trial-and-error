#include "PersonaDiagnostics.h"
#include "PersonaFramework.h"
#include "PersonaCharacter.h"
#include "PersonaInteraction.h"
#include "PersonaAttributes.h"
#include "PersonaOnline.h"
#include "PersonaAI.h"
#include "PersonaSave.h"
#include "PersonaHub.h"
#include "PersonaLoot.h"
#include "PersonaWeapon.h"
#include "Kismet/GameplayStatics.h"
#include "PersonaMenu.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "BrainComponent.h"
#include "Perception/AISense_Hearing.h"
#include "Perception/AIPerceptionComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
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
#include "Navigation/PathFollowingComponent.h"
#include "NavMesh/RecastNavMesh.h"
#include "DynamicRHI.h"
#include "AudioMixerBlueprintLibrary.h"
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
    auto PC = GetGameInstance()->GetFirstLocalPlayerController();
    if (!PC) return;
    if(CaptureDuration>0)
    {
        const double Elapsed=FPlatformTime::Seconds()-CaptureStart;
        if(Elapsed>=CaptureDuration)
        {
            UAudioMixerBlueprintLibrary::StopRecordingOutput(this,EAudioRecordingExportType::WavFile,TEXT("Audio"),CaptureDirectory);
            auto Record=MakeShared<FJsonObject>(); Record->SetNumberField(TEXT("duration"),Elapsed);
            TArray<TSharedPtr<FJsonValue>> Times; for(double T:CaptureTimes) Times.Add(MakeShared<FJsonValueNumber>(T)); Record->SetArrayField(TEXT("times"),Times);
            FString Json; FJsonSerializer::Serialize(Record,TJsonWriterFactory<>::Create(&Json));
            FFileHelper::SaveStringToFile(Json,*(CaptureDirectory/TEXT("frames.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
            CaptureDuration=0;
        }
        else if(Elapsed>=CaptureNext && !FScreenshotRequest::IsScreenshotRequested())
        {
            FScreenshotRequest::RequestScreenshot(CaptureDirectory/FString::Printf(TEXT("Frame_%04d.png"),CaptureTimes.Num()),true,false);
            CaptureTimes.Add(Elapsed); CaptureNext=Elapsed+1.0/12.0;
        }
    }
    if (SpinRate != 0) { auto View=PC->GetControlRotation(); View.Yaw += SpinRate*DeltaTime; PC->SetControlRotation(View); }
    if (auto C=Cast<APersonaCharacter>(PC->GetPawn()); C && C->Interaction->Held && C->CarryAlpha>.99f)
    {
        auto Held=C->Interaction->Held;
        if (Held->OwnerViewMesh->GetAttachParent()==C->Camera)
        {
            const auto Actual=Held->OwnerViewMesh->GetComponentTransform().GetRelativeTransform(C->Camera->GetComponentTransform());
            const auto Expected=Held->CarryLocal();
            MaxViewPositionError=FMath::Max(MaxViewPositionError,FVector::Distance(Actual.GetLocation(),Expected.GetLocation()));
            MaxViewRotationError=FMath::Max(MaxViewRotationError,static_cast<double>(FMath::RadiansToDegrees(Actual.GetRotation().AngularDistance(Expected.GetRotation()))));
            ++ViewSamples;
        }
    }
    if (PC->GetPawn() && !MoveInput.IsNearlyZero())
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
    auto BasePC = GetGameInstance()->GetFirstLocalPlayerController();
    const FString CommonOp = Command->GetStringField(TEXT("op"));
    auto Saves = GetGameInstance()->GetSubsystem<UPersonaSaveSubsystem>();
    auto Online = GetGameInstance()->GetSubsystem<UPersonaOnline>();
    if (CommonOp == TEXT("menu"))
    {
        TArray<UUserWidget*> Widgets; UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this,Widgets,UPersonaMenu::StaticClass(),true);
        for (auto Widget : Widgets) CastChecked<UPersonaMenu>(Widget)->Activate(Command->GetStringField(TEXT("action")),Command->HasField(TEXT("index")) ? Command->GetIntegerField(TEXT("index")) : 0);
        return;
    }
    if (CommonOp == TEXT("save")) { Saves->SaveWorld(); return; }
    if(CommonOp==TEXT("record") && FApp::CanEverRender() && CaptureDuration==0)
    {
        CaptureDirectory=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT(".work/Capture")/(Role+TEXT("_")+FGuid::NewGuid().ToString(EGuidFormats::Digits)));
        IFileManager::Get().MakeDirectory(*CaptureDirectory,true); CaptureTimes.Reset(); CaptureNext=0; CaptureStart=FPlatformTime::Seconds();
        CaptureDuration=FMath::Clamp(Command->GetNumberField(TEXT("seconds")),1.,20.);
        UAudioMixerBlueprintLibrary::StartRecordingOutput(this,CaptureDuration); return;
    }
    if (CommonOp == TEXT("console")) { if(BasePC) BasePC->ConsoleCommand(Command->GetStringField(TEXT("value")),true); return; }
    if (CommonOp == TEXT("host")) { Online->Host(); return; }
    if (CommonOp == TEXT("find")) { Online->Find(); return; }
    if (CommonOp == TEXT("join")) { Online->Join(0); return; }
    if (CommonOp == TEXT("leave")) { Online->Leave(); return; }
    if (CommonOp == TEXT("connect")) { BasePC->ClientTravel(TEXT("127.0.0.1:7777"),TRAVEL_Absolute); return; }
    if (CommonOp == TEXT("shot"))
    {
        const FString Path=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("Artifacts/Previews")/(Role+TEXT("_game.png")));
        FScreenshotRequest::RequestScreenshot(Path,true,false); return;
    }
    if (CommonOp == TEXT("quit")) { Saves->SaveWorld(); UKismetSystemLibrary::QuitGame(this,BasePC,EQuitPreference::Quit,false); return; }
    auto PC = Cast<APersonaPlayerController>(GetGameInstance()->GetFirstLocalPlayerController());
    if (!PC || !PC->GetPawn()) return;
    auto C = Cast<APersonaCharacter>(PC->GetPawn());
    const FString Op = Command->GetStringField(TEXT("op"));
    UE_LOG(LogTemp, Display, TEXT("PERSONA DIAGNOSTIC %s: %s"), *Role, *Op);
    if (Op == TEXT("ready")) PC->ServerSetReady(true);
    else if(Op==TEXT("decision")) PC->ServerTrialDecision(Command->GetBoolField(TEXT("accept")));
    else if(Op==TEXT("fire")) C->Weapon->Fire();
    else if(Op==TEXT("aim")) C->Weapon->SetAim(Command->GetBoolField(TEXT("held")));
    else if(Op==TEXT("reload")) C->Weapon->Load(Command->GetBoolField(TEXT("held")));
    else if(Op==TEXT("unload")) C->Weapon->Unload(Command->GetBoolField(TEXT("held")));
    else if(Op==TEXT("pump")) C->Weapon->Pump();
    else if(Op==TEXT("safety")) C->Weapon->Safety();
    else if(Op==TEXT("inspect")) C->Weapon->Inspect();
    else if(Op==TEXT("charge")) { if(Command->GetBoolField(TEXT("held"))) C->Interaction->BeginDrop(); else C->Interaction->EndDrop(); }
    else if(Op==TEXT("cancel")) C->Interaction->CancelActions();
    else if(Op==TEXT("close_loot")) C->Interaction->CloseLoot();
    else if(Op==TEXT("loot"))
    {
        AActor* Target=nullptr;
        for(TActorIterator<AActor> It(GetWorld());It;++It) if(It->GetName()==Command->GetStringField(TEXT("actor"))) { Target=*It; break; }
        if(Target) C->Interaction->ServerLoot(Target,Command->GetIntegerField(TEXT("index")),Command->GetBoolField(TEXT("deposit")),Command->GetStringField(TEXT("expectedId")));
    }
    else if(Op==TEXT("lift") && PC->HasAuthority()) { if(auto D=APersonaTrialDirector::Get(GetWorld())) D->UseLift(C); }
    else if(Op==TEXT("board") && PC->HasAuthority())
    {
        FVector Base(0,0,-500); for(TActorIterator<APersonaHubObject> It(GetWorld());It;++It) if(It->Definition && It->Definition->Kind==EPersonaHubKind::Lift) Base=It->GetActorLocation();
        int32 Index=0; for(TActorIterator<APersonaCharacter> It(GetWorld());It;++It) if(!It->bNPC && !It->bOffline && It->IsAlive())
        { It->SetActorLocation(Base+FVector((Index%2)*140-70,(Index/2)*140-70,95),false,nullptr,ETeleportType::TeleportPhysics); It->GetCharacterMovement()->StopMovementImmediately(); ++Index; }
    }
    else if(Op==TEXT("finish_wave") && PC->HasAuthority())
    { for(TActorIterator<APersonaCharacter> It(GetWorld());It;++It) if(It->bNPC && It->IsAlive()) UGameplayStatics::ApplyDamage(*It,10000,PC,C,UDamageType::StaticClass()); }
    else if(Op==TEXT("kill_player") && PC->HasAuthority())
    { for(TActorIterator<APersonaCharacter> It(GetWorld());It;++It) if(auto P=It->GetPlayerState(); P && P->GetPlayerId()==Command->GetIntegerField(TEXT("player"))) UGameplayStatics::ApplyDamage(*It,10000,PC,C,UDamageType::StaticClass()); }
    else if(Op==TEXT("fixture") && PC->HasAuthority())
    {
        APersonaCharacter* Recipient=C;
        if(Command->HasField(TEXT("player"))) for(TActorIterator<APersonaCharacter> It(GetWorld());It;++It) if(auto S=It->GetPlayerState(); S && S->GetPlayerId()==Command->GetIntegerField(TEXT("player"))) Recipient=*It;
        FPersonaInventoryRecord R; R.Definition=TEXT("/Game/Persona/Items/")+Command->GetStringField(TEXT("definition")); R.Quantity=Command->GetIntegerField(TEXT("quantity"));
        auto P=UPersonaLoot::Spawn(GetWorld(),R,FTransform(Recipient->GetActorLocation()+FVector(100,0,50)));
        if(P && Command->GetBoolField(TEXT("store"))) Recipient->Interaction->Store(P);
    }
    else if(Op==TEXT("gesture"))
    {
        C->Interaction->BeginDrop(); FTimerHandle H; TWeakObjectPtr<APersonaCharacter> Weak=C;
        GetWorld()->GetTimerManager().SetTimer(H,[Weak](){ if(Weak.IsValid()) Weak->Interaction->EndDrop(); },FMath::Clamp(float(Command->GetNumberField(TEXT("seconds"))),.01f,3.f),false);
    }
    else if(Op==TEXT("probe_gate"))
    {
        for(float X:{1400.f,1800.f,2200.f,2600.f,3000.f})
        {
            FHitResult Hit; FCollisionQueryParams Q(SCENE_QUERY_STAT(ProbeGate),true);
            const bool Blocked=GetWorld()->LineTraceSingleByChannel(Hit,FVector(X,-6600,-12500),FVector(X,-5400,-12500),ECC_Visibility,Q);
            UE_LOG(LogTemp,Display,TEXT("GATE_RAY x=%.0f blocked=%d actor=%s component=%s at=%s"),X,Blocked,*GetNameSafe(Hit.GetActor()),*GetNameSafe(Hit.GetComponent()),*Hit.ImpactPoint.ToString());
        }
    }
    else if (Op == TEXT("mode")) PC->PersonaMode(Command->GetIntegerField(TEXT("value")));
    else if (Op == TEXT("trial")) PC->PersonaTrial();
    else if (Op == TEXT("hub")) PC->PersonaHub();
    else if (Op == TEXT("npcs")) PC->PersonaNPCs(Command->GetIntegerField(TEXT("value")));
    else if (Op == TEXT("damage")) PC->ServerTestVitals(false);
    else if (Op == TEXT("food")) PC->ServerTestVitals(true);
    else if (Op == TEXT("interact")) C->Interaction->TryInteract();
    else if (Op == TEXT("slot")) C->Interaction->ServerSelectSlot(Command->GetIntegerField(TEXT("value")));
    else if (Op == TEXT("spin")) { SpinRate=Command->GetNumberField(TEXT("value")); MaxViewPositionError=0; MaxViewRotationError=0; ViewSamples=0; }
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
    else if (Op == TEXT("jump"))
    {
        C->Jump();
        if (Command->HasField(TEXT("shotDelay")))
        {
            const FString Path=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("Artifacts/Previews")/(Role+TEXT("_game.png")));
            FTimerHandle Timer;
            GetWorld()->GetTimerManager().SetTimer(Timer,[Path] { FScreenshotRequest::RequestScreenshot(Path,true,false); },
                FMath::Clamp(Command->GetNumberField(TEXT("shotDelay")),.05,1.0),false);
        }
    }
    else if (Op == TEXT("noise")) UAISense_Hearing::ReportNoiseEvent(C, C->GetActorLocation(), 1.f, C, 2000, TEXT("Diagnostic"));
    else if (Op == TEXT("arms_offset")) C->FirstPersonArms->SetRelativeLocation(FVector(Command->GetNumberField(TEXT("x")),Command->GetNumberField(TEXT("y")),Command->GetNumberField(TEXT("z"))));
    else if (Op == TEXT("place_npc") && PC->HasAuthority())
    {
        for (TActorIterator<APersonaCharacter> It(GetWorld()); It; ++It)
            if (It->bNPC && It->GetName() == Command->GetStringField(TEXT("actor")))
            {
                It->TeleportTo(FVector(Command->GetNumberField(TEXT("x")),Command->GetNumberField(TEXT("y")),Command->GetNumberField(TEXT("z"))),
                    FRotator(0,Command->GetNumberField(TEXT("yaw")),0),false,true);
                if (auto AI = Cast<APersonaAIController>(It->GetController()))
                {
                    AI->StopMovement(); AI->Target = nullptr; AI->ClearFocus(EAIFocusPriority::Gameplay);
                    AI->SetControlRotation(FRotator(0,Command->GetNumberField(TEXT("yaw")),0));
                    AI->Senses->ForgetAll();
                }
            }
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
    auto PC=GetGameInstance()->GetFirstLocalPlayerController();
    auto GS=World->GetGameState<APersonaGameState>();
    Root->SetStringField(TEXT("map"),World->GetMapName());
    Root->SetStringField(TEXT("utc"),FDateTime::UtcNow().ToIso8601());
    Root->SetStringField(TEXT("captureDirectory"),CaptureDirectory); Root->SetBoolField(TEXT("recording"),CaptureDuration>0); Root->SetNumberField(TEXT("captureFrames"),CaptureTimes.Num());
    Root->SetStringField(TEXT("status"),CastChecked<UPersonaGameInstance>(GetGameInstance())->Status);
    Root->SetNumberField(TEXT("netMode"),World->GetNetMode());
    Root->SetNumberField(TEXT("localPlayer"),PC->PlayerState ? PC->PlayerState->GetPlayerId() : -1);
    auto GamePC = Cast<APersonaPlayerController>(PC);
    Root->SetBoolField(TEXT("hudInViewport"),GamePC && GamePC->IsPanelInViewport());
    Root->SetBoolField(TEXT("arenaClientReady"),GamePC && GamePC->bArenaReady);
    if(auto D=APersonaTrialDirector::Get(World))
    {
        auto Run=MakeShared<FJsonObject>(); Run->SetNumberField(TEXT("phase"),int(D->Run.Phase)); Run->SetNumberField(TEXT("lift"),int(D->Run.Lift));
        Run->SetNumberField(TEXT("wave"),D->Run.Wave); Run->SetNumberField(TEXT("remainingNPC"),D->RemainingNPC); Run->SetBoolField(TEXT("loaded"),D->ArenaReady());
        Run->SetNumberField(TEXT("respect"),D->Run.Respect); Run->SetNumberField(TEXT("penalty"),D->Run.Penalty); Run->SetNumberField(TEXT("bonus"),D->Run.Bonus);
        Run->SetBoolField(TEXT("rewarded"),D->Run.Rewarded); Run->SetBoolField(TEXT("departed"),D->Run.Departed);
        Run->SetNumberField(TEXT("remaining"),FMath::Max(0.f,D->PhaseEnd-(GS ? GS->GetServerWorldTimeSeconds() : World->GetTimeSeconds())));
        Run->SetNumberField(TEXT("progress"),D->Run.LiftProgress); Run->SetNumberField(TEXT("cargo"),D->Run.Cargo.Num()); Run->SetNumberField(TEXT("passengers"),D->Run.Passengers.Num());
        Root->SetObjectField(TEXT("run"),Run);
    }
    auto Saves = GetGameInstance()->GetSubsystem<UPersonaSaveSubsystem>();
    Root->SetStringField(TEXT("sector"),Saves->Active ? Saves->Active->Slot : TEXT(""));
    Root->SetStringField(TEXT("saveError"),Saves->LastError);
    Root->SetNumberField(TEXT("sessions"),GetGameInstance()->GetSubsystem<UPersonaOnline>()->Results.Num());
    Root->SetBoolField(TEXT("onlineBusy"),GetGameInstance()->GetSubsystem<UPersonaOnline>()->bBusy);
    Root->SetNumberField(TEXT("viewSamples"),ViewSamples);
    Root->SetNumberField(TEXT("maxViewPositionError"),MaxViewPositionError);
    Root->SetNumberField(TEXT("maxViewRotationError"),MaxViewRotationError);
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
        Item->SetBoolField(TEXT("offline"),It->bOffline);
        Item->SetBoolField(TEXT("dead"),It->bDead); Item->SetBoolField(TEXT("spectator"),It->bSpectator); Item->SetBoolField(TEXT("transit"),It->bTransit);
        Item->SetNumberField(TEXT("poseBones"),It->CorpsePose.Num()); Item->SetNumberField(TEXT("maxHealth"),It->GetVitals()->GetMaxHealth());
        Item->SetNumberField(TEXT("weaponAction"),int(It->Weapon->Action)); Item->SetNumberField(TEXT("throwAlpha"),It->Interaction->ThrowAlpha());
        Item->SetNumberField(TEXT("corpseLoot"),It->Loot->Items.Num()); Item->SetStringField(TEXT("lootTarget"),GetNameSafe(It->Interaction->LootTarget));
        TArray<TSharedPtr<FJsonValue>> BodyLoot;
        for(const auto& R:It->Loot->Items) { auto Entry=MakeShared<FJsonObject>(); Entry->SetStringField(TEXT("id"),R.Id); Entry->SetStringField(TEXT("definition"),R.Definition); Entry->SetNumberField(TEXT("quantity"),R.Quantity); BodyLoot.Add(MakeShared<FJsonValueObject>(Entry)); }
        Item->SetArrayField(TEXT("lootItems"),BodyLoot);
        Item->SetStringField(TEXT("persistentId"),It->PersistentId);
        Item->SetNumberField(TEXT("carryAlpha"),It->CarryAlpha);
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
            Item->SetNumberField(TEXT("pathStatus"),int(AI->GetMoveStatus()));
            if(auto Path=AI->GetPathFollowingComponent()->GetPath(); Path.IsValid()) { Item->SetBoolField(TEXT("partialPath"),Path->IsPartial()); Item->SetStringField(TEXT("pathEnd"),Path->GetEndLocation().ToString()); }
        }
        Item->SetStringField(TEXT("position"),It->GetActorLocation().ToString());
        Item->SetStringField(TEXT("view"),It->GetBaseAimRotation().ToString());
        Item->SetStringField(TEXT("head"),It->GetMesh()->GetBoneLocation(TEXT("head")).ToString());
        Item->SetStringField(TEXT("footL"),It->GetMesh()->GetBoneLocation(TEXT("foot_l")).ToString());
        Item->SetStringField(TEXT("mesh"),GetNameSafe(It->GetMesh()->GetSkeletalMeshAsset()));
        Item->SetStringField(TEXT("animation"),GetNameSafe(It->GetMesh()->GetAnimInstance()));
        Item->SetStringField(TEXT("held"),GetNameSafe(It->Interaction->Held));
        Item->SetNumberField(TEXT("selectedSlot"),It->Interaction->SelectedSlot);
        TArray<TSharedPtr<FJsonValue>> Slots;
        for (auto Prop : It->Interaction->Slots)
        {
            auto Slot=MakeShared<FJsonObject>();
            Slot->SetStringField(TEXT("actor"),GetNameSafe(Prop));
            Slot->SetStringField(TEXT("definition"),Prop ? GetNameSafe(Prop->Definition) : TEXT("None"));
            Slot->SetNumberField(TEXT("quantity"),Prop ? Prop->Quantity : 0);
            Slots.Add(MakeShared<FJsonValueObject>(Slot));
        }
        Item->SetArrayField(TEXT("slots"),Slots);
        if (It->IsLocallyControlled())
        {
            const FTransform CameraTransform=It->Camera->GetComponentTransform();
            Item->SetStringField(TEXT("handInView"),CameraTransform.InverseTransformPosition(It->FirstPersonArms->GetBoneLocation(TEXT("hand_r"))).ToString());
            if (auto Held = It->Interaction->Held)
            {
                const FTransform Actual = Held->OwnerViewMesh->GetComponentTransform().GetRelativeTransform(CameraTransform);
                const FTransform Expected = Held->CarryLocal();
                Item->SetNumberField(TEXT("heldViewPositionError"),FVector::Distance(Actual.GetLocation(),Expected.GetLocation()));
                Item->SetNumberField(TEXT("heldViewRotationError"),FMath::RadiansToDegrees(Actual.GetRotation().AngularDistance(Expected.GetRotation())));
                Item->SetBoolField(TEXT("heldAttachedToCamera"),Held->OwnerViewMesh->GetAttachParent()==It->Camera);
            }
            Item->SetStringField(TEXT("armsAnim"),GetNameSafe(It->FirstPersonArms->GetAnimInstance()));
            Item->SetBoolField(TEXT("armsVisible"),It->FirstPersonArms->IsVisible());
            Item->SetBoolField(TEXT("armsRendered"),It->FirstPersonArms->WasRecentlyRendered(.5f));
            Item->SetStringField(TEXT("armsBounds"),It->FirstPersonArms->Bounds.ToString());
            Item->SetStringField(TEXT("armsTransform"),It->FirstPersonArms->GetRelativeTransform().ToString());
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
        Item->SetStringField(TEXT("definition"),GetNameSafe(It->Definition));
        Item->SetNumberField(TEXT("quantity"),It->Quantity);
        Item->SetStringField(TEXT("itemId"),It->PersistentItemId); Item->SetNumberField(TEXT("tube"),It->WeaponState.Tube);
        Item->SetNumberField(TEXT("chamber"),int(It->WeaponState.Chamber)); Item->SetBoolField(TEXT("safety"),It->WeaponState.Safety);
        Item->SetNumberField(TEXT("maxStack"),It->StackLimit());
        Item->SetBoolField(TEXT("stowed"),It->bStowed);
        Item->SetStringField(TEXT("mesh"),GetNameSafe(It->Mesh->GetStaticMesh()));
        Item->SetNumberField(TEXT("physicsSpeed"),It->Mesh->GetPhysicsLinearVelocity().Size());
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
    TArray<TSharedPtr<FJsonValue>> Hub;
    for(TActorIterator<APersonaHubObject> It(World);It;++It)
    {
        auto H=MakeShared<FJsonObject>(); H->SetStringField(TEXT("actor"),It->GetName()); H->SetStringField(TEXT("id"),It->PersistentId);
        H->SetNumberField(TEXT("kind"),It->Definition ? int(It->Definition->Kind) : -1); H->SetStringField(TEXT("position"),It->GetActorLocation().ToString());
        auto Bounds=It->GetComponentsBoundingBox(true); H->SetStringField(TEXT("boundsMin"),Bounds.Min.ToString()); H->SetStringField(TEXT("boundsMax"),Bounds.Max.ToString());
        H->SetBoolField(TEXT("open"),It->bOpen); H->SetBoolField(TEXT("powered"),It->bPowered);
        TArray<TSharedPtr<FJsonValue>> Loot;
        for(const auto& R:It->Loot->Items) { auto I=MakeShared<FJsonObject>(); I->SetStringField(TEXT("id"),R.Id); I->SetStringField(TEXT("definition"),R.Definition); I->SetNumberField(TEXT("quantity"),R.Quantity); Loot.Add(MakeShared<FJsonValueObject>(I)); }
        H->SetArrayField(TEXT("items"),Loot); Hub.Add(MakeShared<FJsonValueObject>(H));
    }
    Root->SetArrayField(TEXT("hub"),Hub);
    FString Text;
    FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Text));
    const FString Target=Directory/(Role+TEXT(".json"));
    FFileHelper::SaveStringToFile(Text,*(Target+TEXT(".tmp")));
    IFileManager::Get().Move(*Target,*(Target+TEXT(".tmp")),true,true);
}
