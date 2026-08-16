#include "EditorLinkModularWallActor.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"

AEditorLinkModularWallActor::AEditorLinkModularWallActor()
{
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	WallInstances = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("WallInstances"));
	WallInstances->SetupAttachment(SceneRoot);
	WallInstances->SetMobility(EComponentMobility::Static);
	WallInstances->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
}

void AEditorLinkModularWallActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	WallInstances->ClearInstances();
	WallInstances->SetStaticMesh(WallMesh);
	if (!WallMesh || PieceCount <= 0)
	{
		return;
	}

	const double MeshWidth = WallMesh->GetBoundingBox().GetSize().X;
	if (MeshWidth <= UE_SMALL_NUMBER)
	{
		return;
	}

	const double Spacing = MeshWidth + FMath::Max(0.0, Gap);
	const double StartX = bCenterWall ? -0.5 * static_cast<double>(PieceCount - 1) * Spacing : 0.0;
	for (int32 Index = 0; Index < PieceCount; ++Index)
	{
		const FVector Location(StartX + static_cast<double>(Index) * Spacing, 0.0, 0.0);
		WallInstances->AddInstance(FTransform(Location));
	}
}

