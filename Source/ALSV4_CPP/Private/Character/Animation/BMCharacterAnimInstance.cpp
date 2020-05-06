// Copyright (C) 2020, Doga Can Yanikoglu


#include "Character/Animation/BMCharacterAnimInstance.h"
#include "Character/BMBaseCharacter.h"
#include "Curves/CurveVector.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

void UBMCharacterAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	Character = Cast<ABMBaseCharacter>(TryGetPawnOwner());
}

void UBMCharacterAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if (!Character || DeltaSeconds == 0.0f)
	{
		// Fix character looking right on editor
		CharacterInformation.RotationMode = EBMRotationMode::VelocityDirection;

		// Don't run in editor
		return;
	}

	UpdateCharacterInfo();
	UpdateAimingValues(DeltaSeconds);
	UpdateLayerValues();
	UpdateFootIK(DeltaSeconds);

	if (CharacterInformation.MovementState == EBMMovementState::Grounded)
	{
		// Check If Moving Or Not & Enable Movement Animations if IsMoving and HasMovementInput, or if the Speed is greater than 150.
		const bool prevShouldMove = Grounded.bShouldMove;
		Grounded.bShouldMove = ShouldMoveCheck();

		if (prevShouldMove == false && Grounded.bShouldMove)
		{
			// Do When Starting To Move
			TurnInPlaceValues.ElapsedDelayTime = 0.0f;
			Grounded.bRotateL = false;
			Grounded.bRotateR = false;
		}

		if (Grounded.bShouldMove)
		{
			// Do While Moving
			UpdateMovementValues(DeltaSeconds);
			UpdateRotationValues();
		}
		else
		{
			// Do While Not Moving
			if (CanRotateInPlace())
			{
				RotateInPlaceCheck();
			}
			else
			{
				Grounded.bRotateL = false;
				Grounded.bRotateR = false;
			}
			if (CanTurnInPlace())
			{
				TurnInPlaceCheck(DeltaSeconds);
			}
			else
			{
				TurnInPlaceValues.ElapsedDelayTime = 0.0f;
			}
			if (CanDynamicTransition())
			{
				DynamicTransitionCheck();
			}
		}
	}
	else if (CharacterInformation.MovementState == EBMMovementState::InAir)
	{
		// Do While InAir
		UpdateInAirValues(DeltaSeconds);
	}
	else if (CharacterInformation.MovementState == EBMMovementState::Ragdoll)
	{
		// Do While Ragdolling
		UpdateRagdollValues();
	}
}

void UBMCharacterAnimInstance::PlayTransition(const FBMDynamicMontageParams& Parameters)
{
	PlaySlotAnimationAsDynamicMontage(Parameters.Animation, FName(TEXT("Grounded Slot")),
	                                  Parameters.BlendInTime, Parameters.BlendOutTime, Parameters.PlayRate, 1,
	                                  0.0f, Parameters.StartTime);
}

void UBMCharacterAnimInstance::PlayTransitionChecked(const FBMDynamicMontageParams& Parameters)
{
	if (CharacterInformation.Stance == EBMStance::Standing && !Grounded.bShouldMove)
	{
		PlayTransition(Parameters);
	}
}

void UBMCharacterAnimInstance::PlayDynamicTransition(float ReTriggerDelay, FBMDynamicMontageParams Parameters)
{
	if (bCanPlayDynamicTransition)
	{
		bCanPlayDynamicTransition = false;
		// Play Dynamic Additive Transition Animation
		PlaySlotAnimationAsDynamicMontage(Parameters.Animation, FName(TEXT("Grounded Slot")),
		                                  Parameters.BlendInTime, Parameters.BlendOutTime, Parameters.PlayRate, 1,
		                                  0.0f, Parameters.StartTime);

		Character->GetWorldTimerManager().SetTimer(PlayDynamicTransitionTimer, this,
		                                           &UBMCharacterAnimInstance::PlayDynamicTransitionDelay,
		                                           ReTriggerDelay, false);
	}
}

bool UBMCharacterAnimInstance::ShouldMoveCheck()
{
	return (CharacterInformation.bIsMoving && CharacterInformation.bHasMovementInput) ||
		CharacterInformation.Speed > 150.0f;
}

bool UBMCharacterAnimInstance::CanRotateInPlace()
{
	return CharacterInformation.RotationMode == EBMRotationMode::Aiming ||
		CharacterInformation.ViewMode == EBMViewMode::FirstPerson;
}

bool UBMCharacterAnimInstance::CanTurnInPlace()
{
	return CharacterInformation.RotationMode == EBMRotationMode::LookingDirection &&
		CharacterInformation.ViewMode == EBMViewMode::ThirdPerson &&
		GetCurveValue(FName(TEXT("Enable_Transition"))) > 0.99f;
}

bool UBMCharacterAnimInstance::CanDynamicTransition()
{
	return GetCurveValue(FName(TEXT("Enable_Transition"))) == 1.0f;
}

void UBMCharacterAnimInstance::PlayDynamicTransitionDelay()
{
	bCanPlayDynamicTransition = true;
}

void UBMCharacterAnimInstance::OnJumpedDelay()
{
	InAir.bJumped = false;
}

void UBMCharacterAnimInstance::OnPivotDelay()
{
	Grounded.bPivot = false;
}

void UBMCharacterAnimInstance::UpdateCharacterInfo()
{
	// TODO: Maybe access those directly from character?
	CharacterInformation.Velocity = Character->GetVelocity();
	CharacterInformation.Acceleration = Character->GetCurrentAcceleration();
	CharacterInformation.MovementInput = Character->GetMovementInput();
	CharacterInformation.bIsMoving = Character->IsMoving();
	CharacterInformation.bHasMovementInput = Character->HasMovementInput();
	CharacterInformation.Speed = Character->GetSpeed();
	CharacterInformation.MovementInputAmount = Character->GetMovementInputAmount();
	CharacterInformation.AimingRotation = Character->GetAimingRotation();
	CharacterInformation.AimYawRate = Character->GetAimYawRate();
	CharacterInformation.MovementState = Character->GetMovementState();
	CharacterInformation.PrevMovementState = Character->GetPrevMovementState();
	CharacterInformation.MovementAction = Character->GetMovementAction();
	CharacterInformation.RotationMode = Character->GetRotationMode();
	CharacterInformation.Gait = Character->GetGait();
	CharacterInformation.Stance = Character->GetStance();
	CharacterInformation.ViewMode = Character->GetViewMode();
	CharacterInformation.OverlayState = Character->GetOverlayState();
}

