//===--- ARC.cpp - Implement ARC target feature support -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements ARC TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#include "ARC.h"
#include "latino/Basic/Builtins.h"
#include "latino/Basic/MacroBuilder.h"
#include "latino/Basic/TargetBuiltins.h"

using namespace latino;
using namespace latino::targets;

void ARCTargetInfo::getTargetDefines(const LangOptions &Opts,
                                     MacroBuilder &Builder) const {
  Builder.defineMacro("__arc__");
}
