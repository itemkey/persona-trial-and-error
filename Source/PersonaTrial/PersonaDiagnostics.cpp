#include "PersonaDiagnostics.h"
#include "PersonaFramework.h"
#include "PersonaCharacter.h"
#include "PersonaInteraction.h"
#include "PersonaAttributes.h"
#include "PersonaOnline.h"
#include "PersonaAI.h"
#include "PersonaSave.h"
#include "PersonaBattle.h"
#include "PersonaBattleArena.h"
#include "PersonaHub.h"
#include "PersonaLoot.h"
#include "PersonaWeapon.h"
#include "PersonaMovement.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "PersonaPanel.h"
#include "InputKeyEventArgs.h"
#include "Framework/Application/SlateApplication.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/TextBlock.h"
#include "Components/EditableTextBox.h"
#include "Components/WrapBox.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/DamageEvents.h"
#include "Kismet/GameplayStatics.h"
#include "PersonaMenu.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "BrainComponent.h"
#include "Perception/AISense_Hearing.h"
#include "Perception/AIPerceptionComponent.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
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
void UPersonaDiagnostics::TraceMotion()
{
    const double Now=FPlatformTime::Seconds();
    for(TActorIterator<APersonaCharacter> It(GetWorld());It;++It) if(It->bNPC || (bTracePlayers && !It->bOffline))
    {
        auto F=MakeShared<FJsonObject>(); auto M=It->GetCharacterMovement(); auto Mesh=It->GetMesh();
        F->SetNumberField(TEXT("time"),Now); F->SetNumberField(TEXT("serverTime"),It->ServerTime());
        F->SetStringField(TEXT("id"),It->PersistentId); F->SetStringField(TEXT("capsule"),It->GetActorLocation().ToString());
        F->SetStringField(TEXT("mesh"),Mesh->GetComponentLocation().ToString()); F->SetStringField(TEXT("root"),Mesh->GetBoneLocation(TEXT("root")).ToString());
        F->SetStringField(TEXT("pelvis"),Mesh->GetBoneLocation(TEXT("pelvis")).ToString());
        F->SetStringField(TEXT("head"),Mesh->GetBoneLocation(TEXT("head")).ToString());
        F->SetStringField(TEXT("footL"),Mesh->GetBoneLocation(TEXT("foot_l")).ToString()); F->SetStringField(TEXT("footR"),Mesh->GetBoneLocation(TEXT("foot_r")).ToString());
        F->SetStringField(TEXT("velocity"),It->GetVelocity().ToString()); F->SetStringField(TEXT("acceleration"),M->GetCurrentAcceleration().ToString());
        F->SetNumberField(TEXT("yaw"),It->GetActorRotation().Yaw); F->SetNumberField(TEXT("mode"),M->MovementMode);
        F->SetNumberField(TEXT("floorZ"),M->CurrentFloor.HitResult.ImpactPoint.Z);
        F->SetBoolField(TEXT("npc"),It->bNPC); F->SetNumberField(TEXT("role"),int(It->GetLocalRole()));
        F->SetNumberField(TEXT("smoothing"),int(M->NetworkSmoothingMode));
        F->SetBoolField(TEXT("smoothingComplete"),M->bNetworkSmoothingComplete);
        if(auto Anim=Cast<UPersonaAnimInstance>(Mesh->GetAnimInstance()))
        {
            UObject* Clip=It->Traversal.Active ? Anim->ClimbClip.Get() : M->IsFalling() ? Anim->JumpClip.Get()
                : It->bIsCrouched ? static_cast<UObject*>(Anim->CrouchLocomotion.Get()) : static_cast<UObject*>(Anim->Locomotion.Get());
            if(It->AttackState.Playing && Anim->AttackClips.IsValidIndex(It->AttackState.Variant)) Clip=Anim->AttackClips[It->AttackState.Variant];
            F->SetStringField(TEXT("animation"),GetNameSafe(Clip));
            F->SetNumberField(TEXT("gaitPhase"),Anim->GaitPhase);
            F->SetBoolField(TEXT("footContactL"),Anim->FootContact[0]); F->SetBoolField(TEXT("footContactR"),Anim->FootContact[1]);
        }
        F->SetNumberField(TEXT("collisionRevision"),CastChecked<UPersonaMovement>(M)->CollisionRevision);
        F->SetBoolField(TEXT("dead"),It->bDead); F->SetBoolField(TEXT("crouched"),It->bIsCrouched); F->SetBoolField(TEXT("climbing"),It->Traversal.Active);
        F->SetBoolField(TEXT("floorContact"),M->CurrentFloor.bBlockingHit); F->SetBoolField(TEXT("movementAdjustment"),M->bJustTeleported);
        F->SetNumberField(TEXT("hitSequence"),It->Reaction.Sequence); F->SetNumberField(TEXT("attackSequence"),It->AttackState.Sequence);
        F->SetBoolField(TEXT("attacking"),It->AttackState.Playing);
        if(auto AI=Cast<APersonaAIController>(It->Controller)) { F->SetNumberField(TEXT("pathRevision"),AI->PathRevision); F->SetNumberField(TEXT("tactic"),int(AI->Tactic)); }
        MotionFrames.Add(MakeShared<FJsonValueObject>(F));
    }
    if(Now>=MotionTraceEnd)
    {
        auto Root=MakeShared<FJsonObject>(); Root->SetArrayField(TEXT("frames"),MotionFrames); FString Text;
        FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Text));
        FFileHelper::SaveStringToFile(Text,*(Directory/(Role+TEXT("_motion.json"))),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        MotionFrames.Reset(); MotionTraceEnd=0;
    }
}
void UPersonaDiagnostics::Tick(float DeltaTime)
{
    UWorld* World = GetWorld();
    if (!World || !World->IsGameWorld()) return;
    auto PC = GetGameInstance()->GetFirstLocalPlayerController();
    if (!PC) return;
    if(MotionTraceEnd>0) TraceMotion();
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
            CaptureTimes.Add(Elapsed); CaptureNext=Elapsed+1.0/CaptureFPS;
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
    NextSample = FPlatformTime::Seconds() + SampleInterval;
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
    if(CommonOp==TEXT("load_sector"))
    { if(Saves->SelectSector(Command->GetStringField(TEXT("slot")))) Online->Host(); return; }
    if(CommonOp==TEXT("legacy_trial_save") && Saves->Active && BasePC && BasePC->HasAuthority())
    {
        Saves->SaveWorld(); auto Legacy=DuplicateObject<UPersonaSectorSave>(Saves->Active,this);
        Legacy->Run.TrialId=NAME_None; Legacy->Run.Respect=-30; Legacy->Run.Penalty=3; Legacy->Run.Bonus=20;
        TArray<uint8> Bytes;
        if(UGameplayStatics::SaveGameToMemory(Legacy,Bytes)) FFileHelper::SaveArrayToFile(Bytes,*(Directory/(Role+TEXT("_legacy_trial.sav"))));
        return;
    }
    if (CommonOp == TEXT("save")) { Saves->SaveWorld(); return; }
    if(CommonOp==TEXT("legacy_save") && Saves->Active && BasePC && BasePC->HasAuthority())
    {
        Saves->SaveWorld();
        auto Legacy=DuplicateObject<UPersonaSectorSave>(Saves->Active,this);
        Legacy->Version=3; Legacy->Run.DifficultyPlayers=1; Legacy->Run.PendingNPC=0;
        Legacy->Run.ReinforcementRemaining=0;
        for(auto& Map:Legacy->Maps) for(auto& Hub:Map.Hub)
        {
            for(const auto& Part:Hub.Compartments) Hub.Items.Append(Part.Items);
            Hub.Compartments.Reset();
        }
        TArray<uint8> Bytes;
        if(UGameplayStatics::SaveGameToMemory(Legacy,Bytes)) FFileHelper::SaveArrayToFile(Bytes,*(Directory/(Role+TEXT("_legacy.sav"))));
        return;
    }
    if(CommonOp==TEXT("legacy_interactions_save") && Saves->Active && BasePC && BasePC->HasAuthority())
    {
        Saves->SaveWorld(); auto Legacy=DuplicateObject<UPersonaSectorSave>(Saves->Active,this); Legacy->Version=5;
        FPersonaInventoryRecord Tool; Tool.ActorClass=APersonaProp::StaticClass()->GetPathName();
        Tool.Definition=TEXT("/Game/Persona/Items/DA_HUB_Hammer.DA_HUB_Hammer"); Tool.Id=TEXT("LegacyWorkbenchTool");
        for(auto& Player:Legacy->Players) { Player.Inventory.SetNum(3); Player.Inventory[2]=Tool; }
        for(auto& Map:Legacy->Maps)
        {
            for(auto& Subject:Map.Subjects) if(!Subject.NPC) { Subject.Inventory.SetNum(3); Subject.Inventory[2]=Tool; }
            FPersonaObjectRecord Loose; Loose.ActorClass=Tool.ActorClass; Loose.Definition=TEXT("/Game/Persona/Items/DA_HUB_Wrench.DA_HUB_Wrench"); Loose.Id=TEXT("LegacyLooseWrench"); Loose.Transform=FTransform(FVector(0,1100,150)); Map.Props.Add(Loose);
            for(auto& Hub:Map.Hub)
            {
                if(Hub.Id.StartsWith(TEXT("TRIAL01_Container_")) && !Hub.Compartments.IsEmpty())
                {
                    int32 Index=0; for(const auto& Record:Hub.Items) Hub.Compartments[Index++%Hub.Compartments.Num()].Items.Add(Record);
                    Hub.Items.Reset();
                }
                if(Hub.Id==TEXT("HUB_Chest_01") && !Hub.Compartments.IsEmpty())
                { auto Box=Tool; Box.Definition=TEXT("/Game/Persona/Items/DA_HUB_Toolbox.DA_HUB_Toolbox"); Box.Id=TEXT("LegacyStoredToolbox"); Hub.Compartments[0].Items.Add(Box); }
            }
        }
        TArray<uint8> Bytes;
        if(UGameplayStatics::SaveGameToMemory(Legacy,Bytes)) FFileHelper::SaveArrayToFile(Bytes,*(Directory/(Role+TEXT("_legacy_interactions.sav"))));
        return;
    }
    if(CommonOp==TEXT("sample_interval")) { SampleInterval=FMath::Clamp(float(Command->GetNumberField(TEXT("seconds"))),.02f,2.f); return; }
    if(CommonOp==TEXT("motion_trace")) { bTracePlayers=Command->HasField(TEXT("players")) && Command->GetBoolField(TEXT("players")); MotionFrames.Reset(); MotionTraceEnd=FPlatformTime::Seconds()+FMath::Clamp(Command->GetNumberField(TEXT("seconds")),1.,60.); return; }
    if(CommonOp==TEXT("record") && FApp::CanEverRender() && CaptureDuration==0)
    {
        CaptureDirectory=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT(".work/Capture")/(Role+TEXT("_")+FGuid::NewGuid().ToString(EGuidFormats::Digits)));
        IFileManager::Get().MakeDirectory(*CaptureDirectory,true); CaptureTimes.Reset(); CaptureNext=0; CaptureStart=FPlatformTime::Seconds();
        CaptureDuration=FMath::Clamp(Command->GetNumberField(TEXT("seconds")),1.,20.);
        CaptureFPS=Command->HasField(TEXT("fps")) ? FMath::Clamp(Command->GetNumberField(TEXT("fps")),6.,60.) : 12;
        UAudioMixerBlueprintLibrary::StartRecordingOutput(this,CaptureDuration); return;
    }
    if (CommonOp == TEXT("console")) { if(BasePC) BasePC->ConsoleCommand(Command->GetStringField(TEXT("value")),true); return; }
    if (CommonOp == TEXT("host")) { Online->Host(); return; }
    if (CommonOp == TEXT("find")) { Online->Find(); return; }
    if (CommonOp == TEXT("join")) { Online->Join(0); return; }
    if (CommonOp == TEXT("leave")) { Online->Leave(); return; }
    if (CommonOp == TEXT("connect")) { BasePC->ClientTravel(Command->HasField(TEXT("address")) ? Command->GetStringField(TEXT("address")) : TEXT("127.0.0.1:7777"),TRAVEL_Absolute); return; }
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
    else if(Op==TEXT("battle"))
    {
        auto B=GetGameInstance()->GetSubsystem<UPersonaBattleSubsystem>();const FString Action=Command->GetStringField(TEXT("action"));
        if(Action==TEXT("open")) PC->ServerBattleOpen(true);
        else if(Action==TEXT("find")) B->Find(false);
        else if(Action==TEXT("challenge")) B->Challenge(Command->HasField(TEXT("index")) ? Command->GetIntegerField(TEXT("index")) : 0);
        else if(Action==TEXT("accept")) PC->ServerBattleAnswer(true);
        else if(Action==TEXT("cancel")) PC->ServerBattleAnswer(false);
        else if(Action==TEXT("ready")) PC->ServerBattleReady(true);
        else if(Action==TEXT("lift")) B->UseLift(C);
        else if(Action==TEXT("hold_commit") && PC->HasAuthority()) B->bDiagnosticHoldCommit=Command->GetBoolField(TEXT("hold"));
        else if(Action==TEXT("hold_return") && PC->HasAuthority()) B->bDiagnosticHoldReturn=Command->GetBoolField(TEXT("hold"));
        else if(Action==TEXT("shrink") && PC->HasAuthority()) { if(auto A=APersonaBattleArena::Get(GetWorld())) A->FightAt=A->GetWorld()->GetGameState()->GetServerWorldTimeSeconds()-Command->GetNumberField(TEXT("elapsed")); }
    }
    else if(Op==TEXT("decision")) { if(Command->GetBoolField(TEXT("accept"))) PC->ServerAcceptTrial(Command->HasField(TEXT("trialId")) ? FName(*Command->GetStringField(TEXT("trialId"))) : FName(TEXT("Shooting"))); }
    else if(Op==TEXT("terminal")) PC->ClientTrialTerminal();
    else if(Op==TEXT("admin_give")) PC->ServerGiveItem(Command->GetStringField(TEXT("item")),Command->GetIntegerField(TEXT("quantity")));
    else if(Op==TEXT("npc_drop_fixture") && PC->HasAuthority())
    {
        for(TActorIterator<APersonaCharacter> It(GetWorld());It;++It) if(It->bNPC && It->IsAlive() && It->GetName()==Command->GetStringField(TEXT("actor")))
        {
            auto D=APersonaTrialDirector::Get(GetWorld());const FString Prefix=D ? D->Run.Id : TEXT("Fixture");
            for(int I=0;I<10000;++I)
            {
                const FString Id=Prefix+TEXT("-syringe-")+FString::FromInt(I);
                FRandomStream Random(GetTypeHash(Id+TEXT("/Game/Persona/Items/DA_Syringe.DA_Syringe")));
                if(Random.FRand()<.05f) { It->PersistentId=Id;break; }
            }
        }
    }
    else if(Op==TEXT("ui_text"))
    {
        TArray<UUserWidget*> Panels;UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this,Panels,UPersonaPanel::StaticClass(),false);
        for(auto W:Panels)
        {
            auto Panel=CastChecked<UPersonaPanel>(W);const FString Field=Command->GetStringField(TEXT("field"));
            auto Input=Field==TEXT("trial") ? Panel->TrialSearch : Field==TEXT("item") ? Panel->ItemSearch : Panel->ConsoleInput;
            Input->SetText(FText::FromString(Command->GetStringField(TEXT("text"))));Input->OnTextChanged.Broadcast(Input->GetText());Input->SetKeyboardFocus();
            if(Command->HasField(TEXT("commit")) && Command->GetBoolField(TEXT("commit"))) Input->OnTextCommitted.Broadcast(Input->GetText(),ETextCommit::OnEnter);
        }
    }
    else if(Op==TEXT("ui_click"))
    {
        TArray<UUserWidget*> Panels;UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this,Panels,UUserWidget::StaticClass(),false);
        for(auto Panel:Panels) if(Panel->WidgetTree)
        {
            TArray<UWidget*> Widgets;Panel->WidgetTree->GetAllWidgets(Widgets);
            for(auto W:Widgets) if(auto B=Cast<UButton>(W);B && B->GetIsEnabled())
                if(auto Label=Cast<UTextBlock>(B->GetChildAt(0));Label && Label->GetText().ToString()==Command->GetStringField(TEXT("text"))) { B->OnClicked.Broadcast();return; }
        }
    }
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
        if(Target) C->Interaction->ServerLoot(Target,Command->GetIntegerField(TEXT("index")),Command->GetBoolField(TEXT("deposit")),Command->GetStringField(TEXT("expectedId")),C->Interaction->LootPart);
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
        if(Command->HasField(TEXT("id"))) R.Id=Command->GetStringField(TEXT("id"));
        if(Command->HasField(TEXT("containerState"))) R.ContainerState=Command->GetStringField(TEXT("containerState"));
        const auto At=Command->HasField(TEXT("x")) ? FVector(Command->GetNumberField(TEXT("x")),Command->GetNumberField(TEXT("y")),Command->GetNumberField(TEXT("z"))) : Recipient->GetActorLocation()+FVector(100,0,50);
        auto P=UPersonaLoot::Spawn(GetWorld(),R,FTransform(At));
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
    else if (Op == TEXT("sprint")) C->SetSprintIntent(Command->GetBoolField(TEXT("value")));
    else if(Op==TEXT("key"))
    {
        const FKey Key(*Command->GetStringField(TEXT("key")));
        if(Command->HasField(TEXT("ui")) && Command->GetBoolField(TEXT("ui")) && FSlateApplication::IsInitialized())
        {
            const uint32 Code=Command->HasField(TEXT("code")) ? Command->GetIntegerField(TEXT("code")) : 0;
            const uint32 Character=Command->HasField(TEXT("character")) ? Command->GetIntegerField(TEXT("character")) : 0;
            const bool Repeat=Command->HasField(TEXT("repeat")) && Command->GetBoolField(TEXT("repeat"));
            if(!Command->HasField(TEXT("held")) || Command->GetBoolField(TEXT("held")))
                FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(Key,FModifierKeysState(),0,Repeat,Character,Code));
            if(!Command->HasField(TEXT("held")) || !Command->GetBoolField(TEXT("held")))
                FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(Key,FModifierKeysState(),0,false,Character,Code));
        }
        else
        {
            if(!Command->HasField(TEXT("held")) || Command->GetBoolField(TEXT("held"))) PC->InputKey(FInputKeyEventArgs(nullptr,INPUTDEVICEID_NONE,Key,IE_Pressed,FPlatformTime::Cycles64()));
            if(!Command->HasField(TEXT("held")) || !Command->GetBoolField(TEXT("held"))) PC->InputKey(FInputKeyEventArgs(nullptr,INPUTDEVICEID_NONE,Key,IE_Released,FPlatformTime::Cycles64()));
        }
    }
    else if(Op==TEXT("focus_loot") || Op==TEXT("click_loot"))
    {
        TArray<UUserWidget*> Rows;
        UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this,Rows,UPersonaLootRow::StaticClass(),false);
        for(auto Row:Rows)
        {
            auto LootRow=Cast<UPersonaLootRow>(Row);
            if(!Row->GetParent() || Row->GetOwningPlayer()!=PC || LootRow->Target!=C->Interaction->LootTarget || LootRow->Part!=C->Interaction->LootPart) continue;
            if(Command->HasField(TEXT("expectedId")) && LootRow->ExpectedId!=Command->GetStringField(TEXT("expectedId"))) continue;
            TArray<UWidget*> Widgets; Row->WidgetTree->GetAllWidgets(Widgets);
            for(auto Widget:Widgets) if(auto Button=Cast<UButton>(Widget)) { if(Op==TEXT("click_loot")) Button->OnClicked.Broadcast(); else Button->SetKeyboardFocus(); return; }
        }
    }
    else if(Op==TEXT("clear_inventory") && PC->HasAuthority())
    {
        for(TActorIterator<APersonaCharacter> It(GetWorld());It;++It) if(!It->bNPC)
        {
            It->Interaction->CancelActions();
            const auto Slots=It->Interaction->Slots;
            for(auto Prop:Slots) if(Prop) { It->Interaction->Remove(Prop); Prop->Destroy(); }
        }
    }
    else if(Op==TEXT("open_loot"))
    {
        for(TActorIterator<AActor> It(GetWorld());It;++It)
            if(It->GetName()==Command->GetStringField(TEXT("actor"))) { const auto Hit=C->Interaction->TraceHit(); auto Hub=Cast<APersonaHubObject>(*It); C->Interaction->ServerInteract(*It,Hub ? Hub->HitPart(Hit.GetComponent()) : NAME_None); break; }
    }
    else if(Op==TEXT("hit_volumes"))
    {
        for(TActorIterator<APersonaCharacter> It(GetWorld());It;++It) if(It->bNPC)
            for(auto Box:It->HitVolumes) if(Box) Box->SetHiddenInGame(!Command->GetBoolField(TEXT("value")));
    }
    else if(Op==TEXT("choose_trial") && PC->bTerminalOpen)
    {
        TArray<UUserWidget*> Panels; UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this,Panels,UPersonaPanel::StaticClass(),false);
        for(auto Panel:Panels)
        {
            TArray<UWidget*> Widgets; Panel->WidgetTree->GetAllWidgets(Widgets);
            if(auto D=APersonaTrialDirector::Get(GetWorld())) for(auto Trial:D->Trials) if(Trial && Trial->DisplayName.ToString()==Command->GetStringField(TEXT("name"))) CastChecked<UPersonaPanel>(Panel)->SelectTrial(Trial->TrialId);
            for(auto W:Widgets) if(auto B=Cast<UButton>(W); B && B->GetIsEnabled())
                if(auto Label=Cast<UTextBlock>(B->GetChildAt(0)); Label && Label->GetText().ToString()==TEXT("Принять испытание")) { B->OnClicked.Broadcast(); return; }
        }
    }
    else if(Op==TEXT("forest_path") && PC->HasAuthority())
    {
        ForestPath.Reset();
        const FVector Goal(Command->GetNumberField(TEXT("x")),Command->GetNumberField(TEXT("y")),Command->GetNumberField(TEXT("z")));
        if(auto Route=UNavigationSystemV1::FindPathToLocationSynchronously(this,C->GetActorLocation(),Goal,C); Route && Route->IsValid() && !Route->IsPartial()) ForestPath=Route->PathPoints;
    }
    else if(Op==TEXT("water_fixture") && PC->HasAuthority())
    {
        for(TActorIterator<APersonaCharacter> It(GetWorld());It;++It) if(It->PersistentId==Command->GetStringField(TEXT("id"))) It->Water=Command->GetNumberField(TEXT("water"));
    }
    else if(Op==TEXT("forest_fixture") && PC->HasAuthority())
    {
        auto D=APersonaTrialDirector::Get(GetWorld());
        if(!D || D->Run.Phase!=EPersonaTrialPhase::Forest) return;
        if(Command->HasField(TEXT("raid"))) D->Run.Forest.NextRaid=0;
        if(Command->HasField(TEXT("quiet"))) D->Run.Forest.NextRaid=D->Run.Forest.Elapsed+600;
        if(Command->HasField(TEXT("restock"))) for(auto& Pair:D->NorthHarvest) if(Pair.Value>0) Pair.Value=D->Run.Forest.Elapsed;
        for(TActorIterator<APersonaCharacter> It(GetWorld()); It; ++It)
            if(auto P=D->Run.Forest.Player(It->PersistentId); P && Command->HasField(TEXT("id")) && P->Id==Command->GetStringField(TEXT("id")))
            {
                if(Command->HasField(TEXT("water"))) It->Water=Command->GetNumberField(TEXT("water"));
                if(Command->HasField(TEXT("food"))) It->SetVitals(It->GetVitals()->GetHealth(),100,Command->GetNumberField(TEXT("food")));
            }
    }
    else if(Op==TEXT("vitals") && PC->HasAuthority())
    {
        for(TActorIterator<APersonaCharacter> It(GetWorld());It;++It)
            if((Command->HasField(TEXT("actor")) && It->GetName()==Command->GetStringField(TEXT("actor"))) ||
                (!Command->HasField(TEXT("actor")) && !It->bNPC))
            {
                if(!It->IsAlive()) It->ReviveAt(It->GetActorLocation()+FVector(0,0,100));
                It->SetMaxHealth(Command->GetNumberField(TEXT("health"))); It->SetVitals(Command->GetNumberField(TEXT("health")),100,100);
            }
    }
    else if((Op==TEXT("hit") || Op==TEXT("attack") || Op==TEXT("ai_pause")) && PC->HasAuthority())
    {
        for(TActorIterator<APersonaCharacter> It(GetWorld());It;++It)
            if(It->GetName()==Command->GetStringField(TEXT("actor")))
            {
                if(Op==TEXT("hit"))
                {
                    FHitResult Hit; Hit.BoneName=FName(*Command->GetStringField(TEXT("bone")));
                    Hit.ImpactPoint=It->GetMesh()->GetBoneLocation(Hit.BoneName);
                    UGameplayStatics::ApplyPointDamage(*It,Command->GetNumberField(TEXT("damage")),C->GetActorForwardVector(),Hit,PC,C,UDamageType::StaticClass());
                }
                else if(Op==TEXT("attack")) It->StartMeleeAttack(Command->GetIntegerField(TEXT("variant")));
                else if(auto AI=Cast<APersonaAIController>(It->Controller))
                {
                    if(Command->HasField(TEXT("seed"))) AI->SetTacticalSeed(Command->GetIntegerField(TEXT("seed")));
                    AI->StopMovement(); It->GetCharacterMovement()->StopMovementImmediately(); It->CancelMeleeAttack();
                    if(Command->GetBoolField(TEXT("paused"))) AI->BrainComponent->StopLogic(TEXT("Diagnostic fixture"));
                    else { AI->Target=nullptr; AI->bTargetVisible=false; AI->Senses->ForgetAll(); AI->BrainComponent->RestartLogic(); }
                }
            }
    }
    else if(Op==TEXT("obstacle") && PC->HasAuthority())
    {
        const FVector At(Command->GetNumberField(TEXT("x")),Command->GetNumberField(TEXT("y")),Command->GetNumberField(TEXT("z")));
        auto Wall=GetWorld()->SpawnActor<AStaticMeshActor>(At,FRotator::ZeroRotator);
        Wall->Tags.Add(TEXT("PersonaDiagnosticObstacle")); Wall->SetReplicates(true); Wall->SetReplicateMovement(true);
        auto Mesh=Wall->GetStaticMeshComponent(); Mesh->SetMobility(EComponentMobility::Movable);
        Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
        Mesh->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Persona/Materials/M_Concrete.M_Concrete")));
        Mesh->SetCollisionProfileName(TEXT("BlockAll")); Mesh->SetIsReplicated(true);
        Wall->SetActorScale3D(FVector(Command->GetNumberField(TEXT("sx")),Command->GetNumberField(TEXT("sy")),Command->GetNumberField(TEXT("sz")))/100);
    }
    else if(Op==TEXT("clear_obstacles") && PC->HasAuthority())
    {
        for(TActorIterator<AStaticMeshActor> It(GetWorld());It;++It) if(It->ActorHasTag(TEXT("PersonaDiagnosticObstacle"))) It->Destroy();
    }
    else if(Op==TEXT("npc_crouch") && PC->HasAuthority())
    { for(TActorIterator<APersonaCharacter> It(GetWorld());It;++It) if(It->GetName()==Command->GetStringField(TEXT("actor"))) { if(Command->GetBoolField(TEXT("value"))) It->Crouch(); else It->UnCrouch(); } }
    else if(Op==TEXT("npc_move") && PC->HasAuthority())
    {
        for(TActorIterator<APersonaCharacter> It(GetWorld());It;++It) if(It->bNPC && It->GetName()==Command->GetStringField(TEXT("actor")))
            if(auto AI=Cast<APersonaAIController>(It->Controller))
            {
                It->NPCMoveSpeed=Command->GetNumberField(TEXT("speed")); AI->SetFocus(C);
                AI->MoveToLocation(FVector(Command->GetNumberField(TEXT("x")),Command->GetNumberField(TEXT("y")),It->GetActorLocation().Z),5,false);
            }
    }
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
    auto Battle=GetGameInstance()->GetSubsystem<UPersonaBattleSubsystem>();auto BS=APersonaBattleState::Get(World);auto BState=MakeShared<FJsonObject>();
    BState->SetNumberField(TEXT("phase"),int32(BS ? BS->Phase : Battle->Phase));BState->SetNumberField(TEXT("localPhase"),int32(Battle->Phase));
    BState->SetStringField(TEXT("match"),Battle->Ticket.Match);BState->SetStringField(TEXT("owner"),Battle->Owner());
    BState->SetNumberField(TEXT("journal"),Battle->Journal ? int32(Battle->Journal->Phase) : -1);BState->SetNumberField(TEXT("team"),Battle->Journal ? Battle->Journal->LocalTeam : -1);
    BState->SetBoolField(TEXT("open"),BS && BS->Open);BState->SetNumberField(TEXT("ready"),BS ? BS->Ready : 0);BState->SetNumberField(TEXT("winner"),BS ? BS->Winner : -1);
    BState->SetStringField(TEXT("message"),BS ? BS->Status : FString());
    TArray<TSharedPtr<FJsonValue>> Listing;
    for(const auto& L:Battle->Listings) { auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("owner"),L.Owner);R->SetStringField(TEXT("sector"),L.Sector);R->SetNumberField(TEXT("players"),L.Players);Listing.Add(MakeShared<FJsonValueObject>(R)); }
    BState->SetArrayField(TEXT("listings"),Listing);
    TArray<TSharedPtr<FJsonValue>> Platforms;if(auto A=APersonaBattleArena::Get(World)) { BState->SetNumberField(TEXT("halfExtent"),A->HalfExtent);for(auto P:A->Platforms) if(P) Platforms.Add(MakeShared<FJsonValueString>(P->GetActorLocation().ToString())); }
    BState->SetArrayField(TEXT("platforms"),Platforms);Root->SetObjectField(TEXT("battle"),BState);
    Root->SetNumberField(TEXT("localPlayer"),PC->PlayerState ? PC->PlayerState->GetPlayerId() : -1);
    auto GamePC = Cast<APersonaPlayerController>(PC);
    Root->SetBoolField(TEXT("hudInViewport"),GamePC && GamePC->IsPanelInViewport());
    Root->SetBoolField(TEXT("terminalOpen"),GamePC && GamePC->bTerminalOpen);
    TArray<UUserWidget*> Menus;UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this,Menus,UPersonaMenu::StaticClass(),true);
    Root->SetBoolField(TEXT("pauseOpen"),Menus.ContainsByPredicate([](auto W){return CastChecked<UPersonaMenu>(W)->bInGame && W->IsInViewport();}));
    TArray<UUserWidget*> Panels;UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this,Panels,UPersonaPanel::StaticClass(),false);
    for(auto W:Panels)
    {
        auto Panel=CastChecked<UPersonaPanel>(W);auto UI=MakeShared<FJsonObject>();
        TArray<TSharedPtr<FJsonValue>> Trials;for(auto Id:Panel->TrialIds) Trials.Add(MakeShared<FJsonValueString>(Id.ToString()));UI->SetArrayField(TEXT("trials"),Trials);
        UI->SetStringField(TEXT("selected"),Panel->SelectedTrial.ToString());UI->SetNumberField(TEXT("icons"),Panel->FindsRow->GetChildrenCount());
        UI->SetNumberField(TEXT("adminItems"),Panel->ItemPaths.Num());UI->SetStringField(TEXT("console"),Panel->ConsoleOutput->GetText().ToString());
        Root->SetObjectField(TEXT("ui"),UI);
    }
    Root->SetStringField(TEXT("viewPosition"),PC->PlayerCameraManager->GetCameraLocation().ToString());
    TArray<TSharedPtr<FJsonValue>> Waypoints;
    for(const FVector& Point:ForestPath) Waypoints.Add(MakeShared<FJsonValueString>(Point.ToString()));
    Root->SetArrayField(TEXT("forestPath"),Waypoints);
    Root->SetBoolField(TEXT("arenaClientReady"),GamePC && GamePC->bArenaReady);
    if(auto D=APersonaTrialDirector::Get(World))
    {
        auto Run=MakeShared<FJsonObject>(); Run->SetStringField(TEXT("id"),D->Run.Id); Run->SetStringField(TEXT("trialId"),D->Run.TrialId.ToString());
        int VisibleShutters=0;for(auto Part:D->Cabin) if(Part->IsVisible()) ++VisibleShutters;
        Run->SetNumberField(TEXT("visibleShutters"),VisibleShutters);Run->SetNumberField(TEXT("doorOpening"),D->DoorOpening);
        Run->SetStringField(TEXT("details"),D->TrialDetails(D->Run.TrialId));
        Run->SetNumberField(TEXT("phase"),int(D->Run.Phase)); Run->SetNumberField(TEXT("lift"),int(D->Run.Lift));
        if(D->Run.TrialId==PersonaForest::TrialId)
        {
            const auto& F=D->Run.Forest;
            Run->SetStringField(TEXT("center"),F.Center.ToString()); Run->SetStringField(TEXT("case"),F.CasePosition.ToString());
            Run->SetStringField(TEXT("carrier"),F.Carrier); Run->SetNumberField(TEXT("elapsed"),F.Elapsed);
            TArray<TSharedPtr<FJsonValue>> People;
            for(const auto& P:F.Players)
            {
                auto V=MakeShared<FJsonObject>(); V->SetStringField(TEXT("id"),P.Id); V->SetBoolField(TEXT("hasMap"),P.HasMap);
                V->SetNumberField(TEXT("outcome"),int(P.Outcome)); V->SetNumberField(TEXT("seconds"),P.Seconds);
                People.Add(MakeShared<FJsonValueObject>(V));
            }
            Run->SetArrayField(TEXT("forestPlayers"),People);
            int32 SurveyCells=0; for(const auto& T:D->NorthAtlas.Tiles) SurveyCells+=FMath::CountBits64(T.Seen);
            Run->SetNumberField(TEXT("surveyCells"),SurveyCells);
            Run->SetNumberField(TEXT("generation"),F.Generation); Run->SetNumberField(TEXT("cooldown"),D->ForestAvailableAt);
            Run->SetNumberField(TEXT("seed"),D->Run.Seed); Run->SetNumberField(TEXT("archivedDrops"),D->NorthDrops.Num());
            TArray<TSharedPtr<FJsonValue>> Sites;
            for(TActorIterator<APersonaForestNode> It(World);It;++It)
            {
                auto Node=MakeShared<FJsonObject>(); Node->SetStringField(TEXT("actor"),It->GetName());Node->SetStringField(TEXT("position"),It->GetActorLocation().ToString());
                Node->SetNumberField(TEXT("kind"),int(It->Kind)); Sites.Add(MakeShared<FJsonValueObject>(Node));
            }
            Run->SetArrayField(TEXT("sites"),Sites);
        }
        Run->SetNumberField(TEXT("wave"),D->Run.Wave); Run->SetNumberField(TEXT("remainingNPC"),D->RemainingNPC); Run->SetBoolField(TEXT("loaded"),D->ArenaReady());
        Run->SetNumberField(TEXT("difficultyPlayers"),D->Run.DifficultyPlayers); Run->SetNumberField(TEXT("pendingNPC"),D->Run.PendingNPC);
        Run->SetNumberField(TEXT("spawnedNPC"),D->Run.SpawnedNPC);
        Run->SetNumberField(TEXT("respect"),D->Run.Respect); Run->SetNumberField(TEXT("penalty"),D->Run.Penalty); Run->SetNumberField(TEXT("bonus"),D->Run.Bonus);
        Run->SetBoolField(TEXT("rewarded"),D->Run.Rewarded); Run->SetBoolField(TEXT("departed"),D->Run.Departed);
        Run->SetNumberField(TEXT("remaining"),FMath::Max(0.f,D->PhaseEnd-(GS ? GS->GetServerWorldTimeSeconds() : World->GetTimeSeconds())));
        Run->SetNumberField(TEXT("progress"),D->Run.LiftProgress); Run->SetNumberField(TEXT("cargo"),D->Run.Cargo.Num()); Run->SetNumberField(TEXT("passengers"),D->Run.Passengers.Num());
        Root->SetObjectField(TEXT("run"),Run);
    }
    auto Saves = GetGameInstance()->GetSubsystem<UPersonaSaveSubsystem>();
    Root->SetStringField(TEXT("sector"),Saves->Active ? Saves->Active->Slot : TEXT(""));
    Root->SetNumberField(TEXT("saveVersion"),Saves->Active ? Saves->Active->Version : 0);
    Root->SetBoolField(TEXT("panelOpen"),GamePC && GamePC->IsPanelOpen());
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
        Item->SetBoolField(TEXT("climbing"),It->Traversal.Active);
        Item->SetStringField(TEXT("climbEnd"),It->Traversal.End.ToString());
        Item->SetStringField(TEXT("climbFailure"),CastChecked<UPersonaMovement>(It->GetCharacterMovement())->TraversalFailure);
        Item->SetNumberField(TEXT("corpseFrameBones"),It->CorpseFrame.Pose.Num());
        Item->SetNumberField(TEXT("corpsePoseBones"),It->CorpsePose.Num());
        Item->SetNumberField(TEXT("displayCorpseBones"),It->DisplayCorpsePose.Num());
        Item->SetBoolField(TEXT("meshAttached"),It->GetMesh()->GetAttachParent()==It->GetRootComponent());
        Item->SetBoolField(TEXT("meshSimulating"),It->GetMesh()->IsAnySimulatingPhysics());
        Item->SetBoolField(TEXT("ragdoll"),It->bRagdollStarted);
        Item->SetStringField(TEXT("pelvis"),It->GetMesh()->GetBoneLocation(TEXT("pelvis")).ToString());
        Item->SetStringField(TEXT("meshPosition"),It->GetMesh()->GetComponentLocation().ToString());
        Item->SetStringField(TEXT("rootPosition"),It->GetMesh()->GetBoneLocation(TEXT("root")).ToString());
        Item->SetStringField(TEXT("velocity"),It->GetVelocity().ToString());
        Item->SetNumberField(TEXT("yaw"),It->GetActorRotation().Yaw);
        Item->SetStringField(TEXT("footR"),It->GetMesh()->GetBoneLocation(TEXT("foot_r")).ToString());
        Item->SetNumberField(TEXT("corpseSequence"),It->CorpseFrame.Sequence);
        Item->SetNumberField(TEXT("corpseBuffer"),It->CorpseFrames.Num());
        Item->SetNumberField(TEXT("bodySpeed"),It->GetMesh()->GetPhysicsLinearVelocity(TEXT("pelvis")).Size());
        if(It->bDead)
        {
            Item->SetStringField(TEXT("deathBone"),It->DeathState.Bone.ToString());
            Item->SetNumberField(TEXT("bodyMass"),It->GetMesh()->GetMass());
            Item->SetNumberField(TEXT("bodyRestTime"),It->CorpseRestTime());
            Item->SetNumberField(TEXT("ragdollStartPoseErrorCm"),It->RagdollStartPoseError);
            Item->SetNumberField(TEXT("ragdollStartAngleErrorDegrees"),It->RagdollStartAngleError);
            float Stretch=0; FName StretchBone; const auto Pose=It->CaptureCorpsePose(); const auto& Ref=It->GetMesh()->GetSkeletalMeshAsset()->GetRefSkeleton();
            for(int32 I=1;I<Pose.Num();++I) if(Ref.GetBoneName(I)!=TEXT("pelvis"))
            {
                const float Error=FVector::Distance(Pose[I].GetLocation(),Ref.GetRefBonePose()[I].GetLocation());
                if(Error>Stretch) { Stretch=Error; StretchBone=Ref.GetBoneName(I); }
            }
            Item->SetNumberField(TEXT("boneStretch"),Stretch);
            Item->SetStringField(TEXT("stretchBone"),StretchBone.ToString());
            Item->SetStringField(TEXT("bodyMeshWorld"),It->GetMesh()->GetComponentTransform().ToString());
            TArray<TSharedPtr<FJsonValue>> Bodies;
            for(const auto& Motion:It->CaptureBodyMotion())
            {
                auto Body=MakeShared<FJsonObject>(); Body->SetStringField(TEXT("bone"),Motion.Bone.ToString());
                Body->SetNumberField(TEXT("speed"),Motion.Linear.Size()); Body->SetNumberField(TEXT("spin"),Motion.Angular.Size());
                Body->SetStringField(TEXT("position"),Motion.Transform.GetLocation().ToString());
                Bodies.Add(MakeShared<FJsonValueObject>(Body));
            }
            Item->SetArrayField(TEXT("bodyMotion"),Bodies);
            FHitResult Floor; FCollisionQueryParams Q(SCENE_QUERY_STAT(CorpseDiagnosticSupport),false,*It);
            const FVector At=It->GetMesh()->GetBoneLocation(TEXT("pelvis"));
            World->LineTraceSingleByChannel(Floor,At,At-FVector(0,0,55),ECC_Visibility,Q);
            Item->SetStringField(TEXT("bodySupport"),GetNameSafe(Floor.GetActor())); Item->SetNumberField(TEXT("bodySupportNormal"),Floor.ImpactNormal.Z);
        }
        Item->SetNumberField(TEXT("shotSound"),It->Weapon->LastSoundVariant);
        Item->SetStringField(TEXT("fireReason"),It->Weapon->LastFireReason);
        Item->SetStringField(TEXT("deferredWeaponAction"),It->Weapon->DeferredAction());
        Item->SetNumberField(TEXT("shotsFired"),It->Weapon->ShotsFired);
        Item->SetNumberField(TEXT("hitVolumeCount"),It->HitVolumes.Num());
        TArray<TSharedPtr<FJsonValue>> Pellets;
        for(const auto& H:It->Weapon->LastPelletHits)
        {
            auto P=MakeShared<FJsonObject>(); P->SetStringField(TEXT("actor"),GetNameSafe(H.GetActor()));
            P->SetStringField(TEXT("bone"),H.BoneName.ToString()); P->SetBoolField(TEXT("blocked"),H.bBlockingHit);
            P->SetStringField(TEXT("end"),(H.bBlockingHit ? H.ImpactPoint : H.TraceEnd).ToString());
            Pellets.Add(MakeShared<FJsonValueObject>(P));
        }
        Item->SetArrayField(TEXT("pellets"),Pellets);
        Item->SetNumberField(TEXT("nearMisses"),It->Weapon->NearMissCount);
        Item->SetNumberField(TEXT("bodyImpacts"),It->Weapon->BodyImpactCount);
        Item->SetNumberField(TEXT("wallImpacts"),It->Weapon->WallImpactCount);
        if(It->IsLocallyControlled())
        {
            const auto Hit=It->Interaction->TraceHit(); auto Hub=Cast<APersonaHubObject>(Hit.GetActor());
            Item->SetStringField(TEXT("interactionActor"),GetNameSafe(Hit.GetActor()));
            Item->SetStringField(TEXT("interactionPart"),Hub ? Hub->HitPart(Hit.GetComponent()).ToString() : TEXT("None"));
        }
        Item->SetNumberField(TEXT("poseBones"),It->CorpsePose.Num()); Item->SetNumberField(TEXT("maxHealth"),It->GetVitals()->GetMaxHealth());
        Item->SetNumberField(TEXT("weaponAction"),int(It->Weapon->Action)); Item->SetNumberField(TEXT("throwAlpha"),It->Interaction->ThrowAlpha());
        Item->SetNumberField(TEXT("corpseLoot"),It->Loot->Items.Num()); Item->SetStringField(TEXT("lootTarget"),GetNameSafe(It->Interaction->LootTarget)); Item->SetStringField(TEXT("lootPart"),It->Interaction->LootPart.ToString());
        TArray<TSharedPtr<FJsonValue>> BodyLoot;
        for(const auto& R:It->Loot->AllItems()) { auto Entry=MakeShared<FJsonObject>(); Entry->SetStringField(TEXT("id"),R.Id); Entry->SetStringField(TEXT("definition"),R.Definition); Entry->SetNumberField(TEXT("quantity"),R.Quantity); BodyLoot.Add(MakeShared<FJsonValueObject>(Entry)); }
        Item->SetArrayField(TEXT("lootItems"),BodyLoot);
        Item->SetStringField(TEXT("persistentId"),It->PersistentId);
        Item->SetNumberField(TEXT("carryAlpha"),It->CarryAlpha);
        Item->SetNumberField(TEXT("team"),State ? State->Team : -1);
        Item->SetBoolField(TEXT("ready"),State && State->bReady);
        Item->SetNumberField(TEXT("health"),It->GetVitals()->GetHealth());
        Item->SetNumberField(TEXT("stamina"),It->GetVitals()->GetStamina());
        Item->SetNumberField(TEXT("hunger"),It->GetVitals()->GetHunger()); Item->SetNumberField(TEXT("water"),It->Water); Item->SetBoolField(TEXT("dry"),It->bDryCollapse); Item->SetBoolField(TEXT("mapOpen"),It->bFieldMapOpen);
        Item->SetNumberField(TEXT("speed"),It->GetVelocity().Size2D());
        Item->SetNumberField(TEXT("maxSpeed"),It->GetCharacterMovement()->GetMaxSpeed());
        Item->SetBoolField(TEXT("sprinting"),It->bSprinting);
        Item->SetNumberField(TEXT("aimAlpha"),It->Weapon->AimAlpha);
        Item->SetNumberField(TEXT("fov"),It->Camera->FieldOfView);
        Item->SetStringField(TEXT("camera"),It->Camera->GetComponentLocation().ToString());
        Item->SetNumberField(TEXT("damageFlash"),It->DamageFlash);
        Item->SetNumberField(TEXT("hitRegion"),int(It->Reaction.Region));
        Item->SetNumberField(TEXT("hitSequence"),It->Reaction.Sequence);
        Item->SetNumberField(TEXT("legScale"),It->LegSpeedScale());
        Item->SetBoolField(TEXT("attacking"),It->AttackState.Playing);
        Item->SetNumberField(TEXT("attackSequence"),It->AttackState.Sequence);
        Item->SetNumberField(TEXT("attackElapsed"),It->ServerTime()-It->AttackState.Start);
        Item->SetNumberField(TEXT("attackWindup"),It->AttackState.Windup);
        Item->SetNumberField(TEXT("attackVariant"),It->AttackState.Variant);
        Item->SetBoolField(TEXT("crouched"),It->bIsCrouched);
        Item->SetBoolField(TEXT("falling"),It->GetCharacterMovement()->IsFalling());
        if (auto AI = Cast<APersonaAIController>(It->GetController()))
        {
            Item->SetStringField(TEXT("target"),GetNameSafe(AI->Target));
            Item->SetBoolField(TEXT("targetVisible"),AI->bTargetVisible);
            Item->SetNumberField(TEXT("tactic"),int(AI->Tactic));
            Item->SetStringField(TEXT("tacticalGoal"),AI->TacticalGoal.ToString());
            Item->SetStringField(TEXT("lastKnown"),AI->InvestigateLocation.ToString());
            Item->SetBoolField(TEXT("investigating"),AI->bInvestigating);
            Item->SetStringField(TEXT("lastSense"),AI->LastSense.ToString());
            Item->SetBoolField(TEXT("brainRunning"),AI->GetBrainComponent() && AI->GetBrainComponent()->IsRunning());
            Item->SetNumberField(TEXT("pathStatus"),int(AI->GetMoveStatus()));
            Item->SetNumberField(TEXT("pathRevision"),AI->PathRevision);
            Item->SetStringField(TEXT("repathReason"),AI->RepathReason);
            if(auto Path=AI->GetPathFollowingComponent()->GetPath(); Path.IsValid()) { Item->SetBoolField(TEXT("partialPath"),Path->IsPartial()); Item->SetStringField(TEXT("pathEnd"),Path->GetEndLocation().ToString()); }
        }
        Item->SetStringField(TEXT("position"),It->GetActorLocation().ToString());
        Item->SetStringField(TEXT("view"),It->GetBaseAimRotation().ToString());
        Item->SetStringField(TEXT("head"),It->GetMesh()->GetBoneLocation(TEXT("head")).ToString());
        Item->SetStringField(TEXT("footL"),It->GetMesh()->GetBoneLocation(TEXT("foot_l")).ToString());
        Item->SetStringField(TEXT("armL"),It->GetMesh()->GetBoneLocation(TEXT("upperarm_l")).ToString());
        Item->SetStringField(TEXT("armR"),It->GetMesh()->GetBoneLocation(TEXT("upperarm_r")).ToString());
        Item->SetStringField(TEXT("legL"),It->GetMesh()->GetBoneLocation(TEXT("thigh_l")).ToString());
        Item->SetStringField(TEXT("legR"),It->GetMesh()->GetBoneLocation(TEXT("thigh_r")).ToString());
        Item->SetStringField(TEXT("mesh"),GetNameSafe(It->GetMesh()->GetSkeletalMeshAsset()));
        Item->SetStringField(TEXT("animation"),GetNameSafe(It->GetMesh()->GetAnimInstance()));
        Item->SetStringField(TEXT("held"),GetNameSafe(It->Interaction->Held));
        FTransform Placement; bool PlacementValid=false;
        Item->SetBoolField(TEXT("burnerTarget"),It->Interaction->FindCookwarePlacement(Placement,PlacementValid));
        Item->SetBoolField(TEXT("placementValid"),PlacementValid);
        Item->SetStringField(TEXT("placementPosition"),Placement.GetLocation().ToString());
        auto Preview=It->Interaction->PlacementPreview;
        Item->SetBoolField(TEXT("placementPreview"),Preview && Preview->IsVisible());
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
        Item->SetStringField(TEXT("containerState"),UPersonaLoot::Capture(*It).ContainerState);
        Item->SetNumberField(TEXT("chamber"),int(It->WeaponState.Chamber)); Item->SetBoolField(TEXT("safety"),It->WeaponState.Safety);
        Item->SetNumberField(TEXT("maxStack"),It->StackLimit());
        Item->SetBoolField(TEXT("stowed"),It->bStowed);
        Item->SetStringField(TEXT("mesh"),GetNameSafe(It->Mesh->GetStaticMesh()));
        Item->SetNumberField(TEXT("physicsSpeed"),It->Mesh->GetPhysicsLinearVelocity().Size());
        Item->SetNumberField(TEXT("sweepCorrections"),It->PhysicsSweepCorrections);
        Item->SetBoolField(TEXT("ccd"),It->Mesh->BodyInstance.bUseCCD);
        Item->SetStringField(TEXT("velocity"),It->Mesh->GetPhysicsLinearVelocity().ToString());
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
    int32 ClimbLinks=0; TArray<TSharedPtr<FJsonValue>> Transitions;
    for(TActorIterator<APersonaClimbLink> It(World);It;++It)
    {
        ++ClimbLinks; auto Link=MakeShared<FJsonObject>();
        Link->SetStringField(TEXT("from"),It->Low.ToString()); Link->SetStringField(TEXT("to"),It->High.ToString());
        Link->SetBoolField(TEXT("available"),It->IsSmartLinkEnabled()); Link->SetStringField(TEXT("user"),GetNameSafe(It->User.Get()));
        Transitions.Add(MakeShared<FJsonValueObject>(Link));
    }
    Root->SetArrayField(TEXT("climbTransitions"),Transitions);
    Root->SetNumberField(TEXT("climbLinks"),ClimbLinks);
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
        H->SetBoolField(TEXT("sharedInventory"),It->Loot->HasSharedInventory());
        TArray<TSharedPtr<FJsonValue>> Parts;
        TArray<UStaticMeshComponent*> Meshes; It->GetComponents(Meshes);
        for(auto Mesh:Meshes) if(Mesh->GetName().StartsWith(TEXT("MovingPart")))
        {
            auto Part=MakeShared<FJsonObject>(); Part->SetStringField(TEXT("mesh"),GetNameSafe(Mesh->GetStaticMesh()));
            Part->SetStringField(TEXT("center"),Mesh->Bounds.Origin.ToString());
            Part->SetStringField(TEXT("location"),Mesh->GetRelativeLocation().ToString());
            Part->SetStringField(TEXT("rotation"),Mesh->GetRelativeRotation().ToString()); Parts.Add(MakeShared<FJsonValueObject>(Part));
        }
        H->SetArrayField(TEXT("parts"),Parts);
        TArray<TSharedPtr<FJsonValue>> Loot;
        for(const auto& R:It->Loot->AllItems()) { auto I=MakeShared<FJsonObject>(); I->SetStringField(TEXT("id"),R.Id); I->SetStringField(TEXT("definition"),R.Definition); I->SetNumberField(TEXT("quantity"),R.Quantity); Loot.Add(MakeShared<FJsonValueObject>(I)); }
        TArray<TSharedPtr<FJsonValue>> Compartments;
        for(const auto& Entry:It->Loot->Compartments)
        {
            auto Part=MakeShared<FJsonObject>(); Part->SetStringField(TEXT("id"),Entry.Id.ToString()); Part->SetStringField(TEXT("label"),Entry.Label); Part->SetBoolField(TEXT("open"),Entry.Open);
            TArray<TSharedPtr<FJsonValue>> Records;
            for(const auto& R:*It->Loot->Contents(Entry.Id)) { auto I=MakeShared<FJsonObject>(); I->SetStringField(TEXT("id"),R.Id); I->SetStringField(TEXT("definition"),R.Definition); I->SetNumberField(TEXT("quantity"),R.Quantity); Records.Add(MakeShared<FJsonValueObject>(I)); }
            Part->SetArrayField(TEXT("items"),Records); Compartments.Add(MakeShared<FJsonValueObject>(Part));
        }
        H->SetArrayField(TEXT("compartments"),Compartments);
        H->SetArrayField(TEXT("items"),Loot); Hub.Add(MakeShared<FJsonValueObject>(H));
    }
    Root->SetArrayField(TEXT("hub"),Hub);
    FString Text;
    FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Text));
    const FString Target=Directory/(Role+TEXT(".json"));
    FFileHelper::SaveStringToFile(Text,*(Target+TEXT(".tmp")));
    IFileManager::Get().Move(*Target,*(Target+TEXT(".tmp")),true,true);
}