void UBMCharacterAnimInstance::UpdateAimingValues(float DeltaSeconds)
{
	// Interp the Aiming Rotation value to achieve smooth aiming rotation changes.
	// Interpolating the rotation before calculating the angle ensures the value is not affected by changes
	// in actor rotation, allowing slow aiming rotation changes with fast actor rotation changes.

	AimingValues.SmoothedAimingRotation = FMath::RInterpTo(AimingValues.SmoothedAimingRotation,
	                                                       CharacterInformation.AimingRotation, DeltaSeconds,
	                                                       Config.SmoothedAimingRotationInterpSpeed);

	// Calculate the Aiming angle and Smoothed Aiming Angle by getting
	// the delta between the aiming rotation and the actor rotation.
	FRotator Delta = CharacterInformation.AimingRotation - Character->GetActorRotation();
	Delta.Normalize();
	AimingValues.AimingAngle.X = Delta.Yaw;
	AimingValues.AimingAngle.Y = Delta.Pitch;

	Delta = AimingValues.SmoothedAimingRotation - Character->GetActorRotation();
	Delta.Normalize();
	AimingValues.SmoothedAimingAngle.X = Delta.Yaw;
	AimingValues.SmoothedAimingAngle.Y = Delta.Pitch;

	if (CharacterInformation.RotationMode != EBMRotationMode::VelocityDirection)
	{
		// Clamp the Aiming Pitch Angle to a range of 1 to 0 for use in the vertical aim sweeps.
		AimingValues.AimSweepTime = FMath::GetMappedRangeValueClamped(FVector2D(-90.0f, 90.0f),
		                                                              FVector2D(1.0f, 0.0f),
		                                                              AimingValues.AimingAngle.Y);

		// Use the Aiming Yaw Angle divided by the number of spine+pelvis bones to get the amount of spine rotation
		// needed to remain facing the camera direction.
		AimingValues.SpineRotation.Roll = 0.0f;
		AimingValues.SpineRotation.Pitch = 0.0f;
		AimingValues.SpineRotation.Yaw = AimingValues.AimingAngle.X / 4.0f;
	}
	else if (CharacterInformation.bHasMovementInput)
	{
		// Get the delta between the Movement Input rotation and Actor rotation and map it to a range of 0-1.
		// This value is used in the aim offset behavior to make the character look toward the Movement Input.
		Delta = CharacterInformation.MovementInput.ToOrientationRotator() - Character->GetActorRotation();
		Delta.Normalize();
		const float InterpTarget = FMath::GetMappedRangeValueClamped(FVector2D(-180.0f, 180.0f),
		                                                             FVector2D(0.0f, 1.0f), Delta.Yaw);

		AimingValues.InputYawOffsetTime = FMath::FInterpTo(AimingValues.InputYawOffsetTime, InterpTarget,
		                                                   DeltaSeconds, Config.InputYawOffsetInterpSpeed);
	}

	// Separate the Aiming Yaw Angle into 3 separate Yaw Times. These 3 values are used in the Aim Offset behavior
	// to improve the blending of the aim offset when rotating completely around the character.
	// This allows you to keep the aiming responsive but still smoothly blend from left to right or right to left.
	AimingValues.LeftYawTime = FMath::GetMappedRangeValueClamped(FVector2D(0.0f, 180.0f),
	                                                             FVector2D(0.5f, 0.0f),
	                                                             FMath::Abs(AimingValues.SmoothedAimingAngle.X));
	AimingValues.RightYawTime = FMath::GetMappedRangeValueClamped(FVector2D(0.0f, 180.0f),
	                                                              FVector2D(0.5f, 1.0f),
	                                                              FMath::Abs(AimingValues.SmoothedAimingAngle.X));
	AimingValues.ForwardYawTime = FMath::GetMappedRangeValueClamped(FVector2D(-180.0f, 180.0f),
	                                                                FVector2D(0.0f, 1.0f),
	                                                                AimingValues.SmoothedAimingAngle.X);
}

void UBMCharacterAnimInstance::UpdateLayerValues()
{
	// Get the Aim Offset weight by getting the opposite of the Aim Offset Mask.
	LayerBlendingValues.EnableAimOffset = FMath::Lerp(1.0f, 0.0f, GetCurveValue(FName(TEXT("Mask_AimOffset"))));
	// Set the Base Pose weights
	LayerBlendingValues.BasePose_N = GetCurveValue(FName(TEXT("BasePose_N")));
	LayerBlendingValues.BasePose_CLF = GetCurveValue(FName(TEXT("BasePose_CLF")));
	// Set the Additive amount weights for each body part
	LayerBlendingValues.Spine_Add = GetCurveValue(FName(TEXT("Layering_Spine_Add")));
	LayerBlendingValues.Head_Add = GetCurveValue(FName(TEXT("Layering_Head_Add")));
	LayerBlendingValues.Arm_L_Add = GetCurveValue(FName(TEXT("Layering_Arm_L_Add")));
	LayerBlendingValues.Arm_R_Add = GetCurveValue(FName(TEXT("Layering_Arm_R_Add")));
	// Set the Hand Override weights
	LayerBlendingValues.Hand_R = GetCurveValue(FName(TEXT("Layering_Hand_R")));
	LayerBlendingValues.Hand_L = GetCurveValue(FName(TEXT("Layering_Hand_L")));
	// Blend and set the Hand IK weights to ensure they only are weighted if allowed by the Arm layers.
	LayerBlendingValues.EnableHandIK_L = FMath::Lerp(0.0f, GetCurveValue(FName(TEXT("Enable_HandIK_L"))),
	                                                 GetCurveValue(FName(TEXT("Layering_Arm_L"))));
	LayerBlendingValues.EnableHandIK_R = FMath::Lerp(0.0f, GetCurveValue(FName(TEXT("Enable_HandIK_R"))),
	                                                 GetCurveValue(FName(TEXT("Layering_Arm_R"))));
	// Set whether the arms should blend in mesh space or local space.
	// The Mesh space weight will always be 1 unless the Local Space (LS) curve is fully weighted.
	LayerBlendingValues.Arm_L_LS = GetCurveValue(FName(TEXT("Layering_Arm_L_LS")));
	LayerBlendingValues.Arm_L_MS = static_cast<float>(1 - FMath::FloorToInt(LayerBlendingValues.Arm_L_LS));
	LayerBlendingValues.Arm_R_LS = GetCurveValue(FName(TEXT("Layering_Arm_R_LS")));
	LayerBlendingValues.Arm_R_MS = static_cast<float>(1 - FMath::FloorToInt(LayerBlendingValues.Arm_R_LS));
}

