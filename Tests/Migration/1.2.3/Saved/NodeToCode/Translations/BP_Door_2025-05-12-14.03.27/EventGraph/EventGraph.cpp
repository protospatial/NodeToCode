void ABP_Door::BeginPlay()
{
    Super::BeginPlay();
    UKismetSystemLibrary::PrintString(this, TEXT("Door ready"));
}