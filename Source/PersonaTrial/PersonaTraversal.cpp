#include "PersonaTraversal.h"
#include "PersonaCharacter.h"
#include "PersonaMovement.h"
#include "PersonaAI.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "NavLinkCustomComponent.h"
#include "NavigationSystem.h"
#include "EngineUtils.h"

APersonaClimbLink::APersonaClimbLink()
{
    PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickInterval=.2f;
    PointLinks.Reset(); bSmartLinkIsRelevant=true;
    OnSmartLinkReached.AddDynamic(this,&ThisClass::Reached);
}
void APersonaClimbLink::Configure(const FVector& From,const FVector& To,UPrimitiveComponent* Surface)
{
    Low=From; High=To; Support=Surface; SupportTransform=Surface->GetComponentTransform();
    GetSmartLinkComp()->SetLinkData(From-GetActorLocation(),To-GetActorLocation(),ENavLinkDirection::BothWays);
    SetSmartLinkEnabled(true);
}
void APersonaClimbLink::Reached(AActor* Agent,const FVector& Destination)
{
    auto C=Cast<APersonaCharacter>(Agent);
    auto Move=C ? Cast<UPersonaMovement>(C->GetCharacterMovement()) : nullptr;
    if(!C || !Move || !C->IsAlive() || User.IsValid() || !Support.IsValid()) { ResumePathFollowing(Agent); return; }
    const FVector Target=FVector::DistSquared(Destination,High)<FVector::DistSquared(Destination,Low) ? High : Low;
    if(Move->BeginTraversal(Target,Support.Get())) { User=C; SetSmartLinkEnabled(false); }
    else
    {
        // A blocked landing must invalidate this route long enough for the AI
        // to select another edge instead of requesting the same link forever.
        BlockedUntil=GetWorld()->GetTimeSeconds()+2.f; SetSmartLinkEnabled(false);
        ResumePathFollowing(Agent); if(auto AI=Cast<APersonaAIController>(C->Controller)) AI->StopMovement();
    }
}
void APersonaClimbLink::Tick(float Delta)
{
    Super::Tick(Delta);
    if(!Support.IsValid() || !Support->GetComponentTransform().Equals(SupportTransform,1.f))
    {
        if(auto C=User.Get()) { CastChecked<UPersonaMovement>(C->GetCharacterMovement())->AbortTraversal(); ResumePathFollowing(C); }
        Destroy(); return;
    }
    if(auto C=User.Get(); C && (!C->Traversal.Active || !C->IsAlive())) { ResumePathFollowing(C); User.Reset(); }
    SetSmartLinkEnabled(!User.IsValid() && GetWorld()->GetTimeSeconds()>=BlockedUntil);
}
APersonaTraversalGraph::APersonaTraversalGraph()
{ PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickInterval=1.f; }
void APersonaTraversalGraph::Ensure(UWorld* World)
{
    if(!World || World->GetNetMode()==NM_Client) return;
    for(TActorIterator<APersonaTraversalGraph> It(World);It;++It) return;
    World->SpawnActor<APersonaTraversalGraph>();
}
void APersonaTraversalGraph::Tick(float Delta)
{ Super::Tick(Delta); Age+=Delta; if(Age>=2) { Age=0; Scan(); } }
void APersonaTraversalGraph::Scan()
{
    auto Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()); if(!Nav || UNavigationSystemV1::IsNavigationBeingBuilt(GetWorld())) return;
    FCollisionQueryParams Q(SCENE_QUERY_STAT(ClimbLinks),false);
    // Temporary occupants must not permanently remove a geometric transition.
    // BeginTraversal checks occupancy again when somebody actually uses it.
    for(TActorIterator<APersonaCharacter> Character(GetWorld());Character;++Character) Q.AddIgnoredActor(*Character);
    int32 Budget=24;
    for(TActorIterator<AActor> It(GetWorld());It && Budget>0;++It)
    {
        if(It->IsA<APersonaCharacter>() || It->IsA<APersonaClimbLink>()) continue;
        TArray<UStaticMeshComponent*> Meshes; It->GetComponents(Meshes);
        for(auto Mesh:Meshes)
        {
            if(!Mesh->IsCollisionEnabled() || Mesh->GetCollisionResponseToChannel(ECC_Pawn)!=ECR_Block || Mesh->GetComponentVelocity().Size()>2) continue;
            const FBox Bounds=Mesh->Bounds.GetBox(); const FVector Size=Bounds.GetSize();
            if(Size.Z<55 || FMath::Min(Size.X,Size.Y)<70 || FMath::Max(Size.X,Size.Y)>3000) continue;
            if(auto Previous=Scanned.Find(Mesh); Previous && Previous->Equals(Mesh->GetComponentTransform(),1.f)) continue;
            Scanned.Add(Mesh,Mesh->GetComponentTransform()); --Budget;
            for(int32 Side=0;Side<4;++Side)
            {
                const bool X=Side<2; const float Sign=Side%2 ? 1.f : -1.f;
                const FVector Normal=X ? FVector(Sign,0,0) : FVector(0,Sign,0);
                const float Length=X ? Size.Y : Size.X;
                const int32 Count=FMath::Max(1,FMath::FloorToInt((Length-70)/150));
                for(int32 I=0;I<Count;++I)
                {
                    FVector Edge=Bounds.GetCenter(); Edge.Z=Bounds.Max.Z;
                    if(X) { Edge.X=Sign>0 ? Bounds.Max.X : Bounds.Min.X; Edge.Y=Bounds.Min.Y+Length*(I+.5f)/Count; }
                    else { Edge.Y=Sign>0 ? Bounds.Max.Y : Bounds.Min.Y; Edge.X=Bounds.Min.X+Length*(I+.5f)/Count; }
                    FHitResult Top,Bottom;
                    const FVector Inside=Edge-Normal*40, Outside=Edge+Normal*55;
                    if(!GetWorld()->LineTraceSingleByChannel(Top,Inside+FVector(0,0,12),Inside-FVector(0,0,45),ECC_Visibility,Q) || Top.GetComponent()!=Mesh || Top.ImpactNormal.Z<.7f) continue;
                    if(!GetWorld()->LineTraceSingleByChannel(Bottom,Outside+FVector(0,0,10),Outside-FVector(0,0,242),ECC_Visibility,Q) || Bottom.ImpactNormal.Z<.7f) continue;
                    const float Height=Top.ImpactPoint.Z-Bottom.ImpactPoint.Z; if(Height<50 || Height>240) continue;
                    FNavLocation From,To;
                    if(!Nav->ProjectPointToNavigation(Bottom.ImpactPoint,From,FVector(45,45,35)) || !Nav->ProjectPointToNavigation(Top.ImpactPoint,To,FVector(40,40,35))) continue;
                    const auto Shape=FCollisionShape::MakeCapsule(32,88);
                    if(GetWorld()->OverlapBlockingTestByChannel(To.Location+FVector(0,0,91),FQuat::Identity,ECC_Pawn,Shape,Q)) continue;
                    auto Link=GetWorld()->SpawnActor<APersonaClimbLink>(From.Location,FRotator::ZeroRotator);
                    Link->Configure(From.Location,To.Location,Mesh);
                }
            }
        }
    }
}