void UBMCharacterAnimInstance::UpdateFootIK(float DeltaSeconds)
{
	// Update Foot Locking values.
	SetFootLocking(DeltaSeconds, FName(TEXT("Enable_FootIK_L")), FName(TEXT("FootLock_L")),
	               FName(TEXT("ik_foot_l")), FootIKValues.FootLock_L_Alpha,
	               FootIKValues.FootLock_L_Location, FootIKValues.FootLock_L_Rotation);
	SetFootLocking(DeltaSeconds, FName(TEXT("Enable_FootIK_R")), FName(TEXT("FootLock_R")),
	               FName(TEXT("ik_foot_r")), FootIKValues.FootLock_R_Alpha,
	               FootIKValues.FootLock_R_Location, FootIKValues.FootLock_R_Rotation);

	if (CharacterInformation.MovementState == EBMMovementState::InAir)
	{
		// Reset IK Offsets if In Air
		SetPelvisIKOffset(DeltaSeconds, FVector::ZeroVector, FVector::ZeroVector);
		ResetIKOffsets(DeltaSeconds);
	}
	else
	{
		// Update all Foot Lock and Foot Offset values when not In Air
		FVector FootOffsetLTarget;
		FVector FootOffsetRTarget;
		SetFootOffsets(DeltaSeconds, FName(TEXT("Enable_FootIK_L")), FName(TEXT("ik_foot_l")), FName(TEXT("Root")), FootOffsetLTarget,
		               FootIKValues.FootOffset_L_Location, FootIKValues.FootOffset_L_Rotation);
		SetFootOffsets(DeltaSeconds, FName(TEXT("Enable_FootIK_R")), FName(TEXT("ik_foot_r")), FName(TEXT("Root")), FootOffsetRTarget,
		               FootIKValues.FootOffset_R_Location, FootIKValues.FootOffset_R_Rotation);
		SetPelvisIKOffset(DeltaSeconds, FootOffsetLTarget, FootOffsetRTarget);
	}
}

void UBMCharacterAnimInstance::SetFootLocking(float DeltaSeconds, FName EnableFootIKCurve, FName FootLockCurve,
                                              FName IKFootBone,
                                              float& CurFootLockAlpha, FVector& CurFootLockLoc,
                                              FRotator& CurFootLockRot)
{
	if (GetCurveValue(EnableFootIKCurve) <= 0.0f)
	{
		return;
	}

	// Step 1: Set Local FootLock Curve value
	const float FootLockCurveVal = GetCurveValue(FootLockCurve);

	// Step 2: Only update the FootLock Alpha if the new value is less than the current, or it equals 1. This makes it
	// so that the foot can only blend out of the locked position or lock to a new position, and never blend in.
	if (FootLockCurveVal > 0.99f || FootLockCurveVal < CurFootLockAlpha)
	{
		CurFootLockAlpha = FootLockCurveVal;
	}

	// Step 3: If the Foot Lock curve equals 1, save the new lock location and rotation in component space.
	if (CurFootLockAlpha >= 0.99f)
	{
		const FTransform& OwnerTransform =
			GetOwningComponent()->GetSocketTransform(IKFootBone, ERelativeTransformSpace::RTS_Component);
		CurFootLockLoc = OwnerTransform.GetLocation();
		CurFootLockRot = OwnerTransform.Rotator();
	}

	// Step 4: If the Foot Lock Alpha has a weight,
	// update the Foot Lock offsets to keep the foot planted in place while the capsule moves.
	if (CurFootLockAlpha > 0.0f)
	{
		SetFootLockOffsets(DeltaSeconds, CurFootLockLoc, CurFootLockRot);
	}
}

void UBMCharacterAnimInstance::SetFootLockOffsets(float DeltaSeconds, FVector& LocalLoc, FRotator& LocalRot)
{
	FRotator RotationDifference = FRotator::ZeroRotator;
	// Use the delta between the current and last updated rotation to find how much the foot should be rotated
	// to remain planted on the ground.
	if (Character->GetCharacterMovement()->IsMovingOnGround())
	{
		RotationDifference = Character->GetActorRotation() - Character->GetCharacterMovement()->GetLastUpdateRotation();
		RotationDifference.Normalize();
	}

	// Get the distance traveled between frames relative to the mesh rotation
	// to find how much the foot should be offset to remain planted on the ground.
	const FVector& LocationDifference = GetOwningComponent()->GetComponentRotation().UnrotateVector(
		CharacterInformation.Velocity * DeltaSeconds);

	// Subtract the location difference from the current local location and rotate
	// it by the rotation difference to keep the foot planted in component space.
	LocalLoc = (LocalLoc - LocationDifference).RotateAngleAxis(RotationDifference.Yaw, FVector::DownVector);

	// Subtract the Rotation Difference from the current Local Rotation to get the new local rotation.
	FRotator Delta = LocalRot - RotationDifference;
	Delta.Normalize();
	LocalRot = Delta;
}

