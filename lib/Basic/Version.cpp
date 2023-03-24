//===- Version.cpp - Clang Version Number -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines several version-related utility functions for Clang.
//
//===----------------------------------------------------------------------===//

#include "latino/Basic/Version.h"
#include "latino/Basic/LLVM.h"
#include "latino/Config/config.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdlib>
#include <cstring>

#ifdef HAVE_VCS_VERSION_INC
#include "VCSVersion.inc"
#endif

namespace latino {

std::string getLatinoRepositoryPath() {
#if defined(LATINO_REPOSITORY_STRING)
  return LATINO_REPOSITORY_STRING;
#else
#ifdef LATINO_REPOSITORY
  return LATINO_REPOSITORY;
#else
  return "";
#endif
#endif
}

std::string getLLVMRepositoryPath() {
#ifdef LLVM_REPOSITORY
  return LLVM_REPOSITORY;
#else
  return "";
#endif
}

std::string getLatinoRevision() {
#ifdef LATINO_REVISION
  return LATINO_REVISION;
#else
  return "";
#endif
}

std::string getLLVMRevision() {
#ifdef LLVM_REVISION
  return LLVM_REVISION;
#else
  return "";
#endif
}

std::string getLatinoFullRepositoryVersion() {
  std::string buf;
  llvm::raw_string_ostream OS(buf);
  std::string Path = getLatinoRepositoryPath();
  std::string Revision = getLatinoRevision();
  if (!Path.empty() || !Revision.empty()) {
    OS << '(';
    if (!Path.empty())
      OS << Path;
    if (!Revision.empty()) {
      if (!Path.empty())
        OS << ' ';
      OS << Revision;
    }
    OS << ')';
  }
  // Support LLVM in a separate repository.
  std::string LLVMRev = getLLVMRevision();
  if (!LLVMRev.empty() && LLVMRev != Revision) {
    OS << " (";
    std::string LLVMRepo = getLLVMRepositoryPath();
    if (!LLVMRepo.empty())
      OS << LLVMRepo << ' ';
    OS << LLVMRev << ')';
  }
  return OS.str();
}

std::string getLatinoFullVersion() {
  return getLatinoToolFullVersion("latino");
}

std::string getLatinoToolFullVersion(StringRef ToolName) {
  std::string buf;
  llvm::raw_string_ostream OS(buf);
#ifdef LATINO_VENDOR
  OS << LATINO_VENDOR;
#endif
  OS << ToolName << " version " LATINO_VERSION_STRING;

  std::string repo = getLatinoFullRepositoryVersion();
  if (!repo.empty()) {
    OS << " " << repo;
  }

  return OS.str();
}

std::string getLatinoFullCPPVersion() {
  // The version string we report in __VERSION__ is just a compacted version of
  // the one we report on the command line.
  std::string buf;
  llvm::raw_string_ostream OS(buf);
#ifdef LATINO_VENDOR
  OS << LATINO_VENDOR;
#endif
  OS << "Latino " LATINO_VERSION_STRING;

  std::string repo = getLatinoFullRepositoryVersion();
  if (!repo.empty()) {
    OS << " " << repo;
  }

  return OS.str();
}

} // end namespace latino
