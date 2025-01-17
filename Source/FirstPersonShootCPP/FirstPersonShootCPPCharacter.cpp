// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "FirstPersonShootCPPCharacter.h"
#include "FirstPersonShootCPPProjectile.h"
#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "GameFramework/InputSettings.h"
#include "Kismet/GameplayStatics.h"
#include "MotionControllerComponent.h"
#include "MathUtil.h"

//coreDS Unreal include
#include "coreDSEngine.h"
#include "coreDS_BPCoordinateConversion.h"

DEFINE_LOG_CATEGORY_STATIC(LogFPChar, Warning, All);

//////////////////////////////////////////////////////////////////////////
// AFirstPersonShootCPPCharacter

AFirstPersonShootCPPCharacter::AFirstPersonShootCPPCharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(55.f, 96.0f);

	// set our turn rates for input
	BaseTurnRate = 45.f;
	BaseLookUpRate = 45.f;

	// Create a CameraComponent	
	FirstPersonCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCameraComponent->SetupAttachment(GetCapsuleComponent());
	FirstPersonCameraComponent->SetRelativeLocation(FVector(-39.56f, 1.75f, 64.f)); // Position the camera
	FirstPersonCameraComponent->bUsePawnControlRotation = true;

	// Create a mesh component that will be used when being viewed from a '1st person' view (when controlling this pawn)
	Mesh1P = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("CharacterMesh1P"));
	//Mesh1P->SetOnlyOwnerSee(true);
	Mesh1P->SetupAttachment(FirstPersonCameraComponent);
	Mesh1P->bCastDynamicShadow = false;
	Mesh1P->CastShadow = false;
	Mesh1P->SetRelativeRotation(FRotator(1.9f, -19.19f, 5.2f));
	Mesh1P->SetRelativeLocation(FVector(-0.5f, -4.4f, -155.7f));

	// Create a gun mesh component
	FP_Gun = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FP_Gun"));
	//FP_Gun->SetOnlyOwnerSee(true);			// only the owning player will see this mesh
	FP_Gun->bCastDynamicShadow = false;
	FP_Gun->CastShadow = false;
	// FP_Gun->SetupAttachment(Mesh1P, TEXT("GripPoint"));
	FP_Gun->SetupAttachment(RootComponent);

	FP_MuzzleLocation = CreateDefaultSubobject<USceneComponent>(TEXT("MuzzleLocation"));
	FP_MuzzleLocation->SetupAttachment(FP_Gun);
	FP_MuzzleLocation->SetRelativeLocation(FVector(0.2f, 48.4f, -10.6f));

	// Default offset from the character location for projectiles to spawn
	GunOffset = FVector(100.0f, 0.0f, 10.0f);
		
	//coreDS Reduce the tick requency so we don't flood the network
	PrimaryActorTick.TickInterval = 1.0f;
}

void AFirstPersonShootCPPCharacter::BeginPlay()
{
	// Call the base class  
	Super::BeginPlay();

	Engine = GetGameInstance()->GetSubsystem<UcoreDSEngine>();

	//Attach gun mesh component to Skeleton, doing it here because the skeleton is not yet created in the constructor
	FP_Gun->AttachToComponent(Mesh1P, FAttachmentTransformRules(EAttachmentRule::SnapToTarget, true), TEXT("GripPoint"));

	Mesh1P->SetHiddenInGame(false, true);
}

//////////////////////////////////////////////////////////////////////////
// Input

void AFirstPersonShootCPPCharacter::SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent)
{
	// set up gameplay key bindings
	check(PlayerInputComponent);

	// Bind jump events
	PlayerInputComponent->BindAction("Jump", IE_Pressed, this, &ACharacter::Jump);
	PlayerInputComponent->BindAction("Jump", IE_Released, this, &ACharacter::StopJumping);

	// Bind fire event
	PlayerInputComponent->BindAction("Fire", IE_Pressed, this, &AFirstPersonShootCPPCharacter::OnFire);

	// Enable touchscreen input
	EnableTouchscreenMovement(PlayerInputComponent);

	// Bind movement events
	PlayerInputComponent->BindAxis("MoveForward", this, &AFirstPersonShootCPPCharacter::MoveForward);
	PlayerInputComponent->BindAxis("MoveRight", this, &AFirstPersonShootCPPCharacter::MoveRight);

	// We have 2 versions of the rotation bindings to handle different kinds of devices differently
	// "turn" handles devices that provide an absolute delta, such as a mouse.
	// "turnrate" is for devices that we choose to treat as a rate of change, such as an analog joystick
	PlayerInputComponent->BindAxis("Turn", this, &APawn::AddControllerYawInput);
	PlayerInputComponent->BindAxis("TurnRate", this, &AFirstPersonShootCPPCharacter::TurnAtRate);
	PlayerInputComponent->BindAxis("LookUp", this, &APawn::AddControllerPitchInput);
	PlayerInputComponent->BindAxis("LookUpRate", this, &AFirstPersonShootCPPCharacter::LookUpAtRate);
}