void UBMCharacterAnimInstance::SetPelvisIKOffset(float DeltaSeconds, FVector FootOffsetLTarget,
                                                 FVector FootOffsetRTarget)
{
	// Calculate the Pelvis Alpha by finding the average Foot IK weight. If the alpha is 0, clear the offset.
	FootIKValues.PelvisAlpha =
		(GetCurveValue(FName(TEXT("Enable_FootIK_L"))) + GetCurveValue(FName(TEXT("Enable_FootIK_R")))) / 2.0f;

	if (FootIKValues.PelvisAlpha > 0.0f)
	{
		// Step 1: Set the new Pelvis Target to be the lowest Foot Offset
		const FVector PelvisTarget = FootOffsetLTarget.Z < FootOffsetRTarget.Z ? FootOffsetLTarget : FootOffsetRTarget;

		// Step 2: Interp the Current Pelvis Offset to the new target value.
		//Interpolate at different speeds based on whether the new target is above or below the current one.
		const float InterpSpeed = PelvisTarget.Z > FootIKValues.PelvisOffset.Z ? 10.0f : 15.0f;
		FootIKValues.PelvisOffset =
			FMath::VInterpTo(FootIKValues.PelvisOffset, PelvisTarget, DeltaSeconds, InterpSpeed);
	}
	else
	{
		FootIKValues.PelvisOffset = FVector::ZeroVector;
	}
}

void UBMCharacterAnimInstance::ResetIKOffsets(float DeltaSeconds)
{
	// Interp Foot IK offsets back to 0
	FootIKValues.FootOffset_L_Location = FMath::VInterpTo(FootIKValues.FootOffset_L_Location,
	                                                      FVector::ZeroVector, DeltaSeconds, 15.0f);
	FootIKValues.FootOffset_R_Location = FMath::VInterpTo(FootIKValues.FootOffset_R_Location,
	                                                      FVector::ZeroVector, DeltaSeconds, 15.0f);
	FootIKValues.FootOffset_L_Rotation = FMath::RInterpTo(FootIKValues.FootOffset_L_Rotation,
	                                                      FRotator::ZeroRotator, DeltaSeconds, 15.0f);
	FootIKValues.FootOffset_R_Rotation = FMath::RInterpTo(FootIKValues.FootOffset_R_Rotation,
	                                                      FRotator::ZeroRotator, DeltaSeconds, 15.0f);
}

void UBMCharacterAnimInstance::SetFootOffsets(float DeltaSeconds, FName EnableFootIKCurve, FName IKFootBone,
                                              FName RootBone, FVector& CurLocationTarget, FVector& CurLocationOffset,
                                              FRotator& CurRotationOffset)
{
	// Only update Foot IK offset values if the Foot IK curve has a weight. If it equals 0, clear the offset values.
	if (GetCurveValue(EnableFootIKCurve) <= 0)
	{
		CurLocationOffset = FVector::ZeroVector;
		CurRotationOffset = FRotator::ZeroRotator;
		return;
	}

	// Step 1: Trace downward from the foot location to find the geometry.
	// If the surface is walkable, save the Impact Location and Normal.
	USkeletalMeshComponent* OwnerComp = GetOwningComponent();
	FVector IKFootFloorLoc = OwnerComp->GetSocketLocation(IKFootBone);
	IKFootFloorLoc.Z = OwnerComp->GetSocketLocation(RootBone).Z;

	UWorld* World = GetWorld();
	check(World);

	FCollisionQueryParams Params;
	Params.AddIgnoredActor(Character);

	FHitResult HitResult;
	World->LineTraceSingleByChannel(HitResult,
	                                IKFootFloorLoc + FVector(0.0, 0.0, Config.IK_TraceDistanceAboveFoot),
	                                IKFootFloorLoc - FVector(0.0, 0.0, Config.IK_TraceDistanceBelowFoot),
	                                ECollisionChannel::ECC_Visibility, Params);

	FRotator TargetRotOffset = FRotator::ZeroRotator;
	if (Character->GetCharacterMovement()->IsWalkable(HitResult))
	{
		FVector ImpactPoint = HitResult.ImpactPoint;
		FVector ImpactNormal = HitResult.ImpactNormal;

		// Step 1.1: Find the difference in location from the Impact point and the expected (flat) floor location.
		// These values are offset by the nomrmal multiplied by the
		// foot height to get better behavior on angled surfaces.
		CurLocationTarget = (ImpactPoint + ImpactNormal * Config.FootHeight) -
			(IKFootFloorLoc + FVector::UpVector * Config.FootHeight);

		// Step 1.2: Calculate the Rotation offset by getting the Atan2 of the Impact Normal.
		TargetRotOffset.Pitch = -FMath::Atan2(ImpactNormal.X, ImpactNormal.Z);
		TargetRotOffset.Yaw = 0.0f;
		TargetRotOffset.Roll = FMath::Atan2(ImpactNormal.Y, ImpactNormal.Z);
	}

	// Step 2: Interp the Current Location Offset to the new target value.
	// Interpolate at different speeds based on whether the new target is above or below the current one.
	const float InterpSpeed = CurLocationOffset.Z > CurLocationTarget.Z ? 30.f : 15.0f;
	CurLocationOffset = FMath::VInterpTo(CurLocationOffset, CurLocationTarget, DeltaSeconds, InterpSpeed);

	// Step 3: Interp the Current Rotation Offset to the new target value.
	CurRotationOffset = FMath::RInterpTo(CurRotationOffset, TargetRotOffset, DeltaSeconds, 30.0f);
}

