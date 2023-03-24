//===- Version.h - Clang Version Number -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Defines version macros and version-related utility functions
/// for Clang.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LATINO_BASIC_VERSION_H
#define LLVM_LATINO_BASIC_VERSION_H

#include "latino/Basic/Version.inc"
#include "llvm/ADT/StringRef.h"

namespace latino {
  /// Retrieves the repository path (e.g., Subversion path) that
  /// identifies the particular Clang branch, tag, or trunk from which this
  /// Clang was built.
  std::string getLatinoRepositoryPath();

  /// Retrieves the repository path from which LLVM was built.
  ///
  /// This supports LLVM residing in a separate repository from clang.
  std::string getLLVMRepositoryPath();

  /// Retrieves the repository revision number (or identifier) from which
  /// this Clang was built.
  std::string getLatinoRevision();

  /// Retrieves the repository revision number (or identifier) from which
  /// LLVM was built.
  ///
  /// If Clang and LLVM are in the same repository, this returns the same
  /// string as getLatinoRevision.
  std::string getLLVMRevision();

  /// Retrieves the full repository version that is an amalgamation of
  /// the information in getLatinoRepositoryPath() and getLatinoRevision().
  std::string getLatinoFullRepositoryVersion();

  /// Retrieves a string representing the complete clang version,
  /// which includes the clang version number, the repository version,
  /// and the vendor tag.
  std::string getLatinoFullVersion();

  /// Like getLatinoFullVersion(), but with a custom tool name.
  std::string getLatinoToolFullVersion(llvm::StringRef ToolName);

  /// Retrieves a string representing the complete clang version suitable
  /// for use in the CPP __VERSION__ macro, which includes the clang version
  /// number, the repository version, and the vendor tag.
  std::string getLatinoFullCPPVersion();
}

#endif // LLVM_LATINO_BASIC_VERSION_H
