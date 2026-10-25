#include "PersonaSave.h"
#include "PersonaFramework.h"
#include "PersonaCharacter.h"
#include "PersonaInteraction.h"
#include "PersonaAttributes.h"
#include "PersonaAI.h"
#include "PersonaLoot.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Misc/CoreDelegates.h"

namespace
{
FString MapPath(UWorld* World) { return TEXT("/Game/Persona/Maps/") + UGameplayStatics::GetCurrentLevelName(World, true); }
bool PlayMap(const FString& Map) { return Map == TEXT("/Game/Persona/Maps/L_Hub") || Map == TEXT("/Game/Persona/Maps/L_TestChamber"); }
FPersonaSubjectRecord Capture(APersonaCharacter* C)
{
    FPersonaSubjectRecord R;
    if (C->PersistentId.IsEmpty()) C->PersistentId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    R.Id = C->PersistentId; R.NPC = C->bNPC; R.Transform = C->GetActorTransform(); R.Velocity = C->GetVelocity();
    R.Ready = C->SavedReady; R.Name = C->SavedPlayerName;
    R.View = C->GetBaseAimRotation(); R.Crouched = C->bIsCrouched; R.Sprinting = C->bSprinting;
    R.Movement = C->GetCharacterMovement()->MovementMode;
    R.Health = C->GetVitals()->GetHealth(); R.Stamina = C->GetVitals()->GetStamina(); R.Hunger = C->GetVitals()->GetHunger(); R.Team = C->SavedTeam;
    R.MaxHealth=C->GetVitals()->GetMaxHealth(); R.DamageScale=C->DamageScale; R.Dead=C->bDead; R.Spectator=C->bSpectator; R.Transit=C->bTransit; R.Loot=C->Loot->Items;
    if(C->bDead) { R.CorpsePose=C->CorpsePose.IsEmpty() ? C->GetMesh()->GetBoneSpaceTransforms() : C->CorpsePose; R.CorpseMesh=C->GetMesh()->GetComponentTransform(); }
    if (auto P = C->GetPlayerState<APersonaPlayerState>()) { R.Team = P->Team; R.Ready = P->bReady; R.Name = P->GetPlayerName(); }
    R.SelectedSlot = C->Interaction->SelectedSlot;
    for (auto Item : C->Interaction->Slots)
    {
        FPersonaInventoryRecord Slot;
        if (Item) Slot=UPersonaLoot::Capture(Item);
        R.Inventory.Add(Slot);
    }
    if (auto AI = Cast<APersonaAIController>(C->GetController())) AI->CaptureState(R);
    return R;
}
void RestoreInventory(APersonaCharacter* C, const FPersonaSubjectRecord& R)
{
    C->Interaction->SelectedSlot = FMath::Clamp(R.SelectedSlot,0,2);
    for (int32 I=0; I<FMath::Min(3,R.Inventory.Num()); ++I)
    {
        const auto& Slot=R.Inventory[I]; if (Slot.ActorClass.IsEmpty()) continue;
        UClass* Class=LoadClass<APersonaProp>(nullptr,*Slot.ActorClass); if (!Class) continue;
        auto Item=C->GetWorld()->SpawnActorDeferred<APersonaProp>(Class,C->GetActorTransform(),C,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!Item) continue;
        Item->Definition=Slot.Definition.IsEmpty() ? nullptr : LoadObject<UPersonaItemDefinition>(nullptr,*Slot.Definition);
        Item->Quantity=FMath::Clamp(Slot.Quantity,1,Item->StackLimit());
        Item->WeaponState=Slot.Weapon; if(!Slot.Id.IsEmpty()) Item->PersistentItemId=Slot.Id;
        Item->Holder=C; Item->bStowed=I!=C->Interaction->SelectedSlot;
        Item->FinishSpawning(C->GetActorTransform()); C->Interaction->Slots[I]=Item;
    }
    C->Interaction->RefreshEquipped();
}

void Apply(APersonaCharacter* C, const FPersonaSubjectRecord& R)
{
    C->PersistentId = R.Id; C->SavedTeam = R.Team; C->SetMaxHealth(R.MaxHealth); C->SetVitals(R.Health,R.Stamina,R.Hunger);
    C->DamageScale=R.DamageScale; C->bDead=R.Dead; C->bSpectator=R.Spectator; C->bTransit=R.Transit; C->Loot->Items=R.Loot;
    C->OfflineView = R.View;
    C->SavedReady = R.Ready; C->SavedPlayerName = R.Name;
    C->bSprinting = R.Sprinting; C->OnRep_Sprinting();
    C->GetCharacterMovement()->SetMovementMode(static_cast<EMovementMode>(R.Movement));
    C->GetCharacterMovement()->bWantsToCrouch = R.Crouched;
    if (R.Crouched) C->GetCharacterMovement()->Crouch(false); else C->GetCharacterMovement()->UnCrouch(false);
    C->SetActorTransform(R.Transform, false, nullptr, ETeleportType::TeleportPhysics);
    C->GetCharacterMovement()->Velocity = R.Velocity;
    if (C->Controller) { C->Controller->SetControlRotation(R.View); if (auto PC = Cast<APlayerController>(C->Controller)) PC->ClientSetRotation(R.View,true); }
    if (auto P = C->GetPlayerState<APersonaPlayerState>()) { P->Team = R.Team; P->bReady = R.Ready; }
    if(R.Dead) { C->CorpsePose=R.CorpsePose; C->CorpseMeshTransform=R.CorpseMesh; C->OnRep_Dead(); C->OnRep_Corpse(); }
    if(R.Transit || R.Spectator) C->OnRep_Transit();
}
}
void UPersonaSaveSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    TearDownHandle = FWorldDelegates::OnWorldBeginTearDown.AddUObject(this, &ThisClass::WorldTearingDown);
}
void UPersonaSaveSubsystem::WorldTearingDown(UWorld* World)
{
    // Runs before controllers, held items and GAS owners are destroyed (also Alt+F4 / PIE stop).
    if (World == GetWorld()) { SaveWorld(); bTravel = true; }
}
void UPersonaSaveSubsystem::Deinitialize() { FWorldDelegates::OnWorldBeginTearDown.Remove(TearDownHandle); Super::Deinitialize(); }
FString UPersonaSaveSubsystem::Directory() const
{
    FString Diagnostic;
    if (FParse::Value(FCommandLine::Get(),TEXT("PersonaDiagnostics="),Diagnostic))
        return FPaths::ProjectSavedDir() / TEXT("SaveGames/Validation/Sectors");
    return FPaths::ProjectSavedDir() / TEXT("SaveGames/Sectors");
}
FString UPersonaSaveSubsystem::LocalIdentity()
{
    if (!ProfileId.IsEmpty()) return ProfileId;
    FString Profile;
    if (!FParse::Value(FCommandLine::Get(), TEXT("PersonaProfile="), Profile)) FParse::Value(FCommandLine::Get(), TEXT("PersonaDiagnostics="), Profile);
    Profile = FPaths::MakeValidFileName(Profile.Left(32));
    const FString Path = FPaths::ProjectSavedDir() / (TEXT("PersonaProfile") + Profile + TEXT(".txt"));
    FGuid Guid;
    if (!FFileHelper::LoadFileToString(ProfileId, *Path) || !FGuid::ParseExact(ProfileId, EGuidFormats::Digits, Guid))
    {
        ProfileId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        IFileManager::Get().MakeDirectory(*FPaths::ProjectSavedDir(), true);
        FFileHelper::SaveStringToFile(ProfileId, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }
    return ProfileId;
}
FString UPersonaLocalPlayer::GetGameLoginOptions() const
{
    return Super::GetGameLoginOptions() + TEXT("?PersonaId=") + GetGameInstance()->GetSubsystem<UPersonaSaveSubsystem>()->LocalIdentity();
}
UPersonaSectorSave* UPersonaSaveSubsystem::Read(const FString& Path)
{
    TArray<uint8> Bytes;
    if (!FFileHelper::LoadFileToArray(Bytes, *Path) || Bytes.Num() > 32*1024*1024) return nullptr;
    auto Save = Cast<UPersonaSectorSave>(UGameplayStatics::LoadGameFromMemory(Bytes));
    FGuid Guid;
    return Save && Save->Version>=1 && Save->Version<=3 && PlayMap(Save->CurrentMap) && FGuid::ParseExact(Save->Slot, EGuidFormats::Digits, Guid) ? Save : nullptr;
}
TArray<UPersonaSectorSave*> UPersonaSaveSubsystem::ListSectors()
{
    Listed.Reset(); TArray<FString> Files; TArray<UPersonaSectorSave*> Result;
    IFileManager::Get().FindFiles(Files, *(Directory()/TEXT("*.sav")), true, false);
    for (const FString& File : Files)
    {
        auto S = Read(Directory()/File); if (!S) S = Read((Directory()/File)+TEXT(".bak"));
        if (S) { Listed.Add(S); Result.Add(S); }
    }
    Result.Sort([](const UPersonaSectorSave& A, const UPersonaSectorSave& B) { return A.Updated > B.Updated; });
    return Result;
}
bool UPersonaSaveSubsystem::Write()
{
    TArray<uint8> Bytes;
    if (!Active || !UGameplayStatics::SaveGameToMemory(Active, Bytes)) return false;
    IFileManager::Get().MakeDirectory(*Directory(), true);
    const FString Path = Directory()/(Active->Slot + TEXT(".sav")), Temp = Path + TEXT(".tmp"), Backup = Path + TEXT(".bak");
    if (!FFileHelper::SaveArrayToFile(Bytes,*Temp) || !Read(Temp)) { LastError = TEXT("Не удалось записать сектор. Проверьте свободное место на диске."); return false; }
    if (IFileManager::Get().FileExists(*Path) && Read(Path) && IFileManager::Get().Copy(*Backup,*Path,true,true) != COPY_OK)
    { LastError = TEXT("Не удалось создать резервную копию сектора."); return false; }
    if (!IFileManager::Get().Move(*Path,*Temp,true,true)) { LastError = TEXT("Не удалось заменить файл сектора."); return false; }
    LastError.Reset(); return true;
}
bool UPersonaSaveSubsystem::NewSector(bool Teams)
{
    Active = NewObject<UPersonaSectorSave>(this); Active->Slot = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    Active->Updated = FDateTime::UtcNow(); Active->Teams = Teams;
    Active->Title = TEXT("Сектор ") + FDateTime::Now().ToString(TEXT("%d.%m.%Y / %H:%M:%S"));
    bTravel = false; return Write();
}
bool UPersonaSaveSubsystem::SelectSector(const FString& Slot)
{
    FGuid Guid; if (!FGuid::ParseExact(Slot, EGuidFormats::Digits, Guid)) return false;
    const FString Path = Directory()/(Slot+TEXT(".sav"));
    auto S = Read(Path); if (!S) S = Read(Path+TEXT(".bak"));
    if (!S) { LastError = TEXT("Сектор повреждён или создан несовместимой версией."); return false; }
    if(S->Version<3)
    {
        const FString Backup=Path+TEXT(".pre04");
        if(!IFileManager::Get().FileExists(*Backup) && IFileManager::Get().Copy(*Backup,*Path)!=COPY_OK)
        { LastError=TEXT("Не удалось создать резервную копию старого сектора. Миграция отменена."); return false; }
        FPersonaMapRecord Hub; Hub.Map=TEXT("/Game/Persona/Maps/L_Hub"); Hub.InventoryFormat=3;
        FPersonaHubRecord Storage; Storage.Id=TEXT("MigrationStorage"); Storage.Definition=TEXT("/Game/Persona/Hub/DA_HUB_Chest_01.DA_HUB_Chest_01"); Storage.Transform=FTransform(FVector(500,1100,1)); Storage.Capacity=1024;
        for(const auto& M:S->Maps) for(const auto& P:M.Props)
        {
            FPersonaInventoryRecord R; R.ActorClass=P.ActorClass.IsEmpty() ? APersonaProp::StaticClass()->GetPathName() : P.ActorClass;
            R.Definition=P.Definition; R.Id=P.Id; R.Quantity=P.Quantity; R.Weapon=P.Weapon;
            if(P.Holder.IsEmpty()) Storage.Items.Add(R);
            else if(M.InventoryFormat<2)
            {
                auto Owner=S->Players.FindByPredicate([&](const auto& Player){return Player.Id==P.Holder;});
                if(Owner && Owner->Inventory.Num()<3) Owner->Inventory.Add(R); else Storage.Items.Add(R);
            }
        }
        int32 Index=0; for(auto& P:S->Players) { P.Transform=FTransform(FRotator(0,-90,0),FVector(-180+Index++*120,1100,100)); P.View=FRotator(0,-90,0); P.Velocity=FVector::ZeroVector; P.Health=100; P.Movement=1; P.Crouched=false; Hub.Subjects.Add(P); }
        Hub.Hub.Add(Storage); S->Maps={Hub}; S->CurrentMap=Hub.Map; S->Version=3;
    }
    Active = S; bTravel = false; return true;
}
bool UPersonaSaveSubsystem::SaveWorld()
{
    UWorld* W = GetWorld(); auto GM = W ? W->GetAuthGameMode<APersonaGameMode>() : nullptr;
    if (!GM || !PlayMap(MapPath(W)) || bTravel) return true;
    if (!Active && !NewSector()) return false;
    Active->Version = 3;
    FPersonaMapRecord Snapshot; Snapshot.Map = MapPath(W); Snapshot.InventoryFormat = 3;
    if(auto D=APersonaTrialDirector::Get(W)) D->Capture(Active->Run);
    for (TActorIterator<APersonaCharacter> It(W); It; ++It)
    {
        auto R = Capture(*It); Snapshot.Subjects.Add(R);
        if (!R.NPC)
        {
            Active->Players.RemoveAll([&](const FPersonaSubjectRecord& P) { return P.Id == R.Id; }); Active->Players.Add(R);
        }
    }
    for (TActorIterator<APersonaProp> It(W); It; ++It)
    {
        if (It->Holder) continue; // Inventory is stored once, in its subject record.
        FPersonaObjectRecord R; R.Id = It->PersistentItemId; R.Transform = It->GetActorTransform();
        R.ActorClass=It->GetClass()->GetPathName(); R.Definition=It->Definition ? It->Definition->GetPathName() : FString(); R.Quantity=It->Quantity;
        R.Velocity = It->Mesh->GetPhysicsLinearVelocity(); R.AngularVelocity = It->Mesh->GetPhysicsAngularVelocityInDegrees();
        R.Sleeping = !It->Mesh->IsAnyRigidBodyAwake(); R.Holder = It->Holder ? It->Holder->PersistentId : FString(); Snapshot.Props.Add(R);
        Snapshot.Props.Last().Weapon=It->WeaponState;
    }
    for (TActorIterator<APersonaDoor> It(W); It; ++It) { FPersonaObjectRecord R; R.Id = It->GetName(); R.Open = It->bOpen; R.Transform = It->GetActorTransform(); Snapshot.Doors.Add(R); }
    for(TActorIterator<APersonaHubObject> It(W); It; ++It) { if(It->bTrialObject && Active->Run.Phase==EPersonaTrialPhase::Offered) continue; FPersonaHubRecord R; R.Id=It->PersistentId; R.Definition=It->Definition ? It->Definition->GetPathName() : FString(); R.Transform=It->GetActorTransform(); R.Open=It->bOpen; R.Powered=It->bPowered; R.Items=It->Loot->Items; R.Capacity=It->Loot->Capacity; Snapshot.Hub.Add(R); }
    Active->Maps.RemoveAll([&](const FPersonaMapRecord& M) { return M.Map == Snapshot.Map; }); Active->Maps.Add(Snapshot);
    Active->CurrentMap = Snapshot.Map; Active->Updated = FDateTime::UtcNow();
    Active->Teams = GM->GetGameState<APersonaGameState>()->Mode == EPersonaMode::Teams; Active->FriendlyFire = GM->bFriendlyFire;
    AutosaveElapsed = 0; const bool OK = Write();
    UE_LOG(LogTemp, Display, TEXT("PERSONA SAVE: %s success=%d subjects=%d props=%d"), *Active->Slot, OK, Snapshot.Subjects.Num(),Snapshot.Props.Num());
    return OK;
}
void UPersonaSaveSubsystem::PrepareTravel() { bTravel = true; }
bool UPersonaSaveSubsystem::RestoreWorld(APersonaGameMode* GM)
{
    bTravel = false; AutosaveElapsed = 0;
    if (!Active) return false;
    auto W = GM->GetWorld(); GM->bFriendlyFire = Active->FriendlyFire;
    // Restore collision before waking saved bodies, props and NPCs below the hub.
    if(auto D=APersonaTrialDirector::Get(W); D && Active->Run.Phase!=EPersonaTrialPhase::Offered)
    { D->Restore(Active->Run); W->FlushLevelStreaming(EFlushLevelStreamingType::Full); }
    GM->GetGameState<APersonaGameState>()->Mode = Active->Teams ? EPersonaMode::Teams : EPersonaMode::Cooperative;
    const auto M = Active->Maps.FindByPredicate([&](const FPersonaMapRecord& R) { return R.Map == MapPath(W); });
    if (!M) return false;
    TMap<FString,APersonaCharacter*> Subjects;
    for (const auto& R : M->Subjects)
    {
        FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto C = W->SpawnActor<APersonaCharacter>(R.NPC ? GM->NPCClass.Get() : GM->DefaultPawnClass.Get(),R.Transform,Params);
        if (!C) continue;
        FPersonaSubjectRecord Latest = R;
        if (!R.NPC)
            if (const auto P = Active->Players.FindByPredicate([&](const FPersonaSubjectRecord& S) { return S.Id == R.Id; }))
            { Latest.Health = P->Health; Latest.Stamina = P->Stamina; Latest.Hunger = P->Hunger; Latest.Team = P->Team; Latest.Ready = P->Ready; Latest.Inventory=P->Inventory; Latest.SelectedSlot=P->SelectedSlot; }
        Apply(C,Latest); RestoreInventory(C,Latest); C->bOffline = !R.NPC;
        if (!R.NPC) { C->GetCharacterMovement()->StopMovementImmediately(); C->bRestoredBody = true; C->ResumeVelocity = R.Velocity; }
        Subjects.Add(R.Id,C);
    }
    for (const auto& R : M->Subjects)
        if (auto C = Subjects.FindRef(R.Id); C && R.NPC)
            if (auto AI = Cast<APersonaAIController>(C->GetController())) AI->RestoreState(R,Subjects.FindRef(R.Target));
    if (M->InventoryFormat >= 2)
    {
        // The saved world set is authoritative, including consumed pickups and
        // dynamic drops. Recreating only defaults would duplicate picked items.
        TArray<APersonaProp*> Defaults;
        for (TActorIterator<APersonaProp> It(W); It; ++It) if (!It->Holder) Defaults.Add(*It);
        for (auto Item : Defaults) Item->Destroy();
        for (const auto& R : M->Props)
        {
            UClass* Class=LoadClass<APersonaProp>(nullptr,*R.ActorClass); if (!Class) continue;
            auto Item=W->SpawnActorDeferred<APersonaProp>(Class,R.Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            if (!Item) continue;
            Item->Definition=R.Definition.IsEmpty() ? nullptr : LoadObject<UPersonaItemDefinition>(nullptr,*R.Definition);
            Item->Quantity=FMath::Clamp(R.Quantity,1,Item->StackLimit()); Item->PersistentItemId=R.Id;
            Item->WeaponState=R.Weapon;
            Item->FinishSpawning(R.Transform);
            Item->Mesh->SetPhysicsLinearVelocity(R.Velocity); Item->Mesh->SetPhysicsAngularVelocityInDegrees(R.AngularVelocity);
            if (R.Sleeping) Item->Mesh->PutAllRigidBodiesToSleep();
        }
    }
    else
    {
        // Legacy sectors had a single held cube and no inventory.
        for (TActorIterator<APersonaProp> It(W); It; ++It)
            if (const auto R=M->Props.FindByPredicate([&](const FPersonaObjectRecord& P) { return P.Id==It->GetName(); }))
            {
                It->Mesh->SetSimulatePhysics(false); It->SetActorTransform(R->Transform,false,nullptr,ETeleportType::TeleportPhysics);
                auto C=Subjects.FindRef(R->Holder);
                if (C) C->Interaction->Store(*It);
                else { It->OnRep_Holder(); It->Mesh->SetPhysicsLinearVelocity(R->Velocity); It->Mesh->SetPhysicsAngularVelocityInDegrees(R->AngularVelocity); if (R->Sleeping) It->Mesh->PutAllRigidBodiesToSleep(); }
            }
    }
    for (TActorIterator<APersonaDoor> It(W); It; ++It)
        if (const auto R = M->Doors.FindByPredicate([&](const FPersonaObjectRecord& D) { return D.Id == It->GetName(); }))
        { It->SetActorTransform(R->Transform); It->bOpen = R->Open; It->OnRep_Open(); }
    GM->GetGameState<APersonaGameState>()->NPCCount = M->Subjects.FilterByPredicate([](const FPersonaSubjectRecord& R) { return R.NPC; }).Num();
    RestoreHubObjects();
    return true;
}
void UPersonaSaveSubsystem::RestoreHubObjects()
{
    if(!Active) return;
    auto M=Active->Maps.FindByPredicate([&](const auto& R){return R.Map==MapPath(GetWorld());}); if(!M) return;
    for(const auto& R:M->Hub)
    {
        APersonaHubObject* Object=nullptr; for(TActorIterator<APersonaHubObject> It(GetWorld());It;++It) if(It->PersistentId==R.Id) { Object=*It; break; }
        if(!Object && R.Id==TEXT("MigrationStorage")) { Object=GetWorld()->SpawnActor<APersonaHubObject>(APersonaHubObject::StaticClass(),R.Transform); Object->PersistentId=R.Id; Object->Configure(LoadObject<UPersonaHubAsset>(nullptr,*R.Definition)); }
        if(Object) { Object->SetActorTransform(R.Transform); Object->bOpen=R.Open; Object->bPowered=R.Powered; Object->Loot->Items=R.Items; Object->Loot->Capacity=R.Capacity; }
    }
}
bool UPersonaSaveSubsystem::Rejoin(APlayerController* PC)
{
    auto PS = PC->GetPlayerState<APersonaPlayerState>(); if (!PS || PS->PersistentId.IsEmpty()) return false;
    for (TActorIterator<APersonaCharacter> It(PC->GetWorld()); It; ++It)
        if (!It->bNPC && It->PersistentId == PS->PersistentId && !It->GetController())
        {
            // Capture the live abandoned body: damage incurred while absent wins over disk.
            auto R = Capture(*It);
            if (It->bRestoredBody) R.Velocity = It->ResumeVelocity;
            It->bRestoredBody = false;
            It->bOffline = false; PC->Possess(*It); Apply(*It,R);
            // ClientRestart clears crouch intent and velocity. Restore the owner's
            // input state after possession so its next move does not undo the save.
            It->ClientResumeMovement(R.Crouched, R.Velocity);
            It->ForceNetUpdate(); return true;
        }
    return false;
}
void UPersonaSaveSubsystem::Tick(float Delta)
{
    if (!GetWorld() || !GetWorld()->IsGameWorld() || !GetWorld()->GetAuthGameMode<APersonaGameMode>() || bTravel) return;
    AutosaveElapsed += Delta;
    if (AutosaveElapsed >= 30) { AutosaveElapsed = 0; if (!SaveWorld()) if (auto GI = Cast<UPersonaGameInstance>(GetGameInstance())) GI->Status = LastError; }
}

void UPersonaSaveSubsystem::RestorePlayerInventory(APersonaCharacter* C)
{
    if (Active && C)
        if (const auto R=Active->Players.FindByPredicate([&](const FPersonaSubjectRecord& S) { return S.Id==C->PersistentId; })) RestoreInventory(C,*R);
}