void UBMCharacterAnimInstance::RotateInPlaceCheck()
{
	// Step 1: Check if the character should rotate left or right by checking if the Aiming Angle exceeds the threshold.
	Grounded.bRotateL = AimingValues.AimingAngle.X < RotateInPlace.RotateMinThreshold;
	Grounded.bRotateR = AimingValues.AimingAngle.X > RotateInPlace.RotateMaxThreshold;

	// Step 2: If the character should be rotating, set the Rotate Rate to scale with the Aim Yaw Rate.
	// This makes the character rotate faster when moving the camera faster.
	if (Grounded.bRotateL || Grounded.bRotateR)
	{
		Grounded.RotateRate = FMath::GetMappedRangeValueClamped(
			FVector2D(RotateInPlace.AimYawRateMinRange, RotateInPlace.AimYawRateMaxRange),
			FVector2D(RotateInPlace.MinPlayRate, RotateInPlace.MaxPlayRate), CharacterInformation.AimYawRate);
	}
}

void UBMCharacterAnimInstance::TurnInPlaceCheck(float DeltaSeconds)
{
	// Step 1: Check if Aiming angle is outside of the Turn Check Min Angle, and if the Aim Yaw Rate is below the Aim Yaw Rate Limit.
	// If so, begin counting the Elapsed Delay Time. If not, reset the Elapsed Delay Time.
	// This ensures the conditions remain true for a sustained peroid of time before turning in place.
	if (FMath::Abs(AimingValues.AimingAngle.X) <= TurnInPlaceValues.TurnCheckMinAngle ||
		CharacterInformation.AimYawRate >= TurnInPlaceValues.AimYawRateLimit)
	{
		TurnInPlaceValues.ElapsedDelayTime = 0.0f;
		return;
	}

	TurnInPlaceValues.ElapsedDelayTime += DeltaSeconds;
	const float ClampedAimAngle = FMath::GetMappedRangeValueClamped(
		FVector2D(TurnInPlaceValues.TurnCheckMinAngle, 180.0f),
		FVector2D(TurnInPlaceValues.MinAngleDelay, TurnInPlaceValues.MaxAngleDelay),
		AimingValues.AimingAngle.X);

	// Step 2: Check if the Elapsed Delay time exceeds the set delay (mapped to the turn angle range). If so, trigger a Turn In Place.
	if (TurnInPlaceValues.ElapsedDelayTime > ClampedAimAngle)
	{
		FRotator TurnInPlaceYawRot = CharacterInformation.AimingRotation;
		TurnInPlaceYawRot.Roll = 0.0f;
		TurnInPlaceYawRot.Pitch = 0.0f;
		TurnInPlace(TurnInPlaceYawRot, 1.0f, 0.0f, false);
	}
}

void UBMCharacterAnimInstance::DynamicTransitionCheck()
{
	// Check each foot to see if the location difference between the IK_Foot bone and its desired / target location
	// (determined via a virtual bone) exceeds a threshold. If it does, play an additive transition animation on that foot.
	// The currently set transition plays the second half of a 2 foot transition animation, so that only a single foot moves.
	// Because only the IK_Foot bone can be locked, the separate virtual bone allows the system to know its desired location when locked.
	FTransform SocketTransformA = GetOwningComponent()->GetSocketTransform(FName(TEXT("ik_foot_l")), RTS_Component);
	FTransform SocketTransformB = GetOwningComponent()->GetSocketTransform(FName(TEXT("VB foot_target_l")), RTS_Component);
	float Distance = (SocketTransformB.GetLocation() - SocketTransformA.GetLocation()).Size();
	if (Distance > 12.0f)
	{
		FBMDynamicMontageParams Params;
		Params.Animation = TransitionAnim_L;
		Params.BlendInTime = 0.2f;
		Params.BlendOutTime = 0.2f;
		Params.PlayRate = 1.5f;
		Params.StartTime = 0.8f;
		PlayDynamicTransition(0.1f, Params);
	}

	SocketTransformA = GetOwningComponent()->GetSocketTransform(FName(TEXT("ik_foot_r")), RTS_Component);
	SocketTransformB = GetOwningComponent()->GetSocketTransform(FName(TEXT("VB foot_target_r")), RTS_Component);
	Distance = (SocketTransformB.GetLocation() - SocketTransformA.GetLocation()).Size();
	if (Distance > 12.0f)
	{
		FBMDynamicMontageParams Params;
		Params.Animation = TransitionAnim_R;
		Params.BlendInTime = 0.2f;
		Params.BlendOutTime = 0.2f;
		Params.PlayRate = 1.5f;
		Params.StartTime = 0.8f;
		PlayDynamicTransition(0.1f, Params);
	}
}

void UBMCharacterAnimInstance::UpdateMovementValues(float DeltaSeconds)
{
	// Interp and set the Velocity Blend.
	const FBMVelocityBlend& TargetBlend = CalculateVelocityBlend();
	Grounded.VelocityBlend.F =
		FMath::FInterpTo(Grounded.VelocityBlend.F, TargetBlend.F, DeltaSeconds, Config.VelocityBlendInterpSpeed);
	Grounded.VelocityBlend.B =
		FMath::FInterpTo(Grounded.VelocityBlend.B, TargetBlend.B, DeltaSeconds, Config.VelocityBlendInterpSpeed);
	Grounded.VelocityBlend.L =
		FMath::FInterpTo(Grounded.VelocityBlend.L, TargetBlend.L, DeltaSeconds, Config.VelocityBlendInterpSpeed);
	Grounded.VelocityBlend.R =
		FMath::FInterpTo(Grounded.VelocityBlend.R, TargetBlend.R, DeltaSeconds, Config.VelocityBlendInterpSpeed);

	// Set the Diagnal Scale Amount.
	Grounded.DiagonalScaleAmount = CalculateDiagonalScaleAmount();

	// Set the Relative Acceleration Amount and Interp the Lean Amount.
	Grounded.RelativeAccelerationAmount = CalculateRelativeAccelerationAmount();
	Grounded.LeanAmount.LR = FMath::FInterpTo(Grounded.LeanAmount.LR, Grounded.RelativeAccelerationAmount.Y,
	                                          DeltaSeconds, Config.GroundedLeanInterpSpeed);
	Grounded.LeanAmount.FB = FMath::FInterpTo(Grounded.LeanAmount.FB, Grounded.RelativeAccelerationAmount.X,
	                                          DeltaSeconds, Config.GroundedLeanInterpSpeed);

	// Set the Walk Run Blend
	Grounded.WalkRunBlend = CalculateWalkRunBlend();

	// Set the Stride Blend
	Grounded.StrideBlend = CalculateStrideBlend();

	// Set the Standing and Crouching Play Rates
	Grounded.StandingPlayRate = CalculateStandingPlayRate();
	Grounded.CrouchingPlayRate = CalculateCrouchingPlayRate();
}