void AFirstPersonShootCPPCharacter::OnFire()
{
	// try and fire a projectile
	if (ProjectileClass != NULL)
	{
		UWorld* const World = GetWorld();
		if (World != NULL)
		{
			const FRotator SpawnRotation = GetControlRotation();
			// MuzzleOffset is in camera space, so transform it to world space before offsetting from the character location to find the final muzzle position
			const FVector SpawnLocation = ((FP_MuzzleLocation != nullptr) ? FP_MuzzleLocation->GetComponentLocation() : GetActorLocation()) + SpawnRotation.RotateVector(GunOffset);

			//Set Spawn Collision Handling Override
			FActorSpawnParameters ActorSpawnParams;
			ActorSpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;

			// spawn the projectile at the muzzle
			World->SpawnActor<AFirstPersonShootCPPProjectile>(ProjectileClass, SpawnLocation, SpawnRotation, ActorSpawnParams);
			
		}
	}

	// try and play the sound if specified
	if (FireSound != NULL)
	{
		UGameplayStatics::PlaySoundAtLocation(this, FireSound, GetActorLocation());
	}

	// try and play a firing animation if specified
	if (FireAnimation != NULL)
	{
		// Get the animation object for the arms mesh
		UAnimInstance* AnimInstance = Mesh1P->GetAnimInstance();
		if (AnimInstance != NULL)
		{
			AnimInstance->Montage_Play(FireAnimation, 1.f);
		}
	}

	//if the object was dynamically created, just return
	if (ActorHasTag("coreDSCreated"))
	{
		return;
	}

	//coreDS Unreal
	//Send a message on hit
	TArray< FKeyVariantPair > lValues;
	FVector ActorLocation;

	const FRotator SpawnRotation = GetControlRotation();
	// MuzzleOffset is in camera space, so transform it to world space before offsetting from the character location to find the final muzzle position
	ActorLocation  = ((FP_MuzzleLocation != nullptr) ? FP_MuzzleLocation->GetComponentLocation() : GetActorLocation()) + SpawnRotation.RotateVector(GunOffset);
	
	double x = 0, y = 0, z = 0;
	UcoreDSSettings* lSettings = const_cast<UcoreDSSettings*>(GetDefault<UcoreDSSettings>());
	UCoreDSCoordinateConversion::EnuToEcef(ActorLocation.X * 100, ActorLocation.Y * 100, ActorLocation.Z * 100,
		lSettings->ReferenceLatitude, lSettings->ReferenceLongitude, lSettings->ReferenceAltitude,
		x, y, z);

	lValues.Add(FKeyVariantPair("Location.x", x));
	lValues.Add(FKeyVariantPair("Location.y", y));
	lValues.Add(FKeyVariantPair("Location.z", z));

	Engine->sendMessage("ShotFired", lValues);
}

void AFirstPersonShootCPPCharacter::BeginTouch(const ETouchIndex::Type FingerIndex, const FVector Location)
{
	if (TouchItem.bIsPressed == true)
	{
		return;
	}
	if ((FingerIndex == TouchItem.FingerIndex) && (TouchItem.bMoved == false))
	{
		OnFire();
	}
	TouchItem.bIsPressed = true;
	TouchItem.FingerIndex = FingerIndex;
	TouchItem.Location = Location;
	TouchItem.bMoved = false;
}

void AFirstPersonShootCPPCharacter::EndTouch(const ETouchIndex::Type FingerIndex, const FVector Location)
{
	if (TouchItem.bIsPressed == false)
	{
		return;
	}
	TouchItem.bIsPressed = false;
}

//Commenting this section out to be consistent with FPS BP template.
//This allows the user to turn without using the right virtual joystick

//void AFirstPersonShootCPPCharacter::TouchUpdate(const ETouchIndex::Type FingerIndex, const FVector Location)
//{
//	if ((TouchItem.bIsPressed == true) && (TouchItem.FingerIndex == FingerIndex))
//	{
//		if (TouchItem.bIsPressed)
//		{
//			if (GetWorld() != nullptr)
//			{
//				UGameViewportClient* ViewportClient = GetWorld()->GetGameViewport();
//				if (ViewportClient != nullptr)
//				{
//					FVector MoveDelta = Location - TouchItem.Location;
//					FVector2D ScreenSize;
//					ViewportClient->GetViewportSize(ScreenSize);
//					FVector2D ScaledDelta = FVector2D(MoveDelta.X, MoveDelta.Y) / ScreenSize;
//					if (FMath::Abs(ScaledDelta.X) >= 4.0 / ScreenSize.X)
//					{
//						TouchItem.bMoved = true;
//						float Value = ScaledDelta.X * BaseTurnRate;
//						AddControllerYawInput(Value);
//					}
//					if (FMath::Abs(ScaledDelta.Y) >= 4.0 / ScreenSize.Y)
//					{
//						TouchItem.bMoved = true;
//						float Value = ScaledDelta.Y * BaseTurnRate;
//						AddControllerPitchInput(Value);
//					}
//					TouchItem.Location = Location;
//				}
//				TouchItem.Location = Location;
//			}
//		}
//	}
//}

