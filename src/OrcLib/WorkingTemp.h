#pragma once

#include <string>

#include "Utils/Result.h"

namespace Orc {

Result<std::wstring> CreateWorkingTemp(std::optional<std::wstring> basedir = {});
Result<std::wstring> CreateRestrictedDirectory(const std::wstring& workingTemp);

}