void UBMCharacterAnimInstance::UpdateRotationValues()
{
	// Set the Movement Direction
	Grounded.MovementDirection = CalculateMovementDirection();

	// Set the Yaw Offsets. These values influence the "YawOffset" curve in the animgraph and are used to offset
	// the characters rotation for more natural movement. The curves allow for fine control over how the offset
	// behaves for each movement direction.
	FRotator Delta = CharacterInformation.Velocity.ToOrientationRotator() - Character->GetControlRotation();
	Delta.Normalize();
	const FVector& FBOffset = YawOffset_FB->GetVectorValue(Delta.Yaw);
	Grounded.FYaw = FBOffset.X;
	Grounded.BYaw = FBOffset.Y;
	const FVector& LROffset = YawOffset_LR->GetVectorValue(Delta.Yaw);
	Grounded.LYaw = LROffset.X;
	Grounded.RYaw = LROffset.Y;
}

void UBMCharacterAnimInstance::UpdateInAirValues(float DeltaSeconds)
{
	// Update the fall speed. Setting this value only while in the air allows you to use it within the AnimGraph for the landing strength.
	// If not, the Z velocity would return to 0 on landing.
	InAir.FallSpeed = CharacterInformation.Velocity.Z;

	// Set the Land Prediction weight.
	InAir.LandPrediction = CalculateLandPrediction();

	// Interp and set the In Air Lean Amount
	const FBMLeanAmount& InAirLeanAmount = CalculateAirLeanAmount();
	Grounded.LeanAmount.LR = FMath::FInterpTo(Grounded.LeanAmount.LR, InAirLeanAmount.LR,
	                                          DeltaSeconds, Config.GroundedLeanInterpSpeed);
	Grounded.LeanAmount.FB = FMath::FInterpTo(Grounded.LeanAmount.FB, InAirLeanAmount.FB,
	                                          DeltaSeconds, Config.GroundedLeanInterpSpeed);
}

void UBMCharacterAnimInstance::UpdateRagdollValues()
{
	// Scale the Flail Rate by the velocity length. The faster the ragdoll moves, the faster the character will flail.
	const float VelocityLength = GetOwningComponent()->GetPhysicsLinearVelocity(FName(TEXT("root"))).Size();
	FlailRate = FMath::GetMappedRangeValueClamped(FVector2D(0.0f, 1000.0f), FVector2D(0.0f, 1.0f), VelocityLength);
}

float UBMCharacterAnimInstance::GetAnimCurveClamped(const FName& Name, float Bias, float ClampMin, float ClampMax)
{
	return FMath::Clamp(GetCurveValue(Name) + Bias, ClampMin, ClampMax);
}

FBMVelocityBlend UBMCharacterAnimInstance::CalculateVelocityBlend()
{
	// Calculate the Velocity Blend. This value represents the velocity amount of the actor in each direction (normalized so that
	// diagonals equal .5 for each direction), and is used in a BlendMulti node to produce better
	// directional blending than a standard blendspace.
	const FVector LocRelativeVelocityDir =
		Character->GetActorRotation().UnrotateVector(CharacterInformation.Velocity.GetSafeNormal(0.1f));
	const float Sum = FMath::Abs(LocRelativeVelocityDir.X) + FMath::Abs(LocRelativeVelocityDir.Y) +
		FMath::Abs(LocRelativeVelocityDir.Z);
	const FVector RelativeDir = LocRelativeVelocityDir / Sum;
	FBMVelocityBlend Result;
	Result.F = FMath::Clamp(RelativeDir.X, 0.0f, 1.0f);
	Result.B = FMath::Abs(FMath::Clamp(RelativeDir.X, -1.0f, 0.0f));
	Result.L = FMath::Abs(FMath::Clamp(RelativeDir.Y, -1.0f, 0.0f));
	Result.R = FMath::Clamp(RelativeDir.Y, 0.0f, 1.0f);
	return Result;
}

FVector UBMCharacterAnimInstance::CalculateRelativeAccelerationAmount()
{
	// Calculate the Relative Acceleration Amount. This value represents the current amount of acceleration / deceleration
	// relative to the actor rotation. It is normalized to a range of -1 to 1 so that -1 equals the Max Braking Deceleration,
	// and 1 equals the Max Acceleration of the Character Movement Component.
	if (FVector::DotProduct(CharacterInformation.Acceleration, CharacterInformation.Velocity) > 0.0f)
	{
		const float MaxAcc = Character->GetCharacterMovement()->GetMaxAcceleration();
		return Character->GetActorRotation().UnrotateVector(CharacterInformation.Acceleration.GetClampedToMaxSize(MaxAcc) / MaxAcc);
	}

	const float MaxBrakingDec = Character->GetCharacterMovement()->GetMaxBrakingDeceleration();
	return
		Character->GetActorRotation().UnrotateVector(CharacterInformation.Acceleration.GetClampedToMaxSize(MaxBrakingDec) / MaxBrakingDec);
}

