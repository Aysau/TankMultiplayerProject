#include "TankDamageTestTarget.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

ATankDamageTestTarget::ATankDamageTestTarget()
{
	bReplicates = true;
	SetReplicatingMovement(false);
	bAlwaysRelevant = true;
	UBoxComponent* Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Collision"));
	SetRootComponent(Box);
	Box->InitBoxExtent(FVector(50.0));
	Box->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Box->SetGenerateOverlapEvents(false);
	UStaticMeshComponent* Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Visual"));
	Mesh->SetupAttachment(Box);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Mesh->SetStaticMesh(Cube.Object);
	// Small fixture readout, rebuilt from replicated current state on each machine.
	ResultText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("ResultText"));
	ResultText->SetupAttachment(Box);
	ResultText->SetRelativeLocation(FVector(0.0, 0.0, 100.0));
	ResultText->SetRelativeRotation(FRotator(-90.0, 0.0, 0.0));
	ResultText->SetHorizontalAlignment(EHTA_Center);
	ResultText->SetWorldSize(24.f);
	ResultText->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void ATankDamageTestTarget::BeginPlay()
{
	Super::BeginPlay();
	OnRep_DamageResult();
}

void ATankDamageTestTarget::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATankDamageTestTarget, HitCount);
	DOREPLIFETIME(ATankDamageTestTarget, TotalDamage);
}

void ATankDamageTestTarget::OnRep_DamageResult()
{
	ResultText->SetText(FText::FromString(FString::Printf(TEXT("Hits: %d\nDamage: %.0f"), HitCount, TotalDamage)));
}

float ATankDamageTestTarget::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
	AController* EventInstigator, AActor* DamageCauser)
{
	if (!HasAuthority() || GetNetMode() == NM_Client || IsActorBeingDestroyed()
		|| !CanBeDamaged() || !FMath::IsFinite(DamageAmount) || DamageAmount <= 0.f
		|| HitCount == MAX_int32 || !FMath::IsFinite(TotalDamage + DamageAmount))
	{
		return 0.f;
	}
	++HitCount;
	TotalDamage += DamageAmount;
	OnRep_DamageResult(); // RepNotify is not automatically invoked on the server.
	ForceNetUpdate();
	UE_LOG(LogTemp, Display, TEXT("Damage test target %s: hits=%d total=%.0f"), *GetName(), HitCount, TotalDamage);
	return DamageAmount;
}