void AFirstPersonShootCPPCharacter::MoveForward(float Value)
{
	if (Value != 0.0f)
	{
		// add movement in that direction
		AddMovementInput(GetActorForwardVector(), Value);
	}
}

void AFirstPersonShootCPPCharacter::MoveRight(float Value)
{
	if (Value != 0.0f)
	{
		// add movement in that direction
		AddMovementInput(GetActorRightVector(), Value);
	}
}

void AFirstPersonShootCPPCharacter::TurnAtRate(float Rate)
{
	// calculate delta for this frame from the rate information
	AddControllerYawInput(Rate * BaseTurnRate * GetWorld()->GetDeltaSeconds());
}

void AFirstPersonShootCPPCharacter::LookUpAtRate(float Rate)
{
	// calculate delta for this frame from the rate information
	AddControllerPitchInput(Rate * BaseLookUpRate * GetWorld()->GetDeltaSeconds());
}

bool AFirstPersonShootCPPCharacter::EnableTouchscreenMovement(class UInputComponent* PlayerInputComponent)
{
	if (FPlatformMisc::SupportsTouchInput() || GetDefault<UInputSettings>()->bUseMouseForTouch)
	{
		PlayerInputComponent->BindTouch(EInputEvent::IE_Pressed, this, &AFirstPersonShootCPPCharacter::BeginTouch);
		PlayerInputComponent->BindTouch(EInputEvent::IE_Released, this, &AFirstPersonShootCPPCharacter::EndTouch);

		//Commenting this out to be more consistent with FPS BP template.
		//PlayerInputComponent->BindTouch(EInputEvent::IE_Repeat, this, &AFirstPersonShootCPPCharacter::TouchUpdate);
		return true;
	}
	
	return false;
}

void AFirstPersonShootCPPCharacter::TickActor(float DeltaTime, enum ELevelTick TickType, FActorTickFunction& ThisTickFunction)
{
	Super::TickActor(DeltaTime, TickType, ThisTickFunction);

	//if the object was dynamically created, just return
	if (ActorHasTag("coreDSCreated"))
	{
		return;
	}

	//coreDS Unreal
	//Send a message on hit
	TArray< FKeyVariantPair > lValues;

	FVector ActorLocation = GetActorLocation();
	//FRotator ActorRotation = GetActorRotation();
	FRotator ActorRotation = FirstPersonCameraComponent->GetComponentRotation();

	//only send update if all fields are valid
	if (!ActorLocation.ContainsNaN() && !ActorRotation.ContainsNaN())
	{
		//UE_LOG(LogClass, Log, TEXT("coreDS: Sent actor position %f, %f, %f"), ActorLocation.X, ActorLocation.Y, ActorLocation.Z);
		
		double x = 0, y = 0, z = 0;
		double psi = 0, theta = 0, phi = 0;
		UcoreDSSettings* lSettings = const_cast<UcoreDSSettings*>(GetDefault<UcoreDSSettings>());

		UCoreDSCoordinateConversion::EnuToEcef(ActorLocation.X*100, ActorLocation.Y * 100, ActorLocation.Z * 100,
			lSettings->ReferenceLatitude, lSettings->ReferenceLongitude, lSettings->ReferenceAltitude,
			x, y, z);

		// use the coreDS functions to handle the rotation
		UCoreDSCoordinateConversion::HeadingPitchRollToEuler(
			lSettings->ReferenceLatitude * FMathd::DegToRad,
			lSettings->ReferenceLongitude * FMathd::DegToRad,
			ActorRotation.Yaw * FMathd::DegToRad, ActorRotation.Pitch * FMathd::DegToRad, ActorRotation.Roll * FMathd::DegToRad,
			psi, theta, phi);

		lValues.Add(FKeyVariantPair("Location.x", x));
		lValues.Add(FKeyVariantPair("Location.y", y));
		lValues.Add(FKeyVariantPair("Location.z", z));

		lValues.Add(FKeyVariantPair("Orientation.pitch", psi));
		lValues.Add(FKeyVariantPair("Orientation.yaw", phi));
		lValues.Add(FKeyVariantPair("Orientation.roll", theta));

		//The first argument is the object type, followed a unique identifier, then the values
		Engine->updateObject(GetFName().ToString(), "Gun", lValues);
	}
}

void AFirstPersonShootCPPCharacter::Destroyed()
{
	// If the object was not created by coreDS Unreal (ie, this is a locally created object)
	// Delete it
	if (!ActorHasTag("coreDSCreated"))
	{
		Engine->removeObject(TCHAR_TO_UTF8(*GetFName().ToString()));
	}
}