float UBMCharacterAnimInstance::CalculateStrideBlend()
{
	// Calculate the Stride Blend. This value is used within the blendspaces to scale the stride (distance feet travel)
	// so that the character can walk or run at different movement speeds.
	// It also allows the walk or run gait animations to blend independently while still matching the animation speed to
	// the movement speed, preventing the character from needing to play a half walk+half run blend.
	// The curves are used to map the stride amount to the speed for maximum control.
	const float CurveTime = CharacterInformation.Speed / GetOwningComponent()->GetComponentScale().Z;
	const float ClampedGait = GetAnimCurveClamped(FName(TEXT("Weight_Gait")), -1.0, 0.0f, 1.0f);
	const float LerpedStrideBlend =
		FMath::Lerp(StrideBlend_N_Walk->GetFloatValue(CurveTime), StrideBlend_N_Run->GetFloatValue(CurveTime), ClampedGait);
	return FMath::Lerp(LerpedStrideBlend, StrideBlend_C_Walk->GetFloatValue(CharacterInformation.Speed),
	                   GetCurveValue(FName(TEXT("BasePose_CLF"))));
}

float UBMCharacterAnimInstance::CalculateWalkRunBlend()
{
	// Calculate the Walk Run Blend. This value is used within the Blendspaces to blend between walking and running.
	return CharacterInformation.Gait == EBMGait::Walking ? 0.0f : 1.0;
}

float UBMCharacterAnimInstance::CalculateStandingPlayRate()
{
	// Calculate the Play Rate by dividing the Character's speed by the Animated Speed for each gait.
	// The lerps are determined by the "Weight_Gait" anim curve that exists on every locomotion cycle so
	// that the play rate is always in sync with the currently blended animation.
	// The value is also divided by the Stride Blend and the mesh scale so that the play rate increases as the stride or scale gets smaller
	const float LerpedSpeed = FMath::Lerp(CharacterInformation.Speed / Config.AnimatedWalkSpeed,
	                                      CharacterInformation.Speed / Config.AnimatedRunSpeed,
	                                      GetAnimCurveClamped(FName(TEXT("Weight_Gait")), -1.0f, 0.0f, 1.0f));

	const float SprintAffectedSpeed = FMath::Lerp(LerpedSpeed, CharacterInformation.Speed / Config.AnimatedSprintSpeed,
	                                              GetAnimCurveClamped(FName(TEXT("Weight_Gait")), -2.0f, 0.0f, 1.0f));

	return FMath::Clamp((SprintAffectedSpeed / Grounded.StrideBlend) / GetOwningComponent()->GetComponentScale().Z, 0.0f, 3.0f);
}

float UBMCharacterAnimInstance::CalculateDiagonalScaleAmount()
{
	// Calculate the Diagnal Scale Amount. This value is used to scale the Foot IK Root bone to make the Foot IK bones
	// cover more distance on the diagonal blends. Without scaling, the feet would not move far enough on the diagonal
	// direction due to the linear translational blending of the IK bones. The curve is used to easily map the value.
	return DiagonalScaleAmountCurve->GetFloatValue(FMath::Abs(Grounded.VelocityBlend.F + Grounded.VelocityBlend.B));
}

float UBMCharacterAnimInstance::CalculateCrouchingPlayRate()
{
	// Calculate the Crouching Play Rate by dividing the Character's speed by the Animated Speed.
	// This value needs to be separate from the standing play rate to improve the blend from crocuh to stand while in motion.
	return FMath::Clamp(
		CharacterInformation.Speed / Config.AnimatedCrouchSpeed / Grounded.StrideBlend / GetOwningComponent()->GetComponentScale().Z,
		0.0f, 2.0f);
}

float UBMCharacterAnimInstance::CalculateLandPrediction()
{
	// Calculate the land prediction weight by tracing in the velocity direction to find a walkable surface the character
	// is falling toward, and getting the 'Time' (range of 0-1, 1 being maximum, 0 being about to land) till impact.
	// The Land Prediction Curve is used to control how the time affects the final weight for a smooth blend. 
	if (InAir.FallSpeed >= -200.0f)
	{
		return 0.0f;
	}

	const FVector& CapsuleWorldLoc = Character->GetCapsuleComponent()->GetComponentLocation();
	const float VelocityZ = CharacterInformation.Velocity.Z;
	FVector VelocityClamped = CharacterInformation.Velocity;
	VelocityClamped.Z = FMath::Clamp(VelocityZ, -4000.0f, -200.0f);
	VelocityClamped.Normalize();

	const FVector TraceLength = VelocityClamped * FMath::GetMappedRangeValueClamped(FVector2D(0.0f, -4000.0f), FVector2D(50.0f, 2000.0f),
	                                                                                VelocityZ);

	UWorld* World = GetWorld();
	check(World);

	FCollisionQueryParams Params;
	Params.AddIgnoredActor(Character);

	FHitResult HitResult;
	World->SweepSingleByProfile(HitResult, CapsuleWorldLoc, CapsuleWorldLoc + TraceLength, FQuat::Identity, FName(TEXT("ALS_Character")),
	                            Character->GetCapsuleComponent()->GetCollisionShape(), Params);

	if (Character->GetCharacterMovement()->IsWalkable(HitResult))
	{
		return FMath::Lerp(LandPredictionCurve->GetFloatValue(HitResult.Time), 0.0f, GetCurveValue(FName(TEXT("Mask_LandPrediction"))));
	}

	return 0.0f;
}

FBMLeanAmount UBMCharacterAnimInstance::CalculateAirLeanAmount()
{
	// Use the relative Velocity direction and amount to determine how much the character should lean while in air.
	// The Lean In Air curve gets the Fall Speed and is used as a multiplier to smoothly reverse the leaning direction
	// when transitioning from moving upwards to moving downwards.
	FBMLeanAmount LeanAmount;
	const FVector& UnrotatedVel = Character->GetActorRotation().UnrotateVector(CharacterInformation.Velocity) / 350.0f;
	FVector2D InversedVect(UnrotatedVel.Y, UnrotatedVel.X);
	InversedVect *= LeanInAirCurve->GetFloatValue(InAir.FallSpeed);
	LeanAmount.LR = InversedVect.X;
	LeanAmount.FB = InversedVect.Y;
	return LeanAmount;
}

