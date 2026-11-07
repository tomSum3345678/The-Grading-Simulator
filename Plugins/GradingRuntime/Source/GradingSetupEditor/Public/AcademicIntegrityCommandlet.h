#pragma once

#include "Commandlets/Commandlet.h"
#include "AcademicIntegrityCommandlet.generated.h"

/** Editor-only installer for the academic integrity Blueprint assets. */
UCLASS()
class UInstallAcademicIntegrityCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UInstallAcademicIntegrityCommandlet();
    virtual int32 Main(const FString& Params) override;
};
