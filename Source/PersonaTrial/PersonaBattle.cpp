#include "PersonaBattle.h"
#include "PersonaBattleArena.h"
#include "PersonaFramework.h"
#include "PersonaOnline.h"
#include "PersonaSave.h"
#include "PersonaCharacter.h"
#include "PersonaAttributes.h"
#include "PersonaInteraction.h"
#include "PersonaLoot.h"
#include "PersonaHub.h"
#include "PersonaCatalog.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "JsonObjectConverter.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/SecureHash.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "OnlineSubsystem.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Net/UnrealNetwork.h"

namespace
{
template<class T> TSharedPtr<FJsonObject> Object(const T& Value) { return FJsonObjectConverter::UStructToJsonObject(Value); }
template<class T> bool Read(const TSharedPtr<FJsonObject>& Value,T& Out) { return Value && FJsonObjectConverter::JsonObjectToUStruct(Value.ToSharedRef(),&Out); }
FString Digest(const FString& Value) { FTCHARToUTF8 Bytes(*Value);return FMD5::HashBytes(reinterpret_cast<const uint8*>(Bytes.Get()),Bytes.Length()); }
TSharedPtr<FJsonValue> CanonicalJson(const TSharedPtr<FJsonValue>& V)
{
    if(V->Type==EJson::Object)
    {
        auto Source=V->AsObject();auto Out=MakeShared<FJsonObject>();TArray<FString> Keys;
        for(const auto& Pair:Source->Values) Keys.Add(FString(Pair.Key.ToView()));
        Keys.Sort([](const auto& A,const auto& B){return A.ToLower()<B.ToLower();});
        for(const auto& Key:Keys) Out->SetField(Key.ToLower(),CanonicalJson(Source->TryGetField(Key)));
        return MakeShared<FJsonValueObject>(Out);
    }
    if(V->Type==EJson::Array)
    { TArray<TSharedPtr<FJsonValue>> Values;for(const auto& Item:V->AsArray()) Values.Add(CanonicalJson(Item));return MakeShared<FJsonValueArray>(Values); }
    return V;
}
template<class T> FString Hash(const T& Value)
{
    // Cooked FNames can spell the same property "id" or "iD". Canonical keys
    // and ordering keep the protocol hash independent of that display casing.
    FString S;auto V=CanonicalJson(MakeShared<FJsonValueObject>(Object(Value)));
    FJsonSerializer::Serialize(V->AsObject().ToSharedRef(),TJsonWriterFactory<>::Create(&S));return Digest(S);
}
bool Guid(const FString& S) { FGuid G;return FGuid::ParseExact(S,EGuidFormats::Digits,G); }
void GlobalItemId(FString& Id,const FString& Sector)
{
    FGuid Parsed;
    // Old level-placed tools used actor names, repeated in every new hub.
    // Keep that identity within a persistent sector namespace; UUIDs and
    // already exported items retain their existing globally unique identity.
    if(!FGuid::Parse(Id,Parsed) && !Id.StartsWith(TEXT("sector:"))) Id=TEXT("sector:")+Sector+TEXT(":")+Id;
}
void GlobalRecordIds(FPersonaInventoryRecord& R,const FString& Sector,int32 Depth=0)
{
    if(R.ActorClass.IsEmpty() || Depth>4) return;GlobalItemId(R.Id,Sector);
    if(R.ContainerState.IsEmpty()) return;
    FPersonaContainerState S;if(!FJsonObjectConverter::JsonObjectStringToUStruct(R.ContainerState,&S)) return;
    for(auto& I:S.Items) GlobalRecordIds(I,Sector,Depth+1);
    for(auto& P:S.Compartments) for(auto& I:P.Items) GlobalRecordIds(I,Sector,Depth+1);
    FJsonObjectConverter::UStructToJsonObjectString(S,R.ContainerState);
}
void GlobalPropIds(APersonaProp* P,const FString& Sector)
{
    GlobalItemId(P->PersistentItemId,Sector);
    if(auto L=P->FindComponentByClass<UPersonaLoot>())
    {
        for(auto& I:L->Items) GlobalRecordIds(I,Sector);
        for(auto& Part:L->Compartments) for(auto& I:Part.Items) GlobalRecordIds(I,Sector);
    }
    P->ForceNetUpdate();
}
APersonaHubObject* Lift(UWorld* W)
{ for(TActorIterator<APersonaHubObject> It(W);It;++It) if(It->Definition && It->Definition->Kind==EPersonaHubKind::Lift) return *It;return nullptr; }
bool OnBoard(APersonaCharacter* C,APersonaHubObject* L)
{ if(!C || !L) return false;const auto P=L->GetActorTransform().InverseTransformPosition(C->GetActorLocation());return FMath::Abs(P.X)<260 && FMath::Abs(P.Y)<260 && P.Z>0 && P.Z<220; }
TArray<APersonaPlayerController*> Controllers(UWorld* W)
{ TArray<APersonaPlayerController*> Out;for(FConstPlayerControllerIterator It=W->GetPlayerControllerIterator();It;++It) if(auto P=Cast<APersonaPlayerController>(It->Get())) Out.Add(P);return Out; }
APersonaPlayerController* Host(UWorld* W) { auto P=Cast<APersonaPlayerController>(W->GetFirstPlayerController());return P && P->IsHost() ? P : nullptr; }
FString TextField(const TSharedPtr<FJsonObject>& O,const TCHAR* Key) { FString S;if(O) O->TryGetStringField(Key,S);return S; }
struct FInventoryBudget
{
    TMap<FString,int64> Counts;
    TSet<FString> Ids,NonstackIds;
    int32 Entries=0;
    bool Add(const FPersonaInventoryRecord& I,int32 Depth=0,const TSet<FString>* Known=nullptr)
    {
        if(I.ActorClass.IsEmpty()) return I.Definition.IsEmpty();
        if(Depth>4 || ++Entries>8192 || I.Id.IsEmpty() || Ids.Contains(I.Id)) return false;
        auto Catalog=UPersonaItemCatalog::Get();auto D=Catalog ? Catalog->Find(I.Definition) : nullptr;
        if((!D && !I.Definition.IsEmpty()) || I.Quantity<1 || I.Quantity>(D ? D->MaxStack : 1) || !LoadClass<APersonaProp>(nullptr,*I.ActorClass)) return false;
        Ids.Add(I.Id);Counts.FindOrAdd(I.Definition.IsEmpty() ? I.ActorClass : I.Definition)+=I.Quantity;
        if(!D || D->MaxStack==1)
        { if(Known && !Known->Contains(I.Id)) return false;NonstackIds.Add(I.Id); }
        if(D && D->bShotgun)
        {
            if(I.Weapon.Tube<0 || I.Weapon.Tube>6) return false;
            if(auto Ammo=D->Ammunition.LoadSynchronous()) Counts.FindOrAdd(Ammo->GetPathName())+=I.Weapon.Tube+(I.Weapon.Chamber==EPersonaChamber::Live ? 1 : 0);
        }
        if(!I.ContainerState.IsEmpty())
        {
            FPersonaContainerState S;if(I.Quantity!=1 || I.ContainerState.Len()>256*1024 || !FJsonObjectConverter::JsonObjectStringToUStruct(I.ContainerState,&S) || S.Capacity<1 || S.Capacity>4096 || S.Compartments.Num()>256) return false;
            for(const auto& Item:S.Items) if(!Add(Item,Depth+1,Known)) return false;
            for(const auto& Part:S.Compartments) for(const auto& Item:Part.Items) if(!Add(Item,Depth+1,Known)) return false;
        }
        return true;
    }
};
bool Conserves(const FPersonaBattleTicket& T,const FPersonaBattleSettlement& R)
{
    FInventoryBudget Before,After;
    for(const auto& P:T.Parties)
    { for(const auto& S:P.Subjects) for(const auto& I:S.Inventory) if(!Before.Add(I)) return false;for(const auto& C:P.Cargo) if(!Before.Add(C.Item)) return false; }
    TSet<FString> Subjects;
    for(const auto& S:R.Survivors)
    { if(Subjects.Contains(S.Id)) return false;Subjects.Add(S.Id);for(const auto& I:S.Inventory) if(!After.Add(I,0,&Before.NonstackIds)) return false; }
    for(const auto* Cargo:{&R.Cargo,&R.DrawCargo}) for(const auto& C:*Cargo) if(!After.Add(C.Item,0,&Before.NonstackIds)) return false;
    for(const auto& Pair:After.Counts) if(Pair.Value>Before.Counts.FindRef(Pair.Key)) return false;
    return true;
}
}
APersonaBattleState::APersonaBattleState() { bReplicates=true;bAlwaysRelevant=true;SetNetUpdateFrequency(10); }
APersonaBattleState* APersonaBattleState::Get(UWorld* W) { if(W) for(TActorIterator<APersonaBattleState> It(W);It;++It) return *It;return nullptr; }
void APersonaBattleState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(APersonaBattleState,Phase);DOREPLIFETIME(APersonaBattleState,Status);
    DOREPLIFETIME(APersonaBattleState,Opponent);DOREPLIFETIME(APersonaBattleState,Open);DOREPLIFETIME(APersonaBattleState,Ready);
    DOREPLIFETIME(APersonaBattleState,GroupSize);DOREPLIFETIME(APersonaBattleState,Winner);
}
void UPersonaBattleSubsystem::Initialize(FSubsystemCollectionBase& C) { Super::Initialize(C); }
void UPersonaBattleSubsystem::Deinitialize() { Transport.Reset();Super::Deinitialize(); }
bool UPersonaBattleSubsystem::IsArena() const { return GetWorld() && GetWorld()->GetMapName().Contains(TEXT("L_SectorBattle")); }
bool UPersonaBattleSubsystem::Locked() const { return IsArena() || (Phase!=EPersonaBattlePhase::Idle && Phase!=EPersonaBattlePhase::Offered); }
bool UPersonaBattleSubsystem::ClosingCabin() const
{ auto S=APersonaBattleState::Get(GetWorld());return S && !IsArena() && (S->Phase==EPersonaBattlePhase::Prepared || S->Phase==EPersonaBattlePhase::Travelling); }
FString UPersonaBattleSubsystem::Owner() const
{
    if(auto OSS=IOnlineSubsystem::Get(); OSS && OSS->GetSubsystemName()==FName(TEXT("STEAM")))
        if(auto I=OSS->GetIdentityInterface();I.IsValid()) if(auto Id=I->GetUniquePlayerId(0);Id.IsValid()) return Id->ToString();
    return GetGameInstance()->GetSubsystem<UPersonaSaveSubsystem>()->LocalIdentity();
}
bool UPersonaBattleSubsystem::StartTransport()
{
    if(Transport) return true;
    int32 Port=7777;FParse::Value(FCommandLine::Get(),TEXT("port="),Port);Port+=100;
    FParse::Value(FCommandLine::Get(),TEXT("PersonaBattlePort="),Port);
    auto T=MakeUnique<FPersonaBattleTransport>();
    if(!T->Start(GetGameInstance()->GetSubsystem<UPersonaOnline>()->IsSteam(),Port)) return false;
    Transport=MoveTemp(T);Transport->Allow(FString(),true);return true;
}
FString UPersonaBattleSubsystem::ControlEndpoint() const { return Transport ? Transport->Endpoint(Owner()) : FString(); }
void UPersonaBattleSubsystem::HubReady()
{
    if(GetWorld()->GetNetMode()==NM_Client || IsArena()) return;
    if(!APersonaBattleState::Get(GetWorld())) GetWorld()->SpawnActor<APersonaBattleState>();
    StartTransport();Phase=EPersonaBattlePhase::Idle;bHostIssued=false;ReturningHost=false;ReturningOwner.Reset();ReturningSector.Reset();
    Journal=nullptr;Ticket=FPersonaBattleTicket();TicketAcks.Reset();bOwnPrepared=bPeerManifest=bOwnTicketReady=bPeerTicketReady=bLaunching=false;
    UpdateState();
}
void UPersonaBattleSubsystem::UpdateState(const FString& Status)
{
    if(auto S=APersonaBattleState::Get(GetWorld());S && S->HasAuthority())
    {
        S->Phase=Phase;if(!Status.IsEmpty()) S->Status=Status;
        S->GroupSize=Controllers(GetWorld()).Num();S->Ready=0;
        for(auto P:Controllers(GetWorld())) if(auto PS=P->GetPlayerState<APersonaPlayerState>();PS && PS->bBattleReady) ++S->Ready;
        if(Ticket.Parties.Num()==2) S->Opponent=Ticket.Parties[Ticket.Master==Owner() ? 1 : 0].Title;
        if(Phase!=EPersonaBattlePhase::Idle) S->Open=false;
        S->ForceNetUpdate();
    }
    if(!Status.IsEmpty()) CastChecked<UPersonaGameInstance>(GetGameInstance())->Status=Status;
}
void UPersonaBattleSubsystem::SetPhase(EPersonaBattlePhase Value,const FString& Status) { Phase=Value;UpdateState(Status); }
bool UPersonaBattleSubsystem::CanPrepare(FString& Error) const
{
    auto W=GetWorld();auto S=GetGameInstance()->GetSubsystem<UPersonaSaveSubsystem>();auto D=APersonaTrialDirector::Get(W);
    if(!Host(W) || !S->Active || !D || D->Running() || !Lift(W)) { Error=TEXT("Сначала завершите текущее испытание.");return false; }
    const auto Group=Controllers(W);if(Group.IsEmpty() || Group.Num()>4) { Error=TEXT("В секторе должно быть от одного до четырёх участников.");return false; }
    for(auto PC:Group) if(auto C=Cast<APersonaCharacter>(PC->GetPawn());!C || !C->IsAlive() || C->bOffline || C->bSpectator)
    { Error=TEXT("Все участники должны быть живы и находиться в хабе.");return false; }
    return true;
}
FPersonaBattleParty UPersonaBattleSubsystem::BuildParty(bool Cargo) const
{
    FPersonaBattleParty P;auto S=GetGameInstance()->GetSubsystem<UPersonaSaveSubsystem>();P.Owner=Owner();
    if(S->Active) { P.Sector=S->Active->Slot;P.Title=S->Active->Title; }
    for(auto PC:Controllers(GetWorld())) if(auto C=Cast<APersonaCharacter>(PC->GetPawn()))
    { auto R=UPersonaSaveSubsystem::CaptureSubject(C);R.Name=PC->PlayerState->GetPlayerName();R.CorpsePose.Reset();R.BodyMotion.Reset();if(!Cargo) { R.Inventory.Reset();R.Loot.Reset(); }P.Subjects.Add(R); }
    P.Subjects.Sort([](const auto& A,const auto& B){return A.Id<B.Id;});
    if(Cargo) if(auto L=Lift(GetWorld()))
        for(TActorIterator<APersonaProp> It(GetWorld());It;++It) if(!It->Holder)
        {
            const auto At=L->GetActorTransform().InverseTransformPosition(It->Mesh->Bounds.Origin);
            if(FMath::Abs(At.X)>275 || FMath::Abs(At.Y)>275 || At.Z< -25 || At.Z>220) continue;
            FPersonaBattleCargo R;R.Item=UPersonaLoot::Capture(*It);R.Relative=It->GetActorTransform().GetRelativeTransform(L->GetActorTransform());P.Cargo.Add(R);
        }
    return P;
}
bool UPersonaBattleSubsystem::ValidateParty(const FPersonaBattleParty& P,FString& Error) const
{
    if(P.Subjects.IsEmpty() || P.Subjects.Num()>4 || !Guid(P.Sector) || P.Owner.IsEmpty() || P.Cargo.Num()>1024) { Error=TEXT("Некорректный состав сектора.");return false; }
    TSet<FString> Ids;bool HasOwner=false;FInventoryBudget Budget;
    auto ValidItem=[&](const FPersonaInventoryRecord& I)
    {
        if(I.ActorClass.IsEmpty()) return I.Quantity==0 || I.Definition.IsEmpty();
        if(I.Id.IsEmpty() || Ids.Contains(I.Id) || I.ContainerState.Len()>256*1024) return false;
        auto Catalog=UPersonaItemCatalog::Get();auto D=Catalog ? Catalog->Find(I.Definition) : nullptr;
        if(!D && !I.Definition.IsEmpty()) return false;
        if(I.Quantity<1 || I.Quantity>(D ? D->MaxStack : 1)) return false;
        auto Class=LoadClass<APersonaProp>(nullptr,*I.ActorClass);if(!Class) return false;
        Ids.Add(I.Id);return Budget.Add(I);
    };
    for(const auto& S:P.Subjects)
    {
        if(S.Id.IsEmpty() || Ids.Contains(S.Id) || S.NPC || S.Dead || !FMath::IsFinite(S.Health) || S.Health<=0 || S.Inventory.Num()>3) { Error=TEXT("Некорректный участник.");return false; }
        Ids.Add(S.Id);HasOwner|=S.Id==P.Owner;
        for(const auto& I:S.Inventory) if(!ValidItem(I)) { Error=TEXT("Некорректное снаряжение.");return false; }
    }
    for(const auto& C:P.Cargo) if(!ValidItem(C.Item)) { Error=TEXT("Некорректный груз.");return false; }
    if(!HasOwner) { Error=TEXT("Организатор должен участвовать в бою.");return false; }return true;
}
void UPersonaBattleSubsystem::SetOpen(APersonaPlayerController* PC,bool Open)
{
    FString Error;if(!PC || !PC->IsHost() || Phase!=EPersonaBattlePhase::Idle || !CanPrepare(Error)) { UpdateState(Error);return; }
    if(!StartTransport()) { UpdateState(TEXT("Не удалось открыть канал связи с секторами."));return; }
    if(auto S=APersonaBattleState::Get(GetWorld())) { S->Open=Open;LastAdvertise=0;UpdateState(Open ? TEXT("Сектор открыт для вызовов.") : TEXT("Приём вызовов закрыт.")); }
}
void UPersonaBattleSubsystem::Find(bool Friends)
{ if(!Host(GetWorld())) return;FriendsOnly=Friends;StartTransport();GetGameInstance()->GetSubsystem<UPersonaOnline>()->FindBattles(); }
bool UPersonaBattleSubsystem::Send(const FString& Kind,const TSharedPtr<FJsonObject>& Payload)
{
    if(!Transport || PeerEndpoint.IsEmpty()) return false;
    auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("project"),TEXT("persona-sector-battle"));O->SetNumberField(TEXT("protocol"),1);
    O->SetStringField(TEXT("kind"),Kind);O->SetStringField(TEXT("match"),Ticket.Match);O->SetStringField(TEXT("secret"),Ticket.Secret);
    if(Payload) O->SetObjectField(TEXT("payload"),Payload);
    FString Message;FJsonSerializer::Serialize(O,TJsonWriterFactory<>::Create(&Message));return Transport->Send(PeerEndpoint,Owner(),Message);
}
void UPersonaBattleSubsystem::Challenge(int32 Index)
{
    FString Error;if(!Host(GetWorld()) || Phase!=EPersonaBattlePhase::Idle || !Listings.IsValidIndex(Index) || !CanPrepare(Error)) { UpdateState(Error);return; }
    if(!StartTransport()) { UpdateState(TEXT("Не удалось открыть канал связи с секторами."));return; }
    const auto L=Listings[Index];auto Own=BuildParty(false);if(Own.Subjects.Num()!=L.Players || L.Owner==Owner()) { UpdateState(TEXT("Для боя нужны равные составы."));return; }
    if(Own.Sector==L.Sector) { UpdateState(TEXT("Для боя нужны два разных сектора."));return; }
    Ticket=FPersonaBattleTicket();Ticket.Match=FGuid::NewGuid().ToString(EGuidFormats::Digits);Ticket.Secret=FGuid::NewGuid().ToString(EGuidFormats::Digits);Ticket.Master=Owner();Ticket.Parties.Add(Own);
    FPersonaBattleParty Other;Other.Owner=L.Owner;Other.Sector=L.Sector;Other.Title=L.Title;Other.Subjects.SetNum(L.Players);Ticket.Parties.Add(Other);
    Peer=L.Owner;PeerEndpoint=L.Endpoint;Transport->Allow(Peer,true);auto O=Object(Ticket);O->SetStringField(TEXT("endpoint"),ControlEndpoint());
    if(!Send(TEXT("offer"),O)) { UpdateState(TEXT("Не удалось отправить вызов."));return; }
    Deadline=FPlatformTime::Seconds()+30;SetPhase(EPersonaBattlePhase::Offered,TEXT("Вызов отправлен. Ожидание ответа."));
}
void UPersonaBattleSubsystem::Answer(APersonaPlayerController* PC,bool Accept)
{
    if(!PC || !PC->IsHost()) return;
    if(!Accept) { Send(TEXT("cancel"));Abort(TEXT("Вызов отменён."));return; }
    FString Error;if(Phase!=EPersonaBattlePhase::Offered || Ticket.Master==Owner() || !CanPrepare(Error)) { UpdateState(Error);return; }
    auto P=BuildParty(false);if(P.Subjects.Num()!=Ticket.Parties[0].Subjects.Num()) { Abort(TEXT("Состав группы изменился."));return; }
    Ticket.Parties[1]=P;Send(TEXT("accept"),Object(P));Deadline=0;
    for(auto C:Controllers(GetWorld())) C->GetPlayerState<APersonaPlayerState>()->bBattleReady=false;
    SetPhase(EPersonaBattlePhase::Gathering,TEXT("Подтвердите готовность и соберитесь на лифте."));
}
void UPersonaBattleSubsystem::SetReady(APersonaPlayerController* PC,bool Ready)
{
    if(!PC || Phase!=EPersonaBattlePhase::Gathering) return;
    auto P=PC->GetPlayerState<APersonaPlayerState>();if(!P || !Ticket.Contains(P->PersistentId)) return;P->bBattleReady=Ready;UpdateState();
}
bool UPersonaBattleSubsystem::UseLift(APersonaCharacter* C)
{
    if(IsArena()) { if(auto A=APersonaBattleArena::Get(GetWorld());A && A->IsOnLift(C)) { A->UseLift(C);return true; }return false; }
    if(Phase==EPersonaBattlePhase::Idle || Phase==EPersonaBattlePhase::Offered) return false;
    if(Phase!=EPersonaBattlePhase::Gathering) return true;
    auto L=Lift(GetWorld());
    for(auto PC:Controllers(GetWorld())) if(!PC->GetPlayerState<APersonaPlayerState>()->bBattleReady || !OnBoard(Cast<APersonaCharacter>(PC->GetPawn()),L))
    { if(C) C->Interaction->ClientFeedback(TEXT("Все участники должны подтвердить готовность и встать на лифт."));return true; }
    PrepareParty();return true;
}
FString UPersonaBattleSubsystem::JournalPath(const FString& Match) const
{
    FString Diagnostic;const bool Test=FParse::Value(FCommandLine::Get(),TEXT("PersonaDiagnostics="),Diagnostic);
    return FPaths::ProjectSavedDir()/TEXT("SaveGames")/(Test ? TEXT("Validation/Battles") : TEXT("Battles"))/(GetGameInstance()->GetSubsystem<UPersonaSaveSubsystem>()->LocalIdentity()+TEXT("-")+Match+TEXT(".sav"));
}
bool UPersonaBattleSubsystem::SaveJournal()
{
    if(!Journal || !Guid(Journal->Ticket.Match)) return false;TArray<uint8> Data;if(!UGameplayStatics::SaveGameToMemory(Journal,Data)) return false;
    const FString Path=JournalPath(Journal->Ticket.Match);IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);
    const FString Temp=Path+TEXT(".tmp");return FFileHelper::SaveArrayToFile(Data,*Temp) && IFileManager::Get().Move(*Path,*Temp,true,true);
}
UPersonaBattleJournal* UPersonaBattleSubsystem::LoadJournal(const FString& Match)
{
    if(!Guid(Match)) return nullptr;TArray<uint8> Data;if(!FFileHelper::LoadFileToArray(Data,*JournalPath(Match))) return nullptr;
    return Cast<UPersonaBattleJournal>(UGameplayStatics::LoadGameFromMemory(Data));
}
void UPersonaBattleSubsystem::PrepareParty()
{
    auto S=GetGameInstance()->GetSubsystem<UPersonaSaveSubsystem>();const int32 Team=Ticket.Master==Owner() ? 0 : 1;
    auto Platform=Lift(GetWorld());
    for(TActorIterator<APersonaProp> It(GetWorld());It;++It)
    {
        if(It->Holder) { GlobalPropIds(*It,S->Active->Slot);continue; }
        if(!Platform) continue;const auto At=Platform->GetActorTransform().InverseTransformPosition(It->Mesh->Bounds.Origin);
        if(FMath::Abs(At.X)<=275 && FMath::Abs(At.Y)<=275 && At.Z>=-25 && At.Z<=220) GlobalPropIds(*It,S->Active->Slot);
    }
    auto P=BuildParty(true);FString Error;if(!ValidateParty(P,Error)) { Abort(Error);return; }
    Ticket.Parties[Team]=P;
    if(!S->SaveWorld()) { Abort(TEXT("Не удалось сохранить сектор перед боем."));return; }
    Journal=NewObject<UPersonaBattleJournal>(this);Journal->Ticket=Ticket;Journal->LocalOwner=Owner();Journal->LocalSector=P.Sector;Journal->LocalTeam=Team;Journal->PeerEndpoint=PeerEndpoint;
    if(!UGameplayStatics::SaveGameToMemory(S->Active,Journal->OriginalSector) || !SaveJournal()) { Abort(TEXT("Не удалось сохранить груз боя."));return; }
    S->Active->PendingBattle=Ticket.Match;S->Active->Version=8;
    if(!S->WriteSelectedSector()) { Abort(TEXT("Не удалось закрепить вещи за боем."));return; }
    S->PrepareTravel();bOwnPrepared=true;Deadline=FPlatformTime::Seconds()+120;
    for(auto PC:Controllers(GetWorld())) if(auto C=Cast<APersonaCharacter>(PC->GetPawn()))
    { C->bTransit=true;C->OnRep_Transit();C->Interaction->CancelActions();C->ForceNetUpdate(); }
    SetPhase(EPersonaBattlePhase::Prepared,TEXT("Груз сохранён. Ожидание второго сектора."));Send(TEXT("prepared"),Object(P));
    if(Team==0 && bPeerManifest) DistributeTicket();
}
void UPersonaBattleSubsystem::DistributeTicket()
{
    if(!Journal) return;
    Journal->Ticket=Ticket;if(!SaveJournal()) { Abort(TEXT("Не удалось сохранить общий состав."));return; }
    if(Ticket.Master==Owner()) Send(TEXT("ticket"),Object(Ticket));
    FPersonaBattleTicket Small=Ticket;
    for(auto& P:Small.Parties) { P.Cargo.Reset();for(auto& S:P.Subjects) { S.Inventory.Reset();S.Loot.Reset();S.BodyMotion.Reset();S.CorpsePose.Reset(); } }
    TicketAcks.Reset();const int32 Team=Ticket.Master==Owner() ? 0 : 1;
    for(auto PC:Controllers(GetWorld())) PC->ClientBattleTicket(Small,Team);
}
void UPersonaBattleSubsystem::AcceptTicket(const FPersonaBattleTicket& In,int32 Team)
{
    if(In.Protocol!=1 || !Guid(In.Match) || !Guid(In.Secret) || In.Parties.Num()!=2 || Team<0 || Team>1 || In.TeamFor(Owner())!=Team) return;
    // A restarted sector owner still owns the full escrow journal. Never replace
    // it with the inventory-free ticket used for ordinary travelling clients.
    if(!Journal || Journal->Ticket.Match!=In.Match) Journal=LoadJournal(In.Match);
    if(Journal && (Journal->Ticket.Secret!=In.Secret || Journal->LocalTeam!=Team)) return;
    const bool Own=Journal && Journal->Ticket.Match==In.Match && !Journal->OriginalSector.IsEmpty();
    if(!Own)
    { Journal=NewObject<UPersonaBattleJournal>(this);Journal->Ticket=In;Journal->LocalOwner=In.Parties[Team].Owner;Journal->LocalSector=In.Parties[Team].Sector;Journal->LocalTeam=Team;Ticket=In; }
    if(Own)
    {
        Ticket=Journal->Ticket;Peer=Ticket.Parties[1-Team].Owner;PeerEndpoint=Journal->PeerEndpoint;
        if(!StartTransport()) return;Transport->Allow(Peer,true);
    }
    if(!SaveJournal()) return;
    if(IsArena()) SetPhase(EPersonaBattlePhase::Waiting,TEXT("Восстановлено участие в бою…"));
    else if(!Own) SetPhase(EPersonaBattlePhase::Prepared,TEXT("Подготовка к отправлению…"));
    if(auto PC=Cast<APersonaPlayerController>(GetWorld()->GetFirstPlayerController()))
    { PC->CloseOverlays();PC->ServerBattleTicketAck(In.Match); }
}
void UPersonaBattleSubsystem::AcknowledgeTicket(APersonaPlayerController* PC,const FString& Match)
{
    if(!Journal || Match!=Ticket.Match || !PC || Phase!=EPersonaBattlePhase::Prepared) return;
    auto P=PC->GetPlayerState<APersonaPlayerState>();if(!P || Ticket.TeamFor(P->PersistentId)!=Journal->LocalTeam) return;
    TicketAcks.Add(P->PersistentId);
    for(const auto& S:Ticket.Parties[Journal->LocalTeam].Subjects) if(!TicketAcks.Contains(S.Id)) return;
    bOwnTicketReady=true;
    if(Journal->LocalTeam==1) Send(TEXT("party_ready"));
    else if(bPeerTicketReady) Launch();
}
void UPersonaBattleSubsystem::Launch()
{
    if(bLaunching || !Journal || !bOwnTicketReady || (Journal->LocalTeam==0 && !bPeerTicketReady)) return;
    bLaunching=true;Journal->Phase=EPersonaBattleJournalPhase::Started;if(!SaveJournal()) { Abort(TEXT("Не удалось сохранить отправление."));return; }
    SetPhase(EPersonaBattlePhase::Travelling,TEXT("Переход на поле боя…"));
    if(Journal->LocalTeam==0)
    {
        Send(TEXT("launch"));FTimerHandle Timer;
        GetWorld()->GetTimerManager().SetTimer(Timer,FTimerDelegate::CreateWeakLambda(this,[this]()
        { GetWorld()->ServerTravel(TEXT("/Game/Persona/Maps/L_SectorBattle?Mode=Teams")); }),1.2f,false);
    }
    else
    {
        // Remote clients receive their travel request before their host leaves.
        for(auto PC:Controllers(GetWorld())) if(!PC->IsLocalController()) PC->ClientBattleConnect();
        FTimerHandle Timer;GetWorld()->GetTimerManager().SetTimer(Timer,FTimerDelegate::CreateWeakLambda(this,[this]() { ConnectArena(); }),1.2f,false);
    }
}
void UPersonaBattleSubsystem::ConnectArena()
{
    if(!Journal) return;Journal->Phase=EPersonaBattleJournalPhase::Started;SaveJournal();
    SetPhase(EPersonaBattlePhase::Travelling,TEXT("Подключение к полю боя…"));Deadline=FPlatformTime::Seconds()+120;NextSearch=0;
    GetGameInstance()->GetSubsystem<UPersonaSaveSubsystem>()->PrepareTravel();GetGameInstance()->GetSubsystem<UPersonaOnline>()->SwitchToMenu();
}
void UPersonaBattleSubsystem::ArenaStarted()
{
    if(!Journal || Ticket.Parties.Num()!=2) return;
    Journal->Phase=EPersonaBattleJournalPhase::Started;SaveJournal();Deadline=FPlatformTime::Seconds()+120;
    SetPhase(EPersonaBattlePhase::Waiting,TEXT("Ожидание участников и готовности арены…"));
    GetGameInstance()->GetSubsystem<UPersonaOnline>()->StartBattleSession(Ticket);
}
FString UPersonaBattleSubsystem::ManifestHash() const { return Hash(Ticket); }
void UPersonaBattleSubsystem::Settle(const FPersonaBattleSettlement& In)
{
    if(!Journal || Ticket.Master!=Owner() || Phase==EPersonaBattlePhase::Settling) return;
    if(!Conserves(Ticket,In)) { Abort(TEXT("Проверка груза не пройдена. Исходные вещи возвращены."));return; }
    Journal->Settlement=In;Journal->Settlement.Match=Ticket.Match;Journal->Settlement.ManifestHash=ManifestHash();Journal->ResultHash=Hash(Journal->Settlement);
    Journal->Phase=EPersonaBattleJournalPhase::ResultPrepared;if(!SaveJournal()) { Abort(TEXT("Не удалось сохранить результат боя."));return; }
    SetPhase(EPersonaBattlePhase::Settling,TEXT("Сохранение результата в обоих секторах…"));Deadline=FPlatformTime::Seconds()+30;
    auto O=Object(Journal->Settlement);O->SetStringField(TEXT("hash"),Journal->ResultHash);Send(TEXT("result_prepare"),O);
    for(auto PC:Controllers(GetWorld())) if(!PC->IsLocalController()) PC->ClientBattleReceipt(Ticket.Match,Journal->ResultHash,false);
}
void UPersonaBattleSubsystem::ReceiveSettlement(const FPersonaBattleSettlement& R,const FString& H,bool Commit)
{
    if(!Journal || Journal->Phase==EPersonaBattleJournalPhase::Aborted || R.Match!=Ticket.Match || R.ManifestHash!=ManifestHash() || H!=Hash(R) || R.Winner< -1 || R.Winner>1) return;
    // Control messages and gameplay RPCs have independent ordering. A delayed
    // prepare/receipt must never undo an already durable commit.
    if(Journal->Phase==EPersonaBattleJournalPhase::Committed || Journal->Phase==EPersonaBattleJournalPhase::Applied) return;
    TSet<FString> Ids;
    auto Add=[&](const FPersonaInventoryRecord& I)
    {
        if(I.ActorClass.IsEmpty()) return true;
        if(I.Id.IsEmpty() || Ids.Contains(I.Id) || I.Quantity<1 || I.Quantity>999) return false;
        Ids.Add(I.Id);return true;
    };
    for(const auto& S:R.Survivors)
    { if(Ticket.TeamFor(S.Id)!=R.Winner || S.Dead || S.Inventory.Num()>3) return;for(const auto& I:S.Inventory) if(!Add(I)) return; }
    for(const auto& C:R.Cargo) if(!Add(C.Item)) return;
    if(!Conserves(Ticket,R)) return;
    Journal->Settlement=R;Journal->ResultHash=H;
    Journal->Phase=Commit ? EPersonaBattleJournalPhase::Committed : EPersonaBattleJournalPhase::ResultPrepared;
    if(!SaveJournal()) return;
    if(!Commit) { auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("hash"),H);Send(TEXT("result_vote"),O); }
}
void UPersonaBattleSubsystem::ReceiveReceiptStatus(const FString& Match,const FString& H,bool Commit)
{
    if(!Journal || Match!=Ticket.Match || Journal->Phase==EPersonaBattleJournalPhase::Aborted) return;
    if(Journal->Phase==EPersonaBattleJournalPhase::Applied || (Journal->Phase==EPersonaBattleJournalPhase::Committed && (!Commit || H!=Journal->ResultHash))) return;
    if(Journal->LocalOwner==Owner())
    { if(Journal->ResultHash!=H || Journal->Settlement.Match!=Match) return; }
    Journal->ResultHash=H;Journal->Phase=Commit ? EPersonaBattleJournalPhase::Committed : EPersonaBattleJournalPhase::ResultPrepared;SaveJournal();
}
void UPersonaBattleSubsystem::CommitResult()
{
    if(!Journal || Journal->Phase!=EPersonaBattleJournalPhase::ResultPrepared) return;
    Journal->Phase=EPersonaBattleJournalPhase::Committed;if(!SaveJournal()) { Abort(TEXT("Не удалось подтвердить результат."));return; }
    auto O=Object(Journal->Settlement);O->SetStringField(TEXT("hash"),Journal->ResultHash);Send(TEXT("result_commit"),O);
    if(bDiagnosticHoldReturn) return;
    for(auto PC:Controllers(GetWorld())) if(!PC->IsLocalController()) { PC->ClientBattleReceipt(Ticket.Match,Journal->ResultHash,true);PC->ClientBattleReturn(false); }
    FTimerHandle Timer;GetWorld()->GetTimerManager().SetTimer(Timer,FTimerDelegate::CreateWeakLambda(this,[this]() { Return(false); }),1.f,false);
}
bool UPersonaBattleSubsystem::ApplyResult(bool Aborted)
{
    if(!Journal || Journal->LocalOwner!=Owner() || Journal->OriginalSector.IsEmpty()) return false;
    auto Saves=GetGameInstance()->GetSubsystem<UPersonaSaveSubsystem>();
    if(Saves->Active && Saves->Active->Slot==Journal->LocalSector && Saves->Active->AppliedBattles.Contains(Ticket.Match))
    { Saves->Active->PendingBattle.Reset();return Saves->WriteSelectedSector(); }
    if(!Aborted && Journal->Phase!=EPersonaBattleJournalPhase::Committed && Journal->Phase!=EPersonaBattleJournalPhase::Applied) return false;
    auto Original=Cast<UPersonaSectorSave>(UGameplayStatics::LoadGameFromMemory(Journal->OriginalSector));if(!Original) return false;
    Original->PendingBattle.Reset();Original->Version=8;Original->Updated=FDateTime::UtcNow();
    if(!Aborted)
    {
        const auto& Own=Ticket.Parties[Journal->LocalTeam];const auto& R=Journal->Settlement;
        TSet<FString> CargoIds;for(const auto& C:Own.Cargo) CargoIds.Add(C.Item.Id);
        auto M=Original->Maps.FindByPredicate([](const auto& Map){return Map.Map.EndsWith(TEXT("L_Hub"));});if(!M) return false;
        M->Props.RemoveAll([&](const auto& P){return CargoIds.Contains(P.Id);});
        int32 Index=0;
        for(const auto& Start:Own.Subjects)
        {
            auto S=Start;S.Inventory.Reset();S.Loot.Reset();S.Dead=S.Spectator=S.Transit=S.Crouched=S.Sprinting=S.DryCollapse=false;
            S.CorpsePose.Reset();S.BodyMotion.Reset();S.CorpseMoving=false;S.Team=0;S.Ready=false;S.Movement=MOVE_Walking;S.Velocity=FVector::ZeroVector;
            if(const auto Alive=R.Survivors.FindByPredicate([&](const auto& V){return V.Id==S.Id;}))
            { S.Inventory=Alive->Inventory;S.Health=Alive->Health;S.Stamina=Alive->Stamina;S.Hunger=Alive->Hunger;S.Water=Alive->Water; }
            else { S.Health=S.MaxHealth;S.Hunger=FMath::Max(50.f,S.Hunger);S.Water=FMath::Max(50.f,S.Water); }
            S.Transform=FTransform(FRotator::ZeroRotator,FVector(-180,1100+Index++*110,100));
            M->Subjects.RemoveAll([&](const auto& V){return V.Id==S.Id;});M->Subjects.Add(S);
            Original->Players.RemoveAll([&](const auto& V){return V.Id==S.Id;});Original->Players.Add(S);
        }
        TArray<FPersonaBattleCargo> Export;
        if(R.Winner==Journal->LocalTeam) Export=R.Cargo;
        else if(R.Winner==-1) for(const auto& C:R.DrawCargo) if(CargoIds.Contains(C.Item.Id)) Export.Add(C);
        Index=0;
        for(const auto& C:Export)
        {
            FPersonaObjectRecord P;P.Id=C.Item.Id;P.ActorClass=C.Item.ActorClass;P.Definition=C.Item.Definition;P.Quantity=C.Item.Quantity;P.Weapon=C.Item.Weapon;P.ContainerState=C.Item.ContainerState;
            P.Transform=FTransform(FRotator::ZeroRotator,FVector(160+(Index%6)*70,1100+(Index/6)*70,60));P.Sleeping=false;M->Props.Add(P);++Index;
        }
        Original->AppliedBattles.AddUnique(Ticket.Match);
    }
    Saves->Active=Original;if(!Saves->WriteSelectedSector()) return false;
    Journal->Phase=Aborted ? EPersonaBattleJournalPhase::Aborted : EPersonaBattleJournalPhase::Applied;return SaveJournal();
}
void UPersonaBattleSubsystem::Return(bool Aborted)
{
    if(!Journal || Phase==EPersonaBattlePhase::Returning) return;
    if(Journal->LocalOwner==Owner() && !ApplyResult(Aborted))
    { SetPhase(EPersonaBattlePhase::Recovering,TEXT("Ожидание подтверждённой квитанции боя…"));NextSearch=0;GetGameInstance()->GetSubsystem<UPersonaOnline>()->SwitchToMenu();return; }
    if(Aborted) { Journal->Phase=EPersonaBattleJournalPhase::Aborted;SaveJournal(); }
    ReturningHost=Journal->LocalOwner==Owner();ReturningOwner=Journal->LocalOwner;ReturningSector=Journal->LocalSector;bHostIssued=false;
    SetPhase(EPersonaBattlePhase::Returning,TEXT("Возвращение в свой сектор…"));NextSearch=0;
    GetGameInstance()->GetSubsystem<UPersonaSaveSubsystem>()->PrepareTravel();GetGameInstance()->GetSubsystem<UPersonaOnline>()->SwitchToMenu();
}
void UPersonaBattleSubsystem::Abort(const FString& Reason)
{
    if(Journal && (Journal->Phase==EPersonaBattleJournalPhase::Committed || Journal->Phase==EPersonaBattleJournalPhase::Applied)) { Return(false);return; }
    if(IsArena() || Phase==EPersonaBattlePhase::Travelling || Phase==EPersonaBattlePhase::Recovering)
    {
        if(GetWorld()->GetNetMode()!=NM_Client) for(auto P:Controllers(GetWorld())) if(!P->IsLocalController()) P->ClientBattleReturn(true);
        FTimerHandle T;GetWorld()->GetTimerManager().SetTimer(T,FTimerDelegate::CreateWeakLambda(this,[this]() { Return(true); }),.7f,false);UpdateState(Reason);return;
    }
    if(Journal && Journal->Ticket.Match==Ticket.Match && !Journal->OriginalSector.IsEmpty()) ApplyResult(true);
    for(auto PC:Controllers(GetWorld())) if(auto C=Cast<APersonaCharacter>(PC->GetPawn())) { C->bTransit=false;C->OnRep_Transit();C->ForceNetUpdate(); }
    GetGameInstance()->GetSubsystem<UPersonaSaveSubsystem>()->ResumeWorldSaving();
    Journal=nullptr;Ticket=FPersonaBattleTicket();TicketAcks.Reset();bOwnPrepared=bPeerManifest=bOwnTicketReady=bPeerTicketReady=bLaunching=false;Deadline=0;
    SetPhase(EPersonaBattlePhase::Idle,Reason);
}
bool UPersonaBattleSubsystem::RecoverSector(UPersonaSectorSave*& Sector)
{
    if(!Sector || Sector->PendingBattle.IsEmpty()) return true;
    Journal=LoadJournal(Sector->PendingBattle);if(!Journal || Journal->LocalOwner!=Owner()) return false;
    Ticket=Journal->Ticket;Peer=Ticket.Parties[1-Journal->LocalTeam].Owner;PeerEndpoint=Journal->PeerEndpoint;
    if(!StartTransport()) return false;Transport->Allow(Peer,true);
    if(Journal->Phase==EPersonaBattleJournalPhase::ResultPrepared && Journal->LocalTeam==1)
    { SetPhase(EPersonaBattlePhase::Recovering,TEXT("Проверка незавершённой фиксации результата…"));NextSearch=0;return false; }
    auto S=GetGameInstance()->GetSubsystem<UPersonaSaveSubsystem>();S->Active=Sector;
    const bool Committed=Journal->Phase==EPersonaBattleJournalPhase::Committed || Journal->Phase==EPersonaBattleJournalPhase::Applied;
    if(!ApplyResult(!Committed)) return false;Sector=S->Active;return true;
}
bool UPersonaBattleSubsystem::HandleFailure()
{
    if(!Participating()) return false;
    auto Online=GetGameInstance()->GetSubsystem<UPersonaOnline>();
    if(Phase==EPersonaBattlePhase::Travelling || Phase==EPersonaBattlePhase::Returning) { if(!Online->bBusy) Online->SwitchToMenu();return true; }
    if(Journal->LocalOwner!=Owner() && Journal->Phase>=EPersonaBattleJournalPhase::ResultPrepared) { Return(false);return true; }
    SetPhase(EPersonaBattlePhase::Recovering,TEXT("Связь прервалась. Восстанавливаем подключение…"));Deadline=FPlatformTime::Seconds()+90;NextSearch=0;Online->SwitchToMenu();return true;
}
void UPersonaBattleSubsystem::Receive(const FString& Sender,const FString& Message)
{
    TSharedPtr<FJsonObject> O;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Message),O) || !O) return;
    double Protocol=0;if(TextField(O,TEXT("project"))!=TEXT("persona-sector-battle") || !O->TryGetNumberField(TEXT("protocol"),Protocol) || Protocol!=1) return;
    const FString Kind=TextField(O,TEXT("kind")),Match=TextField(O,TEXT("match")),Secret=TextField(O,TEXT("secret"));
    const TSharedPtr<FJsonObject>* Data=nullptr;O->TryGetObjectField(TEXT("payload"),Data);TSharedPtr<FJsonObject> P=Data ? *Data : nullptr;
    if(Kind==TEXT("query"))
    {
        auto J=LoadJournal(Match);if(!J || Secret!=J->Ticket.Secret || J->LocalOwner!=Owner() || J->Ticket.Parties.Num()!=2 || J->Ticket.Master!=Owner() || Sender!=J->Ticket.Parties[1].Owner) return;
        FString ReplyKind;TSharedPtr<FJsonObject> Reply;
        if(J->Phase==EPersonaBattleJournalPhase::Committed || J->Phase==EPersonaBattleJournalPhase::Applied)
        { ReplyKind=TEXT("result_commit");Reply=Object(J->Settlement);Reply->SetStringField(TEXT("hash"),J->ResultHash); }
        else if(J->Phase==EPersonaBattleJournalPhase::ResultPrepared && Ticket.Match==Match && Phase==EPersonaBattlePhase::Settling)
        { ReplyKind=TEXT("result_prepare");Reply=Object(J->Settlement);Reply->SetStringField(TEXT("hash"),J->ResultHash); }
        else if(J->Phase==EPersonaBattleJournalPhase::Started && Ticket.Match==Match && IsArena()) ReplyKind=TEXT("active");
        else ReplyKind=TEXT("cancel");
        auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("project"),TEXT("persona-sector-battle"));R->SetNumberField(TEXT("protocol"),1);
        R->SetStringField(TEXT("kind"),ReplyKind);R->SetStringField(TEXT("match"),Match);R->SetStringField(TEXT("secret"),Secret);if(Reply) R->SetObjectField(TEXT("payload"),Reply);
        FString Text;FJsonSerializer::Serialize(R,TJsonWriterFactory<>::Create(&Text));
        const auto Endpoint=GetGameInstance()->GetSubsystem<UPersonaOnline>()->IsSteam() ? TEXT("steam:")+Sender : TextField(P,TEXT("endpoint"));
        Transport->Send(Endpoint,Owner(),Text);return;
    }
    if(Kind==TEXT("offer"))
    {
        auto State=APersonaBattleState::Get(GetWorld());FString Error;FPersonaBattleTicket In;
        if(Phase!=EPersonaBattlePhase::Idle || !State || !State->Open || !CanPrepare(Error) || !Read(P,In)) return;
        auto Saves=GetGameInstance()->GetSubsystem<UPersonaSaveSubsystem>();
        if(In.Protocol!=1 || !Guid(In.Match) || !Guid(In.Secret) || In.Match!=Match || In.Secret!=Secret || In.Master!=Sender || In.Parties.Num()!=2 || In.Parties[0].Owner!=Sender || In.Parties[1].Owner!=Owner() || In.Parties[1].Sector!=Saves->Active->Slot || !ValidateParty(In.Parties[0],Error)) return;
        auto Own=BuildParty(false);if(Own.Subjects.Num()!=In.Parties[0].Subjects.Num()) return;
        Ticket=In;Ticket.Parties[1]=Own;Journal=nullptr;Peer=Sender;
        PeerEndpoint=GetGameInstance()->GetSubsystem<UPersonaOnline>()->IsSteam() ? TEXT("steam:")+Sender : TextField(P,TEXT("endpoint"));
        Transport->Allow(Peer,true);Deadline=FPlatformTime::Seconds()+30;
        SetPhase(EPersonaBattlePhase::Offered,TEXT("Получен вызов. Организатор может принять его в терминале."));return;
    }
    if(Sender!=Peer || Match!=Ticket.Match || Secret!=Ticket.Secret || !Guid(Match)) return;
    if(Kind==TEXT("cancel")) { Abort(TEXT("Бой отменён вторым сектором."));return; }
    if(Kind==TEXT("accept"))
    {
        FPersonaBattleParty Other;FString Error;
        if(Phase!=EPersonaBattlePhase::Offered || Ticket.Master!=Owner() || !Read(P,Other) || Other.Owner!=Sender || Other.Sector!=Ticket.Parties[1].Sector || Other.Subjects.Num()!=Ticket.Parties[0].Subjects.Num() || !ValidateParty(Other,Error)) return;
        Ticket.Parties[1]=Other;Deadline=0;for(auto C:Controllers(GetWorld())) C->GetPlayerState<APersonaPlayerState>()->bBattleReady=false;
        SetPhase(EPersonaBattlePhase::Gathering,TEXT("Вызов принят. Подтвердите готовность и соберитесь на лифте."));return;
    }
    if(Kind==TEXT("prepared"))
    {
        FPersonaBattleParty Other;FString Error;const int32 OtherTeam=Ticket.Master==Owner() ? 1 : 0;
        if((Phase!=EPersonaBattlePhase::Gathering && Phase!=EPersonaBattlePhase::Prepared) || !Read(P,Other) || Other.Owner!=Peer || Other.Sector!=Ticket.Parties[OtherTeam].Sector || !ValidateParty(Other,Error)) return;
        const auto& Expected=Ticket.Parties[OtherTeam].Subjects;if(Expected.Num()!=Other.Subjects.Num()) return;
        for(int32 I=0;I<Expected.Num();++I) if(Expected[I].Id!=Other.Subjects[I].Id) { Send(TEXT("cancel"));Abort(TEXT("Состав второго сектора изменился."));return; }
        Ticket.Parties[OtherTeam]=Other;bPeerManifest=true;if(bOwnPrepared && Ticket.Master==Owner()) DistributeTicket();return;
    }
    if(Kind==TEXT("ticket"))
    {
        FPersonaBattleTicket In;FString Error;
        if(!Journal || Journal->LocalTeam!=1 || Phase!=EPersonaBattlePhase::Prepared || !Read(P,In) || In.Match!=Ticket.Match || In.Secret!=Ticket.Secret || In.Master!=Peer || In.Parties.Num()!=2 || Hash(In.Parties[1])!=Hash(Ticket.Parties[1])) return;
        if(!ValidateParty(In.Parties[0],Error) || !ValidateParty(In.Parties[1],Error)) return;
        Ticket=In;DistributeTicket();return;
    }
    if(Kind==TEXT("party_ready")) { if(Ticket.Master==Owner() && Phase==EPersonaBattlePhase::Prepared) { bPeerTicketReady=true;if(bOwnTicketReady) Launch(); }return; }
    if(Kind==TEXT("launch")) { if(Journal && Journal->LocalTeam==1 && Phase==EPersonaBattlePhase::Prepared) Launch();return; }
    if(Kind==TEXT("result_prepare") || Kind==TEXT("result_commit"))
    {
        FPersonaBattleSettlement R;if(!Journal || Sender!=Ticket.Master || !Read(P,R)) return;
        const bool Commit=Kind==TEXT("result_commit");ReceiveSettlement(R,TextField(P,TEXT("hash")),Commit);
        if(Commit && Journal->Phase==EPersonaBattleJournalPhase::Committed && Phase==EPersonaBattlePhase::Recovering) Return(false);return;
    }
    if(Kind==TEXT("result_vote"))
    { if(Journal && Ticket.Master==Owner() && Phase==EPersonaBattlePhase::Settling && Sender==Ticket.Parties[1].Owner && TextField(P,TEXT("hash"))==Journal->ResultHash && !bDiagnosticHoldCommit) CommitResult();return; }
    if(Kind==TEXT("active") && Phase==EPersonaBattlePhase::Recovering && Journal && Journal->Phase==EPersonaBattleJournalPhase::Started) ConnectArena();
}
void UPersonaBattleSubsystem::Tick(float Delta)
{
    auto W=GetWorld();if(!W || !W->IsGameWorld()) return;
    if(Transport) Transport->Tick([this](const FString& Sender,const FString& Message){Receive(Sender,Message);});
    const double Now=FPlatformTime::Seconds();auto Online=GetGameInstance()->GetSubsystem<UPersonaOnline>();
    if(Phase==EPersonaBattlePhase::Returning && W->GetNetMode()==NM_Client && W->GetMapName().Contains(TEXT("L_Hub")))
    {
        bool Home=false;if(auto GS=W->GetGameState<APersonaGameState>()) for(auto P:GS->PlayerArray) if(auto PS=Cast<APersonaPlayerState>(P);PS && PS->PersistentId==ReturningOwner) Home=true;
        if(Home) { Phase=EPersonaBattlePhase::Idle;Journal=nullptr;Ticket=FPersonaBattleTicket();ReturningOwner.Reset();ReturningSector.Reset(); }
    }
    if(Host(W) && !IsArena())
    {
        UpdateState();auto State=APersonaBattleState::Get(W);const int32 Count=Controllers(W).Num();
        if(State && (Now-LastAdvertise>2 || Count!=LastGroupSize))
        { Online->AdvertiseBattle(State->Open && Phase==EPersonaBattlePhase::Idle,Count,ControlEndpoint());LastAdvertise=Now;LastGroupSize=Count; }
        if((Phase==EPersonaBattlePhase::Gathering || Phase==EPersonaBattlePhase::Prepared) && Ticket.Parties.Num()==2)
        {
            auto P=BuildParty(false);const auto& Expected=Ticket.Parties[Ticket.Master==Owner() ? 0 : 1].Subjects;
            bool Same=P.Subjects.Num()==Expected.Num();if(Same) for(int32 I=0;I<Expected.Num();++I) Same&=P.Subjects[I].Id==Expected[I].Id;
            if(!Same) { Send(TEXT("cancel"));Abort(TEXT("Состав группы изменился. Вызов отменён."));return; }
        }
    }
    if(Deadline>0 && Now>Deadline)
    {
        Deadline=0;
        if(Phase==EPersonaBattlePhase::Offered || Phase==EPersonaBattlePhase::Prepared || Phase==EPersonaBattlePhase::Settling)
        { Send(TEXT("cancel"));Abort(TEXT("Ожидание второго сектора истекло. Вещи возвращены."));return; }
        if(Phase==EPersonaBattlePhase::Travelling || (Phase==EPersonaBattlePhase::Recovering && Journal && Journal->Phase<EPersonaBattleJournalPhase::ResultPrepared)) { Return(true);return; }
    }
    if(Now<NextSearch || Online->bBusy) return;
    if(Phase==EPersonaBattlePhase::Returning && W->GetMapName().Contains(TEXT("L_MainMenu")))
    {
        NextSearch=Now+3;
        if(ReturningHost && !bHostIssued) { bHostIssued=true;Online->Host(); }
        else if(!ReturningHost) Online->FindHome(ReturningOwner,ReturningSector);
    }
    else if((Phase==EPersonaBattlePhase::Travelling || Phase==EPersonaBattlePhase::Recovering) && W->GetMapName().Contains(TEXT("L_MainMenu")) && Journal)
    {
        NextSearch=Now+3;
        if(Phase==EPersonaBattlePhase::Recovering && Journal->Phase==EPersonaBattleJournalPhase::ResultPrepared)
        { StartTransport();auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("endpoint"),ControlEndpoint());Send(TEXT("query"),O); }
        else Online->FindBattleMatch(Ticket.Match);
    }
}