static bool AngleInRange(float Angle, float MinAngle, float MaxAngle, float Buffer, bool IncreaseBuffer)
{
	if (IncreaseBuffer)
	{
		return Angle >= MinAngle - Buffer && Angle <= MaxAngle + Buffer;
	}
	return Angle >= MinAngle + Buffer && Angle <= MaxAngle - Buffer;
}

EBMMovementDirection UBMCharacterAnimInstance::CalculateMovementDirection()
{
	// Calculate the Movement Direction. This value represents the direction the character is moving relative to the camera
	// during the Looking Cirection / Aiming rotation modes, and is used in the Cycle Blending Anim Layers to blend to the
	// appropriate directional states.
	if (CharacterInformation.Gait == EBMGait::Sprinting || CharacterInformation.RotationMode == EBMRotationMode::VelocityDirection)
	{
		return EBMMovementDirection::Forward;
	}

	FRotator Delta = CharacterInformation.Velocity.ToOrientationRotator() - CharacterInformation.AimingRotation;
	Delta.Normalize();
	return CalculateQuadrant(Grounded.MovementDirection, 70.0f, -70.0f, 110.0f, -110.0f, 5.0f, Delta.Yaw);
}

EBMMovementDirection UBMCharacterAnimInstance::CalculateQuadrant(EBMMovementDirection Current, float FRThreshold, float FLThreshold,
                                                                 float BRThreshold, float BLThreshold, float Buffer, float Angle)
{
	// Take the input angle and determine its quadrant (direction). Use the current Movement Direction to increase or
	// decrease the buffers on the angle ranges for each quadrant.
	if (AngleInRange(Angle, FLThreshold, FRThreshold, Buffer,
	                 Current != EBMMovementDirection::Forward || Current != EBMMovementDirection::Backward))
	{
		return EBMMovementDirection::Forward;
	}

	if (AngleInRange(Angle, FRThreshold, BRThreshold, Buffer,
	                 Current != EBMMovementDirection::Right || Current != EBMMovementDirection::Left))
	{
		return EBMMovementDirection::Right;
	}

	if (AngleInRange(Angle, BLThreshold, FLThreshold, Buffer,
	                 Current != EBMMovementDirection::Right || Current != EBMMovementDirection::Left))
	{
		return EBMMovementDirection::Left;
	}

	return EBMMovementDirection::Backward;
}

void UBMCharacterAnimInstance::TurnInPlace(FRotator TargetRotation, float PlayRateScale, float StartTime,
                                           bool OverrideCurrent)
{
	// Step 1: Set Turn Angle
	FRotator Delta = TargetRotation - Character->GetActorRotation();
	Delta.Normalize();
	const float TurnAngle = Delta.Yaw;

	FBMTurnInPlaceAsset TargetTurnAsset;
	// Step 2: Choose Turn Asset based on the Turn Angle and Stance
	if (FMath::Abs(TurnAngle) < TurnInPlaceValues.Turn180Threshold)
	{
		if (TurnAngle < 0.0f)
		{
			TargetTurnAsset = CharacterInformation.Stance == EBMStance::Standing
				                  ? TurnInPlaceValues.N_TurnIP_R90
				                  : TurnInPlaceValues.CLF_TurnIP_L90;
		}
		else
		{
			TargetTurnAsset = CharacterInformation.Stance == EBMStance::Standing
				                  ? TurnInPlaceValues.N_TurnIP_L90
				                  : TurnInPlaceValues.CLF_TurnIP_R90;
		}
	}
	else
	{
		if (TurnAngle < 0.0f)
		{
			TargetTurnAsset = CharacterInformation.Stance == EBMStance::Standing
				                  ? TurnInPlaceValues.N_TurnIP_L180
				                  : TurnInPlaceValues.CLF_TurnIP_L180;
		}
		else
		{
			TargetTurnAsset = CharacterInformation.Stance == EBMStance::Standing
				                  ? TurnInPlaceValues.N_TurnIP_R180
				                  : TurnInPlaceValues.CLF_TurnIP_R180;
		}
	}

	// Step 3: If the Target Turn Animation is not playing or set to be overriden, play the turn animation as a dynamic montage.
	if (!OverrideCurrent && IsPlayingSlotAnimation(TargetTurnAsset.Animation, TargetTurnAsset.SlotName))
	{
		return;
	}
	PlaySlotAnimationAsDynamicMontage(TargetTurnAsset.Animation, TargetTurnAsset.SlotName, 0.2f, 0.2f,
	                                  TargetTurnAsset.PlayRate * PlayRateScale, 1, 0.0f, StartTime);

	// Step 4: Scale the rotation amount (gets scaled in animgraph) to compensate for turn angle (If Allowed) and play rate.
	if (TargetTurnAsset.ScaleTurnAngle)
	{
		Grounded.RotationScale = (TurnAngle / TargetTurnAsset.AnimatedAngle) * TargetTurnAsset.PlayRate * PlayRateScale;
	}
	else
	{
		Grounded.RotationScale = TargetTurnAsset.PlayRate * PlayRateScale;
	}
}

void UBMCharacterAnimInstance::OnJumped()
{
	InAir.bJumped = true;
	InAir.JumpPlayRate = FMath::GetMappedRangeValueClamped(FVector2D(0.0f, 600.0f),
	                                                       FVector2D(1.2f, 1.5f), CharacterInformation.Speed);

	Character->GetWorldTimerManager().SetTimer(OnJumpedTimer, this,
	                                           &UBMCharacterAnimInstance::OnJumpedDelay, 0.1f, false);
}

void UBMCharacterAnimInstance::OnPivot()
{
	Grounded.bPivot = CharacterInformation.Speed < Config.TriggerPivotSpeedLimit;
	Character->GetWorldTimerManager().SetTimer(OnPivotTimer, this,
	                                           &UBMCharacterAnimInstance::OnPivotDelay, 0.1f, false);
}