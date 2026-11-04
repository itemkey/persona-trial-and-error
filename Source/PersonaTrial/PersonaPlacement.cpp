#include "PersonaInteraction.h"
#include "PersonaCharacter.h"
#include "PersonaFramework.h"
#include "PersonaHub.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"

bool UPersonaInteraction::FindCookwarePlacement(FTransform& Transform,bool& Valid) const
{
    Valid=false;
    auto C=Cast<APersonaCharacter>(GetOwner());
    if(!C || !Held || !Held->Definition || !Held->Definition->bStoveCookware || !Held->Mesh->GetStaticMesh()) return false;
    FVector Eye; FRotator View; C->GetActorEyesViewPoint(Eye,View);
    const FVector Direction=View.Vector();
    APersonaHubObject* Stove=nullptr; FVector Surface; float Nearest=Reach;
    for(TActorIterator<APersonaHubObject> It(GetWorld());It;++It)
    {
        if(!It->Definition || It->Definition->Kind!=EPersonaHubKind::Stove) continue;
        const auto Frame=It->GetActorTransform();
        const FVector Normal=Frame.GetRotation().GetUpVector();
        const float Down=FVector::DotProduct(Direction,Normal); if(Down>=-.001f) continue;
        for(const FVector& Burner:It->Definition->Burners)
        {
            const FVector Center=Frame.TransformPosition(Burner);
            const float Distance=FVector::DotProduct(Center-Eye,Normal)/Down;
            if(Distance<0 || Distance>Nearest) continue;
            const FVector LocalHit=Frame.InverseTransformPosition(Eye+Direction*Distance);
            if(FVector::DistSquaredXY(LocalHit,Burner)>FMath::Square(It->Definition->BurnerRadius)) continue;
            Stove=*It; Surface=Center; Nearest=Distance;
        }
    }
    if(!Stove) return false;
    const FBox Bounds=Held->Mesh->GetStaticMesh()->GetBoundingBox();
    const FVector Scale=Held->Mesh->GetComponentScale().GetAbs();
    const FQuat Rotation=Stove->GetActorQuat();
    // Align the mesh's actual base, not its authoring pivot, to the hob.
    const FVector Base(Bounds.GetCenter().X,Bounds.GetCenter().Y,Bounds.Min.Z);
    Transform=FTransform(Rotation,Surface+Rotation.GetUpVector()*.8f-Rotation.RotateVector(Base*Scale),Scale);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(CookwarePlacement),false,C);
    for(auto Item:Slots) if(Item) Query.AddIgnoredActor(Item);
    FHitResult Hit;
    GetWorld()->LineTraceSingleByChannel(Hit,Eye,Eye+Direction*(Nearest+2.f),ECC_Visibility,Query);
    if(Hit.GetActor()!=Stove) return true;
    const FVector Extent=(Bounds.GetExtent()*Scale-FVector(.2f)).ComponentMax(FVector(.1f));
    const FVector Center=Transform.TransformPosition(Bounds.GetCenter());
    const FCollisionResponseParams Responses(Held->Mesh->GetCollisionResponseToChannels());
    Valid=!GetWorld()->OverlapBlockingTestByChannel(Center,Rotation,ECC_PhysicsBody,FCollisionShape::MakeBox(Extent),Query,Responses);
    return true;
}

void UPersonaInteraction::TickComponent(float Delta,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(Delta,TickType,ThisTickFunction);
    auto C=Cast<APersonaCharacter>(GetOwner());
    auto PC=C ? Cast<APersonaPlayerController>(C->Controller) : nullptr;
    FTransform Placement; bool Valid=false;
    const bool Show=C && C->IsLocallyControlled() && C->IsAlive() && !C->bTransit && PC &&
        !PC->IsPanelOpen() && !PC->bShowMouseCursor && !PC->IsLookInputIgnored() && !LootTarget && !bCharging &&
        FindCookwarePlacement(Placement,Valid) && Valid;
    if(Show)
    {
        if(!PlacementPreview)
        {
            PlacementPreview=NewObject<UStaticMeshComponent>(C,TEXT("CookwarePreview"));
            PlacementPreview->SetupAttachment(C->GetRootComponent());
            PlacementPreview->SetAbsolute(true,true,true);
            PlacementPreview->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            PlacementPreview->SetCanEverAffectNavigation(false);
            PlacementPreview->SetCastShadow(false);
            PlacementPreview->RegisterComponent();
        }
        PlacementPreview->SetStaticMesh(Held->Mesh->GetStaticMesh());
        for(int32 I=0;I<PlacementPreview->GetNumMaterials();++I) PlacementPreview->SetMaterial(I,Held->Definition->PlacementMaterial);
        PlacementPreview->SetWorldTransform(Placement);
    }
    if(PlacementPreview) PlacementPreview->SetVisibility(Show);
}

void UPersonaInteraction::ServerPlaceCookware_Implementation(APersonaProp* ExpectedItem)
{
    auto C=Cast<APersonaCharacter>(GetOwner());
    if(!C || !C->IsAlive() || C->bTransit || !Held || Held!=ExpectedItem || Held->Holder!=C || ServerLootTarget.IsValid()) return;
    FTransform Placement; bool Valid=false;
    if(!FindCookwarePlacement(Placement,Valid) || !Valid)
    { ClientFeedback(TEXT("Конфорка занята или недоступна")); return; }
    CancelActions();
    auto Pot=Held; Remove(Pot); Pot->Holder=nullptr; Pot->bStowed=false; Pot->SetOwner(nullptr);
    Pot->SetActorTransform(Placement,false,nullptr,ETeleportType::TeleportPhysics);
    Pot->OnRep_Holder();
    Pot->Mesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
    Pot->Mesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
    Pot->Mesh->WakeAllRigidBodies(); // Settle the sub-centimetre clearance onto the burner.
    Pot->ForceNetUpdate();
}